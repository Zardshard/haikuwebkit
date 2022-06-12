/*
    Copyright (C) 2008 Nokia Corporation and/or its subsidiary(-ies)
    Copyright (C) 2007 Staikos Computing Services Inc.  <info@staikos.net>
    Copyright (C) 2008 Holger Hans Peter Freyther

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public License
    along with this library; see the file COPYING.LIB.  If not, write to
    the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
    Boston, MA 02110-1301, USA.
*/
#include "config.h"
#include "BUrlProtocolHandler.h"

#include "FormData.h"
#include "HTTPParsers.h"
#include "ImageData.h"
#include "MIMETypeRegistry.h"
#include "NetworkStorageSession.h"
#include "ProtectionSpace.h"
#include "ResourceHandle.h"
#include "ResourceHandleClient.h"
#include "ResourceHandleInternal.h"
#include "ResourceResponse.h"
#include "ResourceRequest.h"
#include "SecurityOrigin.h"
#include "SharedBuffer.h"

#include <wtf/CompletionHandler.h>
#include <wtf/text/CString.h>

#include <Autolock.h>
#include <Debug.h>
#include <File.h>
#include <Url.h>
#include <UrlRequest.h>
#include <HttpRequest.h>

#include <assert.h>

static const int gMaxRecursionLimit = 10;

