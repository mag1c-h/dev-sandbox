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
#include "gdr_context_cuda.h"
#include <cuda.h>
#include <fmt/format.h>
#include <cstdlib>
#include <cstring>

#define GDR_ASSERT(expr)                                                                             \
    do {                                                                                             \
        if (!(expr)) {                                                                               \
            fmt::println(stderr, "GDR assertion failed: {} at {}:{}", #expr, __FILE__, __LINE__);   \
            exit(EXIT_FAILURE);                                                                      \
        }                                                                                            \
    } while (0)

#define IBV_ASSERT(expr)                                                                             \
    do {                                                                                             \
        if ((expr) != 0) {                                                                           \
            fmt::println(stderr, "ibverbs error: {} at {}:{}", #expr, __FILE__, __LINE__);          \
            exit(EXIT_FAILURE);                                                                      \
        }                                                                                            \
    } while (0)

GdrContext::GdrContext(size_t device_id) : device_id_{device_id}
{
    InitRdma();
    gdr_handle_ = gdr_open();
    GDR_ASSERT(gdr_handle_ != nullptr);
}

GdrContext::~GdrContext()
{
    {
        std::lock_guard<std::mutex> lock(host_map_mutex_);
        for (auto& pair : host_mappings_) {
            ibv_dereg_mr(pair.second->host_mr);
            delete pair.second;
        }
        host_mappings_.clear();
    }
    {
        std::lock_guard<std::mutex> lock(gpu_map_mutex_);
        for (auto& pair : gpu_mappings_) {
            ibv_dereg_mr(pair.second->gpu_mr);
            gdr_unmap(gdr_handle_, pair.second->gdr_mh, pair.second->bar_map, pair.second->size);
            gdr_unpin_buffer(gdr_handle_, pair.second->gdr_mh);
            delete pair.second;
        }
        gpu_mappings_.clear();
    }
    if (qp_) { ibv_destroy_qp(qp_); }
    if (cq_) { ibv_destroy_cq(cq_); }
    if (pd_) { ibv_dealloc_pd(pd_); }
    if (ib_ctx_) { ibv_close_device(ib_ctx_); }
    if (dev_list_) { ibv_free_device_list(dev_list_); }
    if (gdr_handle_) { gdr_close(gdr_handle_); }
}

void GdrContext::InitRdma()
{
    dev_list_ = ibv_get_device_list(nullptr);
    GDR_ASSERT(dev_list_ != nullptr && dev_list_[0] != nullptr);

    ib_ctx_ = ibv_open_device(dev_list_[0]);
    GDR_ASSERT(ib_ctx_ != nullptr);

    pd_ = ibv_alloc_pd(ib_ctx_);
    GDR_ASSERT(pd_ != nullptr);

    cq_ = ibv_create_cq(ib_ctx_, 64, nullptr, nullptr, 0);
    GDR_ASSERT(cq_ != nullptr);

    ibv_qp_init_attr qp_init_attr;
    std::memset(&qp_init_attr, 0, sizeof(qp_init_attr));
    qp_init_attr.send_cq = cq_;
    qp_init_attr.recv_cq = cq_;
    qp_init_attr.cap.max_send_wr = 64;
    qp_init_attr.cap.max_recv_wr = 1;
    qp_init_attr.cap.max_send_sge = 1;
    qp_init_attr.cap.max_recv_sge = 1;
    qp_init_attr.qp_type = IBV_QPT_RC;

    qp_ = ibv_create_qp(pd_, &qp_init_attr);
    GDR_ASSERT(qp_ != nullptr);

    SetupSelfConnection();
}

void GdrContext::SetupSelfConnection()
{
    ibv_port_attr port_attr;
    IBV_ASSERT(ibv_query_port(ib_ctx_, 1, &port_attr));

    uint16_t lid = port_attr.lid;
    uint32_t qpn = qp_->qp_num;

    ibv_qp_attr attr;
    std::memset(&attr, 0, sizeof(attr));
    attr.qp_state = IBV_QPS_INIT;
    attr.pkey_index = 0;
    attr.port_num = 1;
    attr.qp_access_flags = IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_LOCAL_WRITE;
    IBV_ASSERT(ibv_modify_qp(qp_, &attr,
                             IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS));

    std::memset(&attr, 0, sizeof(attr));
    attr.qp_state = IBV_QPS_RTR;
    attr.dest_qp_num = qpn;
    attr.rq_psn = 0;
    attr.path_mtu = port_attr.active_mtu;
    attr.ah_attr.dlid = lid;
    attr.ah_attr.sl = 0;
    attr.ah_attr.src_path_bits = 0;
    attr.ah_attr.port_num = 1;
    attr.max_dest_rd_atomic = 1;
    attr.min_rnr_timer = 12;
    IBV_ASSERT(ibv_modify_qp(qp_, &attr,
                             IBV_QP_STATE | IBV_QP_AV | IBV_QP_DEST_QPN | IBV_QP_RQ_PSN |
                                 IBV_QP_PATH_MTU | IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER));

    std::memset(&attr, 0, sizeof(attr));
    attr.qp_state = IBV_QPS_RTS;
    attr.sq_psn = 0;
    attr.max_rd_atomic = 1;
    attr.retry_cnt = 7;
    attr.rnr_retry = 7;
    IBV_ASSERT(ibv_modify_qp(qp_, &attr,
                             IBV_QP_STATE | IBV_QP_SQ_PSN | IBV_QP_MAX_RD_ATOMIC |
                                 IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY));
}

GdrContext::GpuMapping* GdrContext::MapGpuMemory(void* gpu_ptr, size_t size)
{
    std::lock_guard<std::mutex> lock(gpu_map_mutex_);

    auto it = gpu_mappings_.find(gpu_ptr);
    if (it != gpu_mappings_.end()) {
        it->second->ref_count++;
        it->second->last_used = std::chrono::steady_clock::now();
        return it->second;
    }

    if (total_gpu_mapped_size_ + size > MAX_BAR_SIZE) {
        EvictLruMappings();
        if (total_gpu_mapped_size_ + size > MAX_BAR_SIZE) {
            fmt::println(stderr, "BAR space exhausted, cannot map {} bytes", size);
            return nullptr;
        }
    }

    unsigned int flag = 1;
    CUresult cu_result = cuPointerSetAttribute(&flag, CU_POINTER_ATTRIBUTE_SYNC_MEMOPS, gpu_ptr);
    if (cu_result != CUDA_SUCCESS) {
        fmt::println(stderr, "cuPointerSetAttribute failed for GPU ptr");
        return nullptr;
    }

    auto* mapping = new GpuMapping();
    mapping->gpu_va = gpu_ptr;
    mapping->size = size;
    mapping->ref_count = 1;
    mapping->last_used = std::chrono::steady_clock::now();

    int gdr_result = gdr_pin_buffer(gdr_handle_, gpu_ptr, size, 0, 0, &mapping->gdr_mh);
    if (gdr_result != 0) {
        fmt::println(stderr, "gdr_pin_buffer failed: {}", gdr_result);
        delete mapping;
        return nullptr;
    }

    gdr_info_t info;
    std::memset(&info, 0, sizeof(info));
    gdr_get_info(gdr_handle_, mapping->gdr_mh, &info);
    mapping->bar_pa = info.pa;

    void* bar_map = nullptr;
    gdr_result = gdr_map(gdr_handle_, mapping->gdr_mh, &bar_map, size);
    if (gdr_result != 0) {
        fmt::println(stderr, "gdr_map failed: {}", gdr_result);
        gdr_unpin_buffer(gdr_handle_, mapping->gdr_mh);
        delete mapping;
        return nullptr;
    }
    mapping->bar_map = bar_map;

    mapping->gpu_mr = ibv_reg_mr(pd_, bar_map, size,
                                 IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE);
    if (!mapping->gpu_mr) {
        fmt::println(stderr, "ibv_reg_mr for GPU BAR failed");
        gdr_unmap(gdr_handle_, mapping->gdr_mh, bar_map, size);
        gdr_unpin_buffer(gdr_handle_, mapping->gdr_mh);
        delete mapping;
        return nullptr;
    }

    gpu_mappings_[gpu_ptr] = mapping;
    total_gpu_mapped_size_ += size;
    return mapping;
}

void GdrContext::UnmapGpuMemory(GpuMapping* mapping)
{
    std::lock_guard<std::mutex> lock(gpu_map_mutex_);
    if (--mapping->ref_count == 0) {
        auto it = gpu_mappings_.find(mapping->gpu_va);
        if (it != gpu_mappings_.end()) {
            gpu_mappings_.erase(it);
            ibv_dereg_mr(mapping->gpu_mr);
            gdr_unmap(gdr_handle_, mapping->gdr_mh, mapping->bar_map, mapping->size);
            gdr_unpin_buffer(gdr_handle_, mapping->gdr_mh);
            total_gpu_mapped_size_ -= mapping->size;
            delete mapping;
        }
    }
}

GdrContext::HostMapping* GdrContext::RegisterHostMemory(void* host_ptr, size_t size)
{
    std::lock_guard<std::mutex> lock(host_map_mutex_);

    auto it = host_mappings_.find(host_ptr);
    if (it != host_mappings_.end()) {
        it->second->ref_count++;
        it->second->last_used = std::chrono::steady_clock::now();
        return it->second;
    }

    if (total_host_registered_size_ + size > MAX_HOST_REG_SIZE) {
        EvictLruHostMappings();
    }

    auto* mapping = new HostMapping();
    mapping->host_va = host_ptr;
    mapping->size = size;
    mapping->ref_count = 1;
    mapping->last_used = std::chrono::steady_clock::now();

    mapping->host_mr = ibv_reg_mr(pd_, host_ptr, size, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE);
    if (!mapping->host_mr) {
        fmt::println(stderr, "ibv_reg_mr for host memory failed");
        delete mapping;
        return nullptr;
    }

    host_mappings_[host_ptr] = mapping;
    total_host_registered_size_ += size;
    return mapping;
}

void GdrContext::DeregisterHostMemory(HostMapping* mapping)
{
    std::lock_guard<std::mutex> lock(host_map_mutex_);
    if (--mapping->ref_count == 0) {
        auto it = host_mappings_.find(mapping->host_va);
        if (it != host_mappings_.end()) {
            host_mappings_.erase(it);
            ibv_dereg_mr(mapping->host_mr);
            total_host_registered_size_ -= mapping->size;
            delete mapping;
        }
    }
}

void GdrContext::PostRdmaWrite(HostMapping* src, GpuMapping* dst, size_t size)
{
    ibv_send_wr wr, *bad_wr = nullptr;
    ibv_sge sge;

    std::memset(&wr, 0, sizeof(wr));
    std::memset(&sge, 0, sizeof(sge));

    sge.addr = reinterpret_cast<uint64_t>(src->host_va);
    sge.length = size;
    sge.lkey = src->host_mr->lkey;

    wr.opcode = IBV_WR_RDMA_WRITE;
    wr.sg_list = &sge;
    wr.num_sge = 1;
    wr.send_flags = IBV_SEND_SIGNALED;
    wr.rdma.remote_addr = dst->bar_pa;
    wr.rdma.rkey = dst->gpu_mr->rkey;

    IBV_ASSERT(ibv_post_send(qp_, &wr, &bad_wr));
}

void GdrContext::PostRdmaWriteBatch(HostMapping** srcs, GpuMapping** dsts, size_t size, size_t count)
{
    std::vector<ibv_send_wr> wrs(count);
    std::vector<ibv_sge> sges(count);
    ibv_send_wr* bad_wr = nullptr;

    for (size_t i = 0; i < count; ++i) {
        std::memset(&wrs[i], 0, sizeof(wrs[i]));
        std::memset(&sges[i], 0, sizeof(sges[i]));

        sges[i].addr = reinterpret_cast<uint64_t>(srcs[i]->host_va);
        sges[i].length = size;
        sges[i].lkey = srcs[i]->host_mr->lkey;

        wrs[i].opcode = IBV_WR_RDMA_WRITE;
        wrs[i].sg_list = &sges[i];
        wrs[i].num_sge = 1;
        wrs[i].send_flags = (i == count - 1) ? IBV_SEND_SIGNALED : 0;
        wrs[i].rdma.remote_addr = dsts[i]->bar_pa;
        wrs[i].rdma.rkey = dsts[i]->gpu_mr->rkey;
        wrs[i].next = (i < count - 1) ? &wrs[i + 1] : nullptr;
    }

    IBV_ASSERT(ibv_post_send(qp_, &wrs[0], &bad_wr));
}

void GdrContext::WaitCompletion(size_t count)
{
    ibv_wc wc;
    size_t completed = 0;
    while (completed < count) {
        int n = ibv_poll_cq(cq_, 1, &wc);
        if (n > 0) {
            if (wc.status != IBV_WC_SUCCESS) {
                fmt::println(stderr, "RDMA completion error: {}", ibv_wc_status_str(wc.status));
                exit(EXIT_FAILURE);
            }
            completed += n;
        }
    }
}

void GdrContext::EvictLruMappings()
{
    while (total_gpu_mapped_size_ > MAX_BAR_SIZE / 2 && !gpu_mappings_.empty()) {
        auto oldest = gpu_mappings_.begin();
        for (auto it = gpu_mappings_.begin(); it != gpu_mappings_.end(); ++it) {
            if (it->second->last_used < oldest->second->last_used && it->second->ref_count == 0) {
                oldest = it;
            }
        }
        if (oldest->second->ref_count > 0) break;

        auto* mapping = oldest->second;
        gpu_mappings_.erase(oldest);
        ibv_dereg_mr(mapping->gpu_mr);
        gdr_unmap(gdr_handle_, mapping->gdr_mh, mapping->bar_map, mapping->size);
        gdr_unpin_buffer(gdr_handle_, mapping->gdr_mh);
        total_gpu_mapped_size_ -= mapping->size;
        delete mapping;
    }
}

void GdrContext::EvictLruHostMappings()
{
    while (total_host_registered_size_ > MAX_HOST_REG_SIZE / 2 && !host_mappings_.empty()) {
        auto oldest = host_mappings_.begin();
        for (auto it = host_mappings_.begin(); it != host_mappings_.end(); ++it) {
            if (it->second->last_used < oldest->second->last_used && it->second->ref_count == 0) {
                oldest = it;
            }
        }
        if (oldest->second->ref_count > 0) break;

        auto* mapping = oldest->second;
        host_mappings_.erase(oldest);
        ibv_dereg_mr(mapping->host_mr);
        total_host_registered_size_ -= mapping->size;
        delete mapping;
    }
}