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

#include "config.h"
#include "RemoteTexture.h"

#if ENABLE(GPU_PROCESS)

#include "RemoteTextureView.h"
#include "WebGPUObjectHeap.h"
#include "WebGPUObjectRegistry.h"
#include "WebGPUTextureViewDescriptor.h"
#include <pal/graphics/WebGPU/WebGPUTexture.h>
#include <pal/graphics/WebGPU/WebGPUTextureView.h>
#include <pal/graphics/WebGPU/WebGPUTextureViewDescriptor.h>

namespace WebKit {

RemoteTexture::RemoteTexture(PAL::WebGPU::Texture& texture, WebGPU::ObjectRegistry& objectRegistry, WebGPU::ObjectHeap& objectHeap, WebGPUIdentifier identifier)
    : m_backing(texture)
    , m_objectRegistry(objectRegistry)
    , m_objectHeap(objectHeap)
    , m_identifier(identifier)
{
    m_objectRegistry.addObject(m_identifier, m_backing);
}

RemoteTexture::~RemoteTexture()
{
    m_objectRegistry.removeObject(m_identifier);
}

void RemoteTexture::createView(const std::optional<WebGPU::TextureViewDescriptor>& descriptor, WebGPUIdentifier identifier)
{
    std::optional<PAL::WebGPU::TextureViewDescriptor> convertedDescriptor;
    if (descriptor) {
        auto resultDescriptor = m_objectRegistry.convertFromBacking(*descriptor);
        ASSERT(resultDescriptor);
        convertedDescriptor = WTFMove(resultDescriptor);
        if (!convertedDescriptor)
            return;
    }
    ASSERT(convertedDescriptor);
    auto textureView = m_backing->createView(*convertedDescriptor);
    auto remoteTextureView = RemoteTextureView::create(textureView, m_objectRegistry, m_objectHeap, identifier);
    m_objectHeap.addObject(remoteTextureView);
}

void RemoteTexture::destroy()
{
    m_backing->destroy();
}

void RemoteTexture::setLabel(String&& label)
{
    m_backing->setLabel(WTFMove(label));
}

} // namespace WebKit

#endif // ENABLE(GPU_PROCESS)
