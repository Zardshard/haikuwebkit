/*
 * Copyright (C) 2022 Apple Inc. All rights reserved.
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

#pragma once

#if ENABLE(GPU_PROCESS) && ENABLE(MEDIA_STREAM)

#include "GPUProcessConnection.h"
#include "RemoteVideoFrameIdentifier.h"
#include <WebCore/VideoFrame.h>
#include <wtf/Function.h>
#include <wtf/threads/BinarySemaphore.h>

namespace IPC {
class Connection;
class Decoder;
}

namespace WebKit {

class GPUProcessConnection;

// A WebCore::VideoFrame class that points to a concrete WebCore::VideoFrame instance
// in another process, GPU process.
class RemoteVideoFrameProxy final : public WebCore::VideoFrame {
    WTF_MAKE_FAST_ALLOCATED;
    WTF_MAKE_NONCOPYABLE(RemoteVideoFrameProxy);
public:
    struct Properties {
        // The receiver owns the reference, so it must be released via either adoption to
        // `RemoteVideoFrameProxy::create()` or via `RemoteVideoFrameProxy::releaseUnused()`.
        WebKit::RemoteVideoFrameReference reference;
        MediaTime presentationTime;
        bool isMirrored { false };
        VideoRotation rotation { VideoRotation::None };
        WebCore::IntSize size;
        uint32_t pixelFormat { 0 };

        template<typename Encoder> void encode(Encoder&) const;
        template<typename Decoder> static std::optional<Properties> decode(Decoder&);
    };

    static Properties properties(WebKit::RemoteVideoFrameReference&&, const WebCore::MediaSample&);

#if PLATFORM(COCOA)
    using PixelBufferResultCallback = Function<void(RetainPtr<CVPixelBufferRef>&&)>;
#else
    using PixelBufferResultCallback = Function<void()>;
#endif
    // PixelBufferCallback should always complete but it might not be called on the same thread it was created.
    using PixelBufferCallback = Function<void(const RemoteVideoFrameProxy&, PixelBufferResultCallback&&)>;

    static Ref<RemoteVideoFrameProxy> create(IPC::Connection&, Properties, PixelBufferCallback&&);

    // Called by the end-points that capture creation messages that are sent from GPUP but
    // whose destinations were released in WP before message was processed.
    static void releaseUnused(IPC::Connection&, Properties&&);

    ~RemoteVideoFrameProxy() final;

    RemoteVideoFrameIdentifier identifier() const;
    RemoteVideoFrameWriteReference write() const;
    RemoteVideoFrameReadReference read() const;

    WebCore::IntSize size() const;

#if PLATFORM(COCOA)
    CVPixelBufferRef pixelBuffer() const final;
#endif

private:
    RemoteVideoFrameProxy(IPC::Connection&, Properties, PixelBufferCallback&&);

    // WebCore::VideoFrame overrides.
    MediaTime presentationTime() const final;
    VideoRotation videoRotation() const final;
    bool videoMirrored() const final;
    WebCore::FloatSize presentationSize() const final { return m_size; }
    std::optional<WebCore::MediaSampleVideoFrame> videoFrame() const final;
    uint32_t videoPixelFormat() const final;
    // FIXME: When VideoFrame is not MediaSample, these will not be needed.
    WebCore::PlatformSample platformSample() const final;

#if PLATFORM(COCOA)
    void getPixelBuffer();
#endif

    const Ref<IPC::Connection> m_connection;
    RemoteVideoFrameReferenceTracker m_referenceTracker;
    const MediaTime m_presentationTime;
    const bool m_isMirrored;
    const VideoRotation m_rotation;
    const WebCore::IntSize m_size;
    uint32_t m_pixelFormat { 0 };
    mutable Lock m_pixelBufferLock;
    RetainPtr<CVPixelBufferRef> m_pixelBuffer;
    BinarySemaphore m_semaphore;
    PixelBufferCallback m_pixelBufferCallback;
};

template<typename Encoder> void RemoteVideoFrameProxy::Properties::encode(Encoder& encoder) const
{
    encoder << presentationTime
        << isMirrored
        << rotation
        << size
        << pixelFormat;
}

template<typename Decoder> std::optional<RemoteVideoFrameProxy::Properties> RemoteVideoFrameProxy::Properties::decode(Decoder& decoder)
{
    std::optional<RemoteVideoFrameReference> reference;
    std::optional<MediaTime> presentationTime;
    std::optional<bool> isMirrored;
    std::optional<VideoRotation> videoRotation;
    std::optional<WebCore::IntSize> size;
    std::optional<uint32_t> pixelFormat;
    decoder >> presentationTime
        >> isMirrored
        >> videoRotation
        >> size
        >> pixelFormat;
    if (!decoder.isValid())
        return std::nullopt;
    return Properties { WTFMove(*reference), WTFMove(*presentationTime), *isMirrored, *videoRotation, *size, *pixelFormat };
}

}
SPECIALIZE_TYPE_TRAITS_BEGIN(WebKit::RemoteVideoFrameProxy)
    static bool isType(const WebCore::MediaSample& mediaSample) { return mediaSample.platformSample().type == WebCore::PlatformSample::RemoteVideoFrameProxyType; }
SPECIALIZE_TYPE_TRAITS_END()
SPECIALIZE_TYPE_TRAITS_BEGIN(WebCore::VideoFrame)
    static bool isType(const WebCore::MediaSample& mediaSample) { return mediaSample.platformSample().type == WebCore::PlatformSample::RemoteVideoFrameProxyType; }
SPECIALIZE_TYPE_TRAITS_END()
#endif
