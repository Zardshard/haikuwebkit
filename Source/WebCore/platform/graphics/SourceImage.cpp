/*
 * Copyright (C) 2022 Apple Inc.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "SourceImage.h"

#include "GraphicsContext.h"

namespace WebCore {

SourceImage::SourceImage(ImageVariant&& imageVariant)
    : m_imageVariant(WTFMove(imageVariant))
{
}

NativeImage* SourceImage::nativeImageIfExists() const
{
    if (auto* nativeImage = std::get_if<Ref<NativeImage>>(&m_imageVariant))
        return nativeImage->ptr();
    return nullptr;
}

NativeImage* SourceImage::nativeImage()
{
    if (!std::holds_alternative<Ref<ImageBuffer>>(m_imageVariant))
        return nativeImageIfExists();

    auto imageBuffer = std::get<Ref<ImageBuffer>>(m_imageVariant);
    auto nativeImage = ImageBuffer::sinkIntoNativeImage(WTFMove(imageBuffer));
    if (!nativeImage)
        return nullptr;

    m_imageVariant = nativeImage.releaseNonNull();
    return nativeImageIfExists();
}

ImageBuffer* SourceImage::imageBufferIfExists() const
{
    if (auto* imageBuffer = std::get_if<Ref<ImageBuffer>>(&m_imageVariant))
        return imageBuffer->ptr();
    return nullptr;
}

ImageBuffer* SourceImage::imageBuffer()
{
    if (!std::holds_alternative<Ref<NativeImage>>(m_imageVariant))
        return imageBufferIfExists();

    auto nativeImage = std::get<Ref<NativeImage>>(m_imageVariant);
    auto rect = FloatRect { { }, nativeImage->size() };

    auto imageBuffer = ImageBuffer::create(nativeImage->size(), RenderingMode::Unaccelerated, 1, DestinationColorSpace::SRGB(), PixelFormat::BGRA8);
    if (!imageBuffer)
        return nullptr;

    imageBuffer->context().drawNativeImage(nativeImage, rect.size(), rect, rect);

    m_imageVariant = imageBuffer.releaseNonNull();
    return imageBufferIfExists();
}

RenderingResourceIdentifier SourceImage::imageIdentifier() const
{
    return WTF::switchOn(m_imageVariant,
        [&] (const Ref<NativeImage>& nativeImage) {
            return nativeImage->renderingResourceIdentifier();
        },
        [&] (const Ref<ImageBuffer>& imageBuffer) {
            return imageBuffer->renderingResourceIdentifier();
        },
        [&] (RenderingResourceIdentifier renderingResourceIdentifier) {
            return renderingResourceIdentifier;
        }
    );
}

} // namespace WebCore
