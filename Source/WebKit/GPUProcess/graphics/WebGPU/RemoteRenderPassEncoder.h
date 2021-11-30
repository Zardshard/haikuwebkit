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

#pragma once

#if ENABLE(GPU_PROCESS)

#include "StreamMessageReceiver.h"
#include "WebGPUColor.h"
#include "WebGPUIdentifier.h"
#include <pal/graphics/WebGPU/WebGPUIndexFormat.h>
#include <pal/graphics/WebGPU/WebGPUIntegralTypes.h>
#include <wtf/Ref.h>
#include <wtf/text/WTFString.h>

namespace PAL::WebGPU {
class RenderPassEncoder;
}

namespace WebKit {

namespace WebGPU {
class ObjectHeap;
class ObjectRegistry;
}

class RemoteRenderPassEncoder final : public IPC::StreamMessageReceiver {
public:
    static Ref<RemoteRenderPassEncoder> create(PAL::WebGPU::RenderPassEncoder& renderPassEncoder, WebGPU::ObjectRegistry& objectRegistry, WebGPU::ObjectHeap& objectHeap, WebGPUIdentifier identifier)
    {
        return adoptRef(*new RemoteRenderPassEncoder(renderPassEncoder, objectRegistry, objectHeap, identifier));
    }

    ~RemoteRenderPassEncoder();

private:
    friend class ObjectRegistry;

    RemoteRenderPassEncoder(PAL::WebGPU::RenderPassEncoder&, WebGPU::ObjectRegistry&, WebGPU::ObjectHeap&, WebGPUIdentifier);

    RemoteRenderPassEncoder(const RemoteRenderPassEncoder&) = delete;
    RemoteRenderPassEncoder(RemoteRenderPassEncoder&&) = delete;
    RemoteRenderPassEncoder& operator=(const RemoteRenderPassEncoder&) = delete;
    RemoteRenderPassEncoder& operator=(RemoteRenderPassEncoder&&) = delete;

    void didReceiveStreamMessage(IPC::StreamServerConnectionBase&, IPC::Decoder&) final;

    void setPipeline(WebGPUIdentifier);

    void setIndexBuffer(WebGPUIdentifier, PAL::WebGPU::IndexFormat, PAL::WebGPU::Size64 offset, std::optional<PAL::WebGPU::Size64>);
    void setVertexBuffer(PAL::WebGPU::Index32 slot, WebGPUIdentifier, PAL::WebGPU::Size64 offset, std::optional<PAL::WebGPU::Size64>);

    void draw(PAL::WebGPU::Size32 vertexCount, PAL::WebGPU::Size32 instanceCount,
        PAL::WebGPU::Size32 firstVertex, PAL::WebGPU::Size32 firstInstance);
    void drawIndexed(PAL::WebGPU::Size32 indexCount, PAL::WebGPU::Size32 instanceCount,
        PAL::WebGPU::Size32 firstIndex,
        PAL::WebGPU::SignedOffset32 baseVertex,
        PAL::WebGPU::Size32 firstInstance);

    void drawIndirect(WebGPUIdentifier indirectBuffer, PAL::WebGPU::Size64 indirectOffset);
    void drawIndexedIndirect(WebGPUIdentifier indirectBuffer, PAL::WebGPU::Size64 indirectOffset);

    void setBindGroup(PAL::WebGPU::Index32, WebGPUIdentifier,
        std::optional<Vector<PAL::WebGPU::BufferDynamicOffset>>&& dynamicOffsets);

    void pushDebugGroup(String&& groupLabel);
    void popDebugGroup();
    void insertDebugMarker(String&& markerLabel);

    void setViewport(float x, float y,
        float width, float height,
        float minDepth, float maxDepth);

    void setScissorRect(PAL::WebGPU::IntegerCoordinate x, PAL::WebGPU::IntegerCoordinate y,
        PAL::WebGPU::IntegerCoordinate width, PAL::WebGPU::IntegerCoordinate height);

    void setBlendConstant(WebGPU::Color);
    void setStencilReference(PAL::WebGPU::StencilValue);

    void beginOcclusionQuery(PAL::WebGPU::Size32 queryIndex);
    void endOcclusionQuery();

    void beginPipelineStatisticsQuery(WebGPUIdentifier, PAL::WebGPU::Size32 queryIndex);
    void endPipelineStatisticsQuery();

    void executeBundles(Vector<WebGPUIdentifier>&&);
    void endPass();

    void setLabel(String&&);

    Ref<PAL::WebGPU::RenderPassEncoder> m_backing;
    WebGPU::ObjectRegistry& m_objectRegistry;
    WebGPU::ObjectHeap& m_objectHeap;
    WebGPUIdentifier m_identifier;
};

} // namespace WebKit

#endif // ENABLE(GPU_PROCESS)
