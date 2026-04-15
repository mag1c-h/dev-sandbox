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
#ifndef GDR_CONTEXT_CUDA_H
#define GDR_CONTEXT_CUDA_H

#include <chrono>
#include <cstdint>
#include <infiniband/verbs.h>
#include <list>
#include <mutex>
#include <unordered_map>
#include <gdrapi.h>

class GdrContext {
public:
    struct GpuMapping {
        void* gpu_va;
        void* bar_map;
        uint64_t bar_pa;
        size_t size;
        gdr_mh_t gdr_mh;
        ibv_mr* gpu_mr;
        size_t ref_count;
        std::chrono::steady_clock::time_point last_used;
    };
    struct HostMapping {
        void* host_va;
        size_t size;
        ibv_mr* host_mr;
        size_t ref_count;
        std::chrono::steady_clock::time_point last_used;
    };

    GdrContext(size_t device_id);
    ~GdrContext();
    GdrContext(const GdrContext&) = delete;
    GdrContext& operator=(const GdrContext&) = delete;

    GpuMapping* MapGpuMemory(void* gpu_ptr, size_t size);
    void UnmapGpuMemory(GpuMapping* mapping);
    HostMapping* RegisterHostMemory(void* host_ptr, size_t size);
    void DeregisterHostMemory(HostMapping* mapping);

    void PostRdmaWrite(HostMapping* src, GpuMapping* dst, size_t size);
    void PostRdmaWriteBatch(HostMapping** srcs, GpuMapping** dsts, size_t size, size_t count);
    void WaitCompletion(size_t count);

private:
    void InitRdma();
    void SetupSelfConnection();
    void EvictLruMappings();
    void EvictLruHostMappings();

    size_t device_id_;
    ibv_context* ib_ctx_{nullptr};
    ibv_pd* pd_{nullptr};
    ibv_qp* qp_{nullptr};
    ibv_cq* cq_{nullptr};
    ibv_device** dev_list_{nullptr};
    gdr_t gdr_handle_{nullptr};

    std::mutex gpu_map_mutex_;
    std::unordered_map<void*, GpuMapping*> gpu_mappings_;
    size_t total_gpu_mapped_size_{0};
    static constexpr size_t MAX_BAR_SIZE = 128 * 1024 * 1024;

    std::mutex host_map_mutex_;
    std::unordered_map<void*, HostMapping*> host_mappings_;
    size_t total_host_registered_size_{0};
    static constexpr size_t MAX_HOST_REG_SIZE = 512 * 1024 * 1024;

    std::list<ibv_send_wr*> pending_wrs_;
};

#endif  // GDR_CONTEXT_CUDA_H