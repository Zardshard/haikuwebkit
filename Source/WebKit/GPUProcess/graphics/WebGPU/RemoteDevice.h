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
#include "WebGPUError.h"
#include "WebGPUIdentifier.h"
#include <pal/graphics/WebGPU/WebGPUErrorFilter.h>
#include <wtf/CompletionHandler.h>
#include <wtf/Ref.h>
#include <wtf/text/WTFString.h>

namespace PAL::WebGPU {
class Device;
}

namespace WebKit {

namespace WebGPU {
struct BindGroupDescriptor;
struct BindGroupLayoutDescriptor;
struct BufferDescriptor;
struct CommandEncoderDescriptor;
struct ComputePipelineDescriptor;
struct ExternalTextureDescriptor;
class ObjectHeap;
class ObjectRegistry;
struct PipelineLayoutDescriptor;
struct QuerySetDescriptor;
struct RenderBundleEncoderDescriptor;
struct RenderPipelineDescriptor;
struct SamplerDescriptor;
struct ShaderModuleDescriptor;
struct TextureDescriptor;
}

class RemoteDevice final : public IPC::StreamMessageReceiver {
public:
    static Ref<RemoteDevice> create(PAL::WebGPU::Device& device, WebGPU::ObjectRegistry& objectRegistry, WebGPU::ObjectHeap& objectHeap, WebGPUIdentifier identifier)
    {
        return adoptRef(*new RemoteDevice(device, objectRegistry, objectHeap, identifier));
    }

    ~RemoteDevice();

private:
    friend class ObjectRegistry;

    RemoteDevice(PAL::WebGPU::Device&, WebGPU::ObjectRegistry&, WebGPU::ObjectHeap&, WebGPUIdentifier);

    RemoteDevice(const RemoteDevice&) = delete;
    RemoteDevice(RemoteDevice&&) = delete;
    RemoteDevice& operator=(const RemoteDevice&) = delete;
    RemoteDevice& operator=(RemoteDevice&&) = delete;

    void didReceiveStreamMessage(IPC::StreamServerConnectionBase&, IPC::Decoder&) final;

    void destroy();

    void createBuffer(const WebGPU::BufferDescriptor&, WebGPUIdentifier);
    void createTexture(const WebGPU::TextureDescriptor&, WebGPUIdentifier);
    void createSampler(const WebGPU::SamplerDescriptor&, WebGPUIdentifier);
    void importExternalTexture(const WebGPU::ExternalTextureDescriptor&, WebGPUIdentifier);

    void createBindGroupLayout(const WebGPU::BindGroupLayoutDescriptor&, WebGPUIdentifier);
    void createPipelineLayout(const WebGPU::PipelineLayoutDescriptor&, WebGPUIdentifier);
    void createBindGroup(const WebGPU::BindGroupDescriptor&, WebGPUIdentifier);

    void createShaderModule(const WebGPU::ShaderModuleDescriptor&, WebGPUIdentifier);
    void createComputePipeline(const WebGPU::ComputePipelineDescriptor&, WebGPUIdentifier);
    void createRenderPipeline(const WebGPU::RenderPipelineDescriptor&, WebGPUIdentifier);
    void createComputePipelineAsync(const WebGPU::ComputePipelineDescriptor&, WebGPUIdentifier, WTF::CompletionHandler<void()>&&);
    void createRenderPipelineAsync(const WebGPU::RenderPipelineDescriptor&, WebGPUIdentifier, WTF::CompletionHandler<void()>&&);

    void createCommandEncoder(const std::optional<WebGPU::CommandEncoderDescriptor>&, WebGPUIdentifier);
    void createRenderBundleEncoder(const WebGPU::RenderBundleEncoderDescriptor&, WebGPUIdentifier);

    void createQuerySet(const WebGPU::QuerySetDescriptor&, WebGPUIdentifier);

    void pushErrorScope(PAL::WebGPU::ErrorFilter);
    void popErrorScope(WTF::CompletionHandler<void(std::optional<WebGPU::Error>&&)>&&);

    void setLabel(String&&);

    Ref<PAL::WebGPU::Device> m_backing;
    WebGPU::ObjectRegistry& m_objectRegistry;
    WebGPU::ObjectHeap& m_objectHeap;
    WebGPUIdentifier m_identifier;
};

} // namespace WebKit

#endif // ENABLE(GPU_PROCESS)
