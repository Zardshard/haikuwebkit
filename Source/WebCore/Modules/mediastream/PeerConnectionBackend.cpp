/*
 * Copyright (C) 2015 Ericsson AB. All rights reserved.
 * Copyright (C) 2016-2017 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of Ericsson nor the names of its contributors
 *    may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "PeerConnectionBackend.h"

#if ENABLE(WEB_RTC)

#include "EventNames.h"
#include "JSDOMPromiseDeferred.h"
#include "JSRTCSessionDescriptionInit.h"
#include "LibWebRTCCertificateGenerator.h"
#include "Logging.h"
#include "Page.h"
#include "RTCDtlsTransport.h"
#include "RTCIceCandidate.h"
#include "RTCPeerConnection.h"
#include "RTCPeerConnectionIceEvent.h"
#include "RTCRtpCapabilities.h"
#include "RTCSctpTransportBackend.h"
#include "RTCSessionDescriptionInit.h"
#include "RTCTrackEvent.h"
#include "RuntimeEnabledFeatures.h"
#include <wtf/UUID.h>
#include <wtf/text/StringBuilder.h>
#include <wtf/text/StringConcatenateNumbers.h>

namespace WebCore {

#if !USE(LIBWEBRTC)
static std::unique_ptr<PeerConnectionBackend> createNoPeerConnectionBackend(RTCPeerConnection&)
{
    return nullptr;
}

CreatePeerConnectionBackend PeerConnectionBackend::create = createNoPeerConnectionBackend;

std::optional<RTCRtpCapabilities> PeerConnectionBackend::receiverCapabilities(ScriptExecutionContext&, const String&)
{
    ASSERT_NOT_REACHED();
    return { };
}

std::optional<RTCRtpCapabilities> PeerConnectionBackend::senderCapabilities(ScriptExecutionContext&, const String&)
{
    ASSERT_NOT_REACHED();
    return { };
}
#endif

PeerConnectionBackend::PeerConnectionBackend(RTCPeerConnection& peerConnection)
    : m_peerConnection(peerConnection)
#if !RELEASE_LOG_DISABLED
    , m_logger(peerConnection.logger())
    , m_logIdentifier(peerConnection.logIdentifier())
#endif
{
#if USE(LIBWEBRTC)
    auto* document = peerConnection.document();
    if (auto* page = document ? document->page() : nullptr)
        m_shouldFilterICECandidates = page->libWebRTCProvider().isSupportingMDNS();
#endif
}

PeerConnectionBackend::~PeerConnectionBackend() = default;

void PeerConnectionBackend::createOffer(RTCOfferOptions&& options, PeerConnection::SessionDescriptionPromise&& promise)
{
    ASSERT(!m_offerAnswerPromise);
    ASSERT(!m_peerConnection.isClosed());

    m_offerAnswerPromise = WTF::makeUnique<PeerConnection::SessionDescriptionPromise>(WTFMove(promise));
    doCreateOffer(WTFMove(options));
}

void PeerConnectionBackend::createOfferSucceeded(String&& sdp)
{
    ASSERT(isMainThread());
    ALWAYS_LOG(LOGIDENTIFIER, "Create offer succeeded:\n", sdp);

    ASSERT(m_offerAnswerPromise);
    validateSDP(sdp);
    m_peerConnection.doTask([this, promise = WTFMove(m_offerAnswerPromise), sdp = WTFMove(sdp)]() mutable {
        if (m_peerConnection.isClosed())
            return;

        promise->resolve(RTCSessionDescriptionInit { RTCSdpType::Offer, sdp });
    });
}

void PeerConnectionBackend::createOfferFailed(Exception&& exception)
{
    ASSERT(isMainThread());
    ALWAYS_LOG(LOGIDENTIFIER, "Create offer failed:", exception.message());

    ASSERT(m_offerAnswerPromise);
    m_peerConnection.doTask([this, promise = WTFMove(m_offerAnswerPromise), exception = WTFMove(exception)]() mutable {
        if (m_peerConnection.isClosed())
            return;

        promise->reject(WTFMove(exception));
    });
}

void PeerConnectionBackend::createAnswer(RTCAnswerOptions&& options, PeerConnection::SessionDescriptionPromise&& promise)
{
    ASSERT(!m_offerAnswerPromise);
    ASSERT(!m_peerConnection.isClosed());

    m_offerAnswerPromise = WTF::makeUnique<PeerConnection::SessionDescriptionPromise>(WTFMove(promise));
    doCreateAnswer(WTFMove(options));
}

void PeerConnectionBackend::createAnswerSucceeded(String&& sdp)
{
    ASSERT(isMainThread());
    ALWAYS_LOG(LOGIDENTIFIER, "Create answer succeeded:\n", sdp);

    ASSERT(m_offerAnswerPromise);
    m_peerConnection.doTask([this, promise = WTFMove(m_offerAnswerPromise), sdp = WTFMove(sdp)]() mutable {
        if (m_peerConnection.isClosed())
            return;

        promise->resolve(RTCSessionDescriptionInit { RTCSdpType::Answer, sdp });
    });
}

void PeerConnectionBackend::createAnswerFailed(Exception&& exception)
{
    ASSERT(isMainThread());
    ALWAYS_LOG(LOGIDENTIFIER, "Create answer failed:", exception.message());

    ASSERT(m_offerAnswerPromise);
    m_peerConnection.doTask([this, promise = WTFMove(m_offerAnswerPromise), exception = WTFMove(exception)]() mutable {
        if (m_peerConnection.isClosed())
            return;

        promise->reject(WTFMove(exception));
    });
}

void PeerConnectionBackend::setLocalDescription(const RTCSessionDescription* sessionDescription, Function<void(ExceptionOr<void>&&)>&& callback)
{
    ASSERT(!m_peerConnection.isClosed());

    m_setDescriptionCallback = WTFMove(callback);
    doSetLocalDescription(sessionDescription);
}

void PeerConnectionBackend::setLocalDescriptionSucceeded(std::optional<DescriptionStates>&& descriptionStates, std::unique_ptr<RTCSctpTransportBackend>&& sctpBackend)
{
    ASSERT(isMainThread());
    ALWAYS_LOG(LOGIDENTIFIER);

    ASSERT(m_setDescriptionCallback);
    m_peerConnection.doTask([this, callback = WTFMove(m_setDescriptionCallback), descriptionStates = WTFMove(descriptionStates), sctpBackend = WTFMove(sctpBackend)]() mutable {
        if (m_peerConnection.isClosed())
            return;

        if (descriptionStates)
            m_peerConnection.updateDescriptions(WTFMove(*descriptionStates));
        m_peerConnection.updateTransceiversAfterSuccessfulLocalDescription();
        m_peerConnection.updateSctpBackend(WTFMove(sctpBackend));
        callback({ });
    });
}

void PeerConnectionBackend::setLocalDescriptionFailed(Exception&& exception)
{
    ASSERT(isMainThread());
    ALWAYS_LOG(LOGIDENTIFIER, "Set local description failed:", exception.message());

    ASSERT(m_setDescriptionCallback);
    m_peerConnection.doTask([this, callback = WTFMove(m_setDescriptionCallback), exception = WTFMove(exception)]() mutable {
        if (m_peerConnection.isClosed())
            return;

        callback(WTFMove(exception));
    });
}

void PeerConnectionBackend::setRemoteDescription(const RTCSessionDescription& sessionDescription, Function<void(ExceptionOr<void>&&)>&& callback)
{
    ASSERT(!m_peerConnection.isClosed());

    m_setDescriptionCallback = WTFMove(callback);
    doSetRemoteDescription(sessionDescription);
}

void PeerConnectionBackend::setRemoteDescriptionSucceeded(std::optional<DescriptionStates>&& descriptionStates, std::unique_ptr<RTCSctpTransportBackend>&& sctpBackend)
{
    ASSERT(isMainThread());
    ALWAYS_LOG(LOGIDENTIFIER, "Set remote description succeeded");
    ASSERT(m_setDescriptionCallback);

    auto callback = WTFMove(m_setDescriptionCallback);
    auto events = WTFMove(m_pendingTrackEvents);
    for (auto& event : events) {
        auto& track = event.track.get();

        m_peerConnection.dispatchEventWhenFeasible(RTCTrackEvent::create(eventNames().trackEvent, Event::CanBubble::No, Event::IsCancelable::No, WTFMove(event.receiver), WTFMove(event.track), WTFMove(event.streams), WTFMove(event.transceiver)));
        ALWAYS_LOG(LOGIDENTIFIER, "Dispatched if feasible track of type ", track.source().type());

        if (m_peerConnection.isClosed())
            return;

        // FIXME: As per spec, we should set muted to 'false' when starting to receive the content from network.
        track.source().setMuted(false);
    }

    m_peerConnection.doTask([this, callback = WTFMove(callback), descriptionStates = WTFMove(descriptionStates), sctpBackend = WTFMove(sctpBackend)]() mutable {
        if (m_peerConnection.isClosed())
            return;

        if (descriptionStates)
            m_peerConnection.updateDescriptions(WTFMove(*descriptionStates));
        m_peerConnection.updateTransceiversAfterSuccessfulRemoteDescription();
        m_peerConnection.updateSctpBackend(WTFMove(sctpBackend));
        callback({ });
    });
}

void PeerConnectionBackend::setRemoteDescriptionFailed(Exception&& exception)
{
    ASSERT(isMainThread());
    ALWAYS_LOG(LOGIDENTIFIER, "Set remote description failed:", exception.message());

    ASSERT(m_pendingTrackEvents.isEmpty());
    m_pendingTrackEvents.clear();

    ASSERT(m_setDescriptionCallback);
    m_peerConnection.doTask([this, callback = WTFMove(m_setDescriptionCallback), exception = WTFMove(exception)]() mutable {
        if (m_peerConnection.isClosed())
            return;

        callback(WTFMove(exception));
    });
}

void PeerConnectionBackend::addPendingTrackEvent(PendingTrackEvent&& event)
{
    ASSERT(!m_peerConnection.isStopped());
    m_pendingTrackEvents.append(WTFMove(event));
}

static String extractIPAddress(StringView sdp)
{
    unsigned counter = 0;
    for (auto item : StringView { sdp }.split(' ')) {
        if (++counter == 5)
            return item.toString();
    }
    return { };
}

static inline bool shouldIgnoreIceCandidate(const RTCIceCandidate& iceCandidate)
{
    auto address = extractIPAddress(iceCandidate.candidate());
    if (!address.endsWithIgnoringASCIICase(".local"_s))
        return false;

    if (!WTF::isVersion4UUID(StringView { address }.substring(0, address.length() - 6))) {
        RELEASE_LOG_ERROR(WebRTC, "mDNS candidate is not a Version 4 UUID");
        return true;
    }
    return false;
}

void PeerConnectionBackend::addIceCandidate(RTCIceCandidate* iceCandidate, DOMPromiseDeferred<void>&& promise)
{
    ASSERT(!m_peerConnection.isClosed());

    if (!iceCandidate) {
        endOfIceCandidates(WTFMove(promise));
        return;
    }

    // FIXME: As per https://w3c.github.io/webrtc-pc/#dom-rtcpeerconnection-addicecandidate(), this check should be done before enqueuing the task.
    if (iceCandidate->sdpMid().isNull() && !iceCandidate->sdpMLineIndex()) {
        promise.reject(Exception { TypeError, "Trying to add a candidate that is missing both sdpMid and sdpMLineIndex"_s });
        return;
    }

    if (shouldIgnoreIceCandidate(*iceCandidate)) {
        promise.resolve();
        return;
    }

    doAddIceCandidate(*iceCandidate, [weakThis = makeWeakPtr(this), promise = WTFMove(promise)](auto&& result) mutable {
        ASSERT(isMainThread());
        if (!weakThis || weakThis->m_peerConnection.isClosed())
            return;

        if (result.hasException()) {
            RELEASE_LOG_ERROR(WebRTC, "Adding ice candidate failed %d", result.exception().code());
            promise.reject(result.releaseException());
            return;
        }

        if (auto descriptions = result.releaseReturnValue())
            weakThis->m_peerConnection.updateDescriptions(WTFMove(*descriptions));
        promise.resolve();
    });
}

void PeerConnectionBackend::fireICECandidateEvent(RefPtr<RTCIceCandidate>&& candidate, String&& serverURL)
{
    ASSERT(isMainThread());

    m_peerConnection.dispatchEventWhenFeasible(RTCPeerConnectionIceEvent::create(Event::CanBubble::No, Event::IsCancelable::No, WTFMove(candidate), WTFMove(serverURL)));
}

void PeerConnectionBackend::enableICECandidateFiltering()
{
    m_shouldFilterICECandidates = true;
}

void PeerConnectionBackend::disableICECandidateFiltering()
{
    m_shouldFilterICECandidates = false;
}

void PeerConnectionBackend::validateSDP(const String& sdp) const
{
#ifndef NDEBUG
    if (!m_shouldFilterICECandidates)
        return;
    sdp.split('\n', [](auto line) {
        ASSERT(!line.startsWith("a=candidate") || line.contains(".local"));
    });
#else
    UNUSED_PARAM(sdp);
#endif
}

void PeerConnectionBackend::newICECandidate(String&& sdp, String&& mid, unsigned short sdpMLineIndex, String&& serverURL, std::optional<DescriptionStates>&& descriptions)
{
    m_peerConnection.doTask([logSiteIdentifier = LOGIDENTIFIER, this, sdp = WTFMove(sdp), mid = WTFMove(mid), sdpMLineIndex, serverURL = WTFMove(serverURL), descriptions = WTFMove(descriptions)]() mutable {
        if (m_peerConnection.isClosed())
            return;

        if (descriptions)
            m_peerConnection.updateDescriptions(WTFMove(*descriptions));

        UNUSED_PARAM(logSiteIdentifier);
        ALWAYS_LOG(logSiteIdentifier, "Gathered ice candidate:", sdp);
        m_finishedGatheringCandidates = false;

        ASSERT(!m_shouldFilterICECandidates || sdp.contains(".local"));
        fireICECandidateEvent(RTCIceCandidate::create(WTFMove(sdp), WTFMove(mid), sdpMLineIndex), WTFMove(serverURL));
    });
}

void PeerConnectionBackend::doneGatheringCandidates()
{
    ASSERT(isMainThread());
    ALWAYS_LOG(LOGIDENTIFIER, "Finished ice candidate gathering");
    m_finishedGatheringCandidates = true;

    m_peerConnection.dispatchEventWhenFeasible(RTCPeerConnectionIceEvent::create(Event::CanBubble::No, Event::IsCancelable::No, nullptr, { }));
    m_peerConnection.updateIceGatheringState(RTCIceGatheringState::Complete);
}

void PeerConnectionBackend::endOfIceCandidates(DOMPromiseDeferred<void>&& promise)
{
    promise.resolve();
}

void PeerConnectionBackend::updateSignalingState(RTCSignalingState newSignalingState)
{
    ASSERT(isMainThread());

    if (newSignalingState != m_peerConnection.signalingState()) {
        m_peerConnection.setSignalingState(newSignalingState);
        m_peerConnection.dispatchEventWhenFeasible(Event::create(eventNames().signalingstatechangeEvent, Event::CanBubble::No, Event::IsCancelable::No));
    }
}

void PeerConnectionBackend::stop()
{
    m_offerAnswerPromise = nullptr;
    m_setDescriptionCallback = nullptr;

    m_pendingTrackEvents.clear();

    doStop();
}

void PeerConnectionBackend::markAsNeedingNegotiation(uint32_t eventId)
{
    m_peerConnection.updateNegotiationNeededFlag(eventId);
}

ExceptionOr<Ref<RTCRtpSender>> PeerConnectionBackend::addTrack(MediaStreamTrack&, Vector<String>&&)
{
    return Exception { NotSupportedError, "Not implemented"_s };
}

ExceptionOr<Ref<RTCRtpTransceiver>> PeerConnectionBackend::addTransceiver(const String&, const RTCRtpTransceiverInit&)
{
    return Exception { NotSupportedError, "Not implemented"_s };
}

ExceptionOr<Ref<RTCRtpTransceiver>> PeerConnectionBackend::addTransceiver(Ref<MediaStreamTrack>&&, const RTCRtpTransceiverInit&)
{
    return Exception { NotSupportedError, "Not implemented"_s };
}

void PeerConnectionBackend::generateCertificate(Document& document, const CertificateInformation& info, DOMPromiseDeferred<IDLInterface<RTCCertificate>>&& promise)
{
#if USE(LIBWEBRTC)
    auto* page = document.page();
    if (!page) {
        promise.reject(InvalidStateError);
        return;
    }
    LibWebRTCCertificateGenerator::generateCertificate(document.securityOrigin(), page->libWebRTCProvider(), info, WTFMove(promise));
#else
    UNUSED_PARAM(document);
    UNUSED_PARAM(expires);
    UNUSED_PARAM(type);
    promise.reject(NotSupportedError);
#endif
}

ScriptExecutionContext* PeerConnectionBackend::context() const
{
    return m_peerConnection.scriptExecutionContext();
}

#if !RELEASE_LOG_DISABLED
WTFLogChannel& PeerConnectionBackend::logChannel() const
{
    return LogWebRTC;
}
#endif

} // namespace WebCore

#endif // ENABLE(WEB_RTC)
