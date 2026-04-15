/**
 * MIT License
 *
 * Copyright (c) 2026 Mag1c.H
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 * */
#ifndef COPY_INITIATOR_GDR_CUDA_H
#define COPY_INITIATOR_GDR_CUDA_H

#include "copy_initiator.h"
#include "gdr_context_cuda.h"
#include <memory>
#include <vector>

class GdrH2DInitiator : public CopyInitiator {
    size_t device_;
    size_t number_;
    std::unique_ptr<GdrContext> gdr_ctx_;
    std::vector<GdrContext::HostMapping*> src_mappings_;
    std::vector<GdrContext::GpuMapping*> dst_mappings_;
    bool initialized_{false};

public:
    GdrH2DInitiator(size_t device, size_t number) : CopyInitiator{}, device_{device}, number_{number}
    {
        gdr_ctx_ = std::make_unique<GdrContext>(device);
        src_mappings_.resize(number, nullptr);
        dst_mappings_.resize(number, nullptr);
    }
    ~GdrH2DInitiator() override
    {
        for (auto* m : src_mappings_) {
            if (m) gdr_ctx_->DeregisterHostMemory(m);
        }
        for (auto* m : dst_mappings_) {
            if (m) gdr_ctx_->UnmapGpuMemory(m);
        }
    }
    std::string Name() const override { return "GDR"; }

    void Initialize(void* const* src, void* const* dst, size_t size, size_t number)
    {
        if (initialized_) return;
        for (size_t i = 0; i < number; ++i) {
            src_mappings_[i] = gdr_ctx_->RegisterHostMemory(src[i], size);
            dst_mappings_[i] = gdr_ctx_->MapGpuMemory(dst[i], size);
        }
        initialized_ = true;
    }

    void Copy(void* const* src, void* const* dst, size_t size, size_t number, void* args) const override
    {
        if (!initialized_) {
            const_cast<GdrH2DInitiator*>(this)->Initialize(src, dst, size, number);
        }

        std::vector<GdrContext::HostMapping*> srcs(number);
        std::vector<GdrContext::GpuMapping*> dsts(number);
        for (size_t i = 0; i < number; ++i) {
            srcs[i] = src_mappings_[i];
            dsts[i] = dst_mappings_[i];
        }

        gdr_ctx_->PostRdmaWriteBatch(srcs.data(), dsts.data(), size, number);
        gdr_ctx_->WaitCompletion(number);
    }
};

#endif  // COPY_INITIATOR_GDR_CUDA_H