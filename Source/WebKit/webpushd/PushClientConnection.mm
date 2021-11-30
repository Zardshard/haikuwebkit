/*
 * Copyright (C) 2021 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#import "config.h"
#import "PushClientConnection.h"

#import "CodeSigning.h"
#import "WebPushDaemon.h"
#import <JavaScriptCore/ConsoleTypes.h>
#import <wtf/Vector.h>
#import <wtf/cocoa/Entitlements.h>

namespace WebPushD {

ClientConnection::ClientConnection(xpc_connection_t connection)
    : m_xpcConnection(connection)
{
}

void ClientConnection::setHostAppAuditTokenData(const Vector<uint8_t>& tokenData)
{
    RELEASE_ASSERT(!hasHostAppAuditToken());

    audit_token_t token;
    if (tokenData.size() != sizeof(token)) {
        ASSERT_WITH_MESSAGE(false, "Attempt to set an audit token from incorrect number of bytes");
        return;
    }

    memcpy(&token, tokenData.data(), tokenData.size());
    m_hostAppAuditToken = WTFMove(token);
}

const String& ClientConnection::hostAppCodeSigningIdentifier()
{
    if (!m_hostAppCodeSigningIdentifier) {
        if (!m_hostAppAuditToken)
            m_hostAppCodeSigningIdentifier = String();
        else
            m_hostAppCodeSigningIdentifier = WebKit::codeSigningIdentifier(*m_hostAppAuditToken);
    }

    return *m_hostAppCodeSigningIdentifier;
}

bool ClientConnection::hostAppHasPushEntitlement()
{
    if (!m_hostAppHasPushEntitlement) {
        if (!m_hostAppAuditToken)
            return false;
        m_hostAppHasPushEntitlement = WTF::hasEntitlement(*m_hostAppAuditToken, "com.apple.private.webkit.webpush");
    }

    return *m_hostAppHasPushEntitlement;
}

void ClientConnection::setDebugModeIsEnabled(bool enabled)
{
    if (enabled == m_debugModeEnabled)
        return;

    m_debugModeEnabled = enabled;

    auto identifier = hostAppCodeSigningIdentifier();
    String message;
    if (!identifier.isEmpty())
        message = makeString("[webpushd - ", identifier, "] Turned Debug Mode ", m_debugModeEnabled ? "on" : "off");
    else
        message = makeString("[webpushd] Turned Debug Mode ", m_debugModeEnabled ? "on" : "off");

    Daemon::singleton().broadcastDebugMessage(MessageLevel::Info, message);
}

} // namespace WebPushD