namespace WebCore {

static bool shouldRedirectAsGET(const ResourceRequest& request, int statusCode, bool crossOrigin)
{
    if (request.httpMethod() == ASCIILiteral::fromLiteralUnsafe("GET") || request.httpMethod() == ASCIILiteral::fromLiteralUnsafe("HEAD"))
        return false;

    if (statusCode == 303)
        return true;

    if ((statusCode == 301 || statusCode == 302) && request.httpMethod() == ASCIILiteral::fromLiteralUnsafe("POST"))
        return true;

    if (crossOrigin && request.httpMethod() == ASCIILiteral::fromLiteralUnsafe("DELETE"))
        return true;

    return false;
}

RefPtr<BUrlRequestWrapper> BUrlRequestWrapper::create(BUrlProtocolHandler* handler, NetworkStorageSession* storageSession, ResourceRequest& request)
{
    return adoptRef(*new BUrlRequestWrapper(handler, storageSession, request));
}

BUrlRequestWrapper::BUrlRequestWrapper(BUrlProtocolHandler* handler, NetworkStorageSession* storageSession, ResourceRequest& request)
    : BUrlProtocolAsynchronousListener(true)
    , m_handler(handler)
    , m_receiveMutex("http data locker")
{
    ASSERT(isMainThread());
    ASSERT(m_handler);
    ASSERT(storageSession);

    m_request = request.toNetworkRequest(&storageSession->platformSession());

    if (!m_request)
        return;

    m_request->SetListener(SynchronousListener());
    m_request->SetOutput(this);

    BPrivate::Network::BHttpRequest* httpRequest = dynamic_cast<BPrivate::Network::BHttpRequest*>(m_request);
    if (httpRequest) {
        if (request.httpMethod() == ASCIILiteral::fromLiteralUnsafe("POST")
            || request.httpMethod() == ASCIILiteral::fromLiteralUnsafe("PUT")) {
            if (request.httpBody()) {
                auto postData = new BFormDataIO(request.httpBody(), storageSession->sessionID());
                httpRequest->AdoptInputData(postData, postData->Size());
            }
        }

        httpRequest->SetMethod(request.httpMethod().utf8().data());
        // Redirections will be handled by this class.
        httpRequest->SetFollowLocation(false);
    } else if (request.httpMethod() != ASCIILiteral::fromLiteralUnsafe("GET")) {
        // Only the HTTP backend support things other than GET.
        // Remove m_request to signify to ResourceHandle that the request was
        // invalid.
        delete m_request;
        m_request = NULL;
        return;
    }

    // Keep self alive while BUrlRequest is running as we hold
    // the main dispatcher.
    ref();

    // Block the receiving thread until headers are parsed.
    m_receiveMutex.Lock();

    if (m_request->Run() < B_OK) {
        deref();

        ResourceError error(ASCIILiteral::fromLiteralUnsafe("BUrlProtocol"), 42,
            request.url(),
            ASCIILiteral::fromLiteralUnsafe("The service kit failed to start the request."));
        m_handler->didFail(error);

        return;
    }
}

BUrlRequestWrapper::~BUrlRequestWrapper()
{
    abort();
    delete m_request;
}

void BUrlRequestWrapper::abort()
{
    ASSERT(isMainThread());

    // Lock if we have already unblocked the receive thread to
    // synchronize cancellation status.
    if (!m_receiveMutex.IsLocked())
        m_receiveMutex.Lock();

    m_handler = nullptr;

    // If the receive thread is still blocked, unblock it so that it
    // become aware of the state change.
    m_receiveMutex.Unlock();

    if (m_request)
        m_request->Stop();
}

void BUrlRequestWrapper::HeadersReceived(BPrivate::Network::BUrlRequest* caller)
{
    ASSERT(isMainThread());

    if (!m_handler)
        return;

    const BPrivate::Network::BUrlResult& result = caller->Result();

    String contentType = String::fromUTF8(result.ContentType());
    ResourceResponse response(URL(caller->Url()),
        extractMIMETypeFromMediaType(contentType), result.Length(),
        extractCharsetFromMediaType(contentType).toString());

    const BPrivate::Network::BHttpResult* httpResult = dynamic_cast<const BPrivate::Network::BHttpResult*>(&result);
    if (httpResult) {
        StringView suggestedFilename = filenameFromHTTPContentDisposition(
            String::fromUTF8(httpResult->Headers()["Content-Disposition"]));

        if (!suggestedFilename.isEmpty())
            response.setSuggestedFilename(suggestedFilename.toString());

        response.setHTTPStatusCode(httpResult->StatusCode());
        response.setHTTPStatusText(AtomString::fromUTF8(httpResult->StatusText().String()));

        // Add remaining headers.
        const BPrivate::Network::BHttpHeaders& resultHeaders = httpResult->Headers();
        for (int i = 0; i < resultHeaders.CountHeaders(); i++) {
            BPrivate::Network::BHttpHeader& headerPair = resultHeaders.HeaderAt(i);
            response.setHTTPHeaderField(String::fromUTF8(headerPair.Name()), String::fromUTF8(headerPair.Value()));
        }

        if (response.isRedirection() && !response.httpHeaderField(HTTPHeaderName::Location).isEmpty()) {
            m_handler->willSendRequest(response);
            return;
        }

        if (response.httpStatusCode() == 401 && m_handler->didReceiveAuthenticationChallenge(response))
            return;
    }

    ResourceResponse responseCopy = response;
    m_handler->didReceiveResponse(WTFMove(responseCopy));

    // Unblock receive thread
    if (m_receiveMutex.IsLocked()) {
        m_receiveMutex.Unlock();
    }
}

void BUrlRequestWrapper::UploadProgress(BPrivate::Network::BUrlRequest*, off_t bytesSent, off_t bytesTotal)
{
    ASSERT(isMainThread());

    if (!m_handler)
        return;

    m_handler->didSendData(bytesSent, bytesTotal);
}

void BUrlRequestWrapper::RequestCompleted(BPrivate::Network::BUrlRequest* caller, bool success)
{
    ASSERT(isMainThread());

    // We held a pointer to keep the main dispatcher alive for the duration
    // of the request run.
    //
    // As the request completes, we adopt the ref here so that it can
    // release itself after completion.
    auto releaseThis = adoptRef(*this);

    if (!m_handler)
        return;

    BPrivate::Network::BHttpRequest* httpRequest = dynamic_cast<BPrivate::Network::BHttpRequest*>(m_request);

    if (success || (httpRequest && m_didReceiveData)) {
        m_handler->didFinishLoading();
        return;
    } else if (httpRequest) {
        const BPrivate::Network::BHttpResult& result = static_cast<const BPrivate::Network::BHttpResult&>(httpRequest->Result());
        int httpStatusCode = result.StatusCode();

        if (httpStatusCode != 0) {
            ResourceError error(ASCIILiteral::fromLiteralUnsafe("HTTP"), httpStatusCode,
                URL(caller->Url()), String::fromUTF8(strerror(caller->Status())));

            m_handler->didFail(error);
            return;
        }
    }

    // If we get here, it means we are in failure without an HTTP error code
    // (DNS error, or error from a protocol other than HTTP).
    ResourceError error(ASCIILiteral::fromLiteralUnsafe("BUrlRequest"), caller->Status(), URL(caller->Url()), String::fromUTF8(strerror(caller->Status())));
    m_handler->didFail(error);
}

bool BUrlRequestWrapper::CertificateVerificationFailed(BPrivate::Network::BUrlRequest*,
    BCertificate& certificate, const char* message)
{
    ASSERT(isMainThread());

    if (!m_handler)
        return false;

    return m_handler->didReceiveInvalidCertificate(certificate, message);
}

ssize_t BUrlRequestWrapper::Write(const void* data, size_t size)
{
    BAutolock locker(m_receiveMutex);

    if (!m_handler)
        return size;

    if (size > 0) {
        m_didReceiveData = true;

        auto buffer = SharedBuffer::create(reinterpret_cast<const char*>(data), size);

        callOnMainThread([this, protectedThis = Ref{*this}, buffer = WTFMove(buffer)]() mutable {
            if (m_handler)
                m_handler->didReceiveBuffer(WTFMove(buffer));
        });
    }

    return size;
}

BUrlProtocolHandler::BUrlProtocolHandler(ResourceHandle* handle)
    : m_resourceHandle(handle)
    , m_request()
{
    if (!m_resourceHandle)
        return;

    m_resourceRequest = m_resourceHandle->firstRequest();
    m_request = BUrlRequestWrapper::create(this,
        m_resourceHandle->context()->storageSession(),
        m_resourceRequest);
}

BUrlProtocolHandler::~BUrlProtocolHandler()
{
    abort();
}

void BUrlProtocolHandler::abort()
{
    ASSERT(isMainThread());

    if (m_request)
        m_request->abort();

    m_resourceHandle = nullptr;
}

void BUrlProtocolHandler::didFail(const ResourceError& error)
{
    ASSERT(isMainThread());

    ResourceHandleClient* client = m_resourceHandle->client();
    if (!client)
        return;

    client->didFail(m_resourceHandle, error);
}

void BUrlProtocolHandler::willSendRequest(const ResourceResponse& response)
{
    ASSERT(isMainThread());

    if (!m_resourceHandle)
        return;

    ResourceHandleClient* client = m_resourceHandle->client();
    if (!client)
        return;

    ResourceRequest request = m_resourceHandle->firstRequest();

    m_redirectionTries++;

    if (m_redirectionTries > gMaxRecursionLimit) {
        ResourceError error(request.url().host().toString(), 400, request.url(),
            ASCIILiteral::fromLiteralUnsafe("Redirection limit reached"));
        client->didFail(m_resourceHandle, error);
        return;
    }

    URL newUrl = URL(request.url(), response.httpHeaderField(HTTPHeaderName::Location));

    bool crossOrigin = !protocolHostAndPortAreEqual(request.url(), newUrl);

    request.setURL(newUrl);

    if (!newUrl.protocolIsInHTTPFamily() || shouldRedirectAsGET(request, response.httpStatusCode(), crossOrigin)) {
        request.setHTTPMethod(ASCIILiteral::fromLiteralUnsafe("GET"));
        request.setHTTPBody(nullptr);
        request.clearHTTPContentType();
    }

    if (crossOrigin) {
        request.clearHTTPAuthorization();
        request.clearHTTPOrigin();
    }

    m_request->abort();
    ResourceResponse responseCopy = response;
    client->willSendRequestAsync(m_resourceHandle, WTFMove(request), WTFMove(responseCopy), [this, protectedThis = Ref { *this }] (ResourceRequest&& request) {
        continueAfterWillSendRequest(WTFMove(request));
    });
}

void BUrlProtocolHandler::continueAfterWillSendRequest(ResourceRequest&& request)
{
    ASSERT(isMainThread());

    // willSendRequestAsync might cancel the request
    if (!m_resourceHandle || !m_resourceHandle->client() || request.isNull())
        return;

    m_resourceRequest = request;
    m_request = BUrlRequestWrapper::create(this, m_resourceHandle->context()->storageSession(), request);
}

bool BUrlProtocolHandler::didReceiveAuthenticationChallenge(const ResourceResponse& response)
{
    ASSERT(isMainThread());

    if (!m_resourceHandle || !m_resourceHandle->client())
        return false;

    const URL& url = response.url();
    auto serverType = WebCore::ProtectionSpaceBase::ServerType::HTTP;
    if (url.protocolIs(ASCIILiteral::fromLiteralUnsafe("https")))
        serverType = WebCore::ProtectionSpaceBase::ServerType::HTTPS;
    // FIXME handle other types (FTP and proxy stuff)

    static ASCIILiteral wwwAuthenticate = ASCIILiteral::fromLiteralUnsafe("www-authenticate");
    String challenge = response.httpHeaderField(wwwAuthenticate);

    auto scheme = WebCore::ProtectionSpaceBase::AuthenticationScheme::Default;
    // TODO according to RFC7235, there could be more than one challenge in WWW-Authenticate. We
    // should parse them all, instead of just the first one.
    if (challenge.startsWith(ASCIILiteral::fromLiteralUnsafe("Digest")))
        scheme = WebCore::ProtectionSpaceBase::AuthenticationScheme::HTTPDigest;
    else if (challenge.startsWith(ASCIILiteral::fromLiteralUnsafe("Basic")))
        scheme = WebCore::ProtectionSpaceBase::AuthenticationScheme::HTTPBasic;
    else {
        // Unknown authentication type, ignore (various websites are intercepting the auth and
        // handling it by themselves)
        return false;
    }

    String realm;
    int realmStart = challenge.find(ASCIILiteral::fromLiteralUnsafe("realm=\""), 0);
    if (realmStart > 0) {
        realmStart += 7;
        int realmEnd = challenge.find(ASCIILiteral::fromLiteralUnsafe("\""), realmStart);
        if (realmEnd >= 0)
            realm = challenge.substring(realmStart, realmEnd - realmStart);
    }

    int port;
    if (url.port())
        port = *url.port();
    else if (url.protocolIs(ASCIILiteral::fromLiteralUnsafe("https")))
        port = 443;
    else
        port = 80;
    String host = url.host().toString();
    ProtectionSpace protectionSpace(host, port, serverType, realm, scheme);
    ResourceError resourceError(host, 401, url, String());

    ResourceHandleInternal* d = m_resourceHandle->getInternal();

    Credential proposedCredential(d->m_user, d->m_password, CredentialPersistenceForSession);

    AuthenticationChallenge authenticationChallenge(protectionSpace,
        proposedCredential, m_authenticationTries++, response, resourceError);
    authenticationChallenge.m_authenticationClient = m_resourceHandle;
    m_resourceHandle->didReceiveAuthenticationChallenge(authenticationChallenge);
    // will set m_user and m_password in ResourceHandleInternal

    if (!d->m_user.isEmpty()) {
        ResourceRequest request = m_resourceRequest;
        ResourceResponse responseCopy = response;
        request.setCredentials(d->m_user.utf8().data(), d->m_password.utf8().data());
        m_request->abort();
        m_resourceHandle->client()->willSendRequestAsync(m_resourceHandle,
                WTFMove(request), WTFMove(responseCopy), [this] (ResourceRequest&& request) {
            continueAfterWillSendRequest(WTFMove(request));
        });
        return true;
    }

    return false;
}

void BUrlProtocolHandler::didReceiveResponse(ResourceResponse&& response)
{
    ASSERT(isMainThread());

    if (!m_resourceHandle)
        return;

    // Make sure the resource handle is not deleted immediately, otherwise
    // didReceiveResponse would crash. Keep a reference to it so it can be
    // deleted cleanly after the function returns.
    auto protectedHandle = Ref{*m_resourceHandle};
    protectedHandle->didReceiveResponse(WTFMove(response), [this/*, protectedThis = Ref{*this}*/] {
        //continueAfterDidReceiveResponse();
    });
}

void BUrlProtocolHandler::didReceiveBuffer(Ref<SharedBuffer>&& buffer)
{
    ASSERT(isMainThread());

    if (!m_resourceHandle || !m_resourceHandle->client())
        return;

    m_resourceHandle->client()->didReceiveBuffer(m_resourceHandle, WTFMove(buffer), buffer->size());
}

void BUrlProtocolHandler::didSendData(ssize_t bytesSent, ssize_t bytesTotal)
{
    ASSERT(isMainThread());

    if (!m_resourceHandle || !m_resourceHandle->client())
        return;

    m_resourceHandle->client()->didSendData(m_resourceHandle, bytesSent, bytesTotal);
}

void BUrlProtocolHandler::didFinishLoading()
{
    ASSERT(isMainThread());

    if (!m_resourceHandle || !m_resourceHandle->client())
        return;

    const WebCore::NetworkLoadMetrics metrics;
    m_resourceHandle->client()->didFinishLoading(m_resourceHandle, metrics);
}

bool BUrlProtocolHandler::didReceiveInvalidCertificate(BCertificate& certificate, const char* message)
{
    ASSERT(isMainThread());

    if (!m_resourceHandle)
        return false;

    return m_resourceHandle->didReceiveInvalidCertificate(certificate, message);
}

} // namespace WebCore
