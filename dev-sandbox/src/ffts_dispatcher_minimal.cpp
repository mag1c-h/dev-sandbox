#include "ffts_dispatcher_minimal.h"
#include <cstring>
#include <acl/acl.h>
#include <runtime/dev.h>

#ifdef USE_NPU
extern int rtGeneralCtrl(uintptr_t* input, size_t inputSize, uint32_t type);
#endif

#ifndef USE_NPU
constexpr uint32_t RT_CTX_SUCCESSOR_NUM = 26;
#endif

FftsDispatcherMinimal::FftsDispatcherMinimal() {}

FftsDispatcherMinimal::~FftsDispatcherMinimal() {}

int FftsDispatcherMinimal::Init() {
    aclrtGetDevice(&deviceId_);
    return 0;
}

int FftsDispatcherMinimal::CreateFftsCtxs(int amount) {
    ctxs_.resize(amount);
    for (int i = 0; i < amount; i++) {
        ctxs_[i].contexts.resize(128);
        ctxs_[i].refreshIndex = 0;
        ctxs_[i].ctxNum = 0;
    }
    return 0;
}

int FftsDispatcherMinimal::SetFftsCtx(int index) {
    if (index >= (int)ctxs_.size()) return -1;
    currentCtx_ = &ctxs_[index];
    return 0;
}

int FftsDispatcherMinimal::EnsureContextSize() {
    if (currentCtx_->refreshIndex >= currentCtx_->contexts.size()) {
        currentCtx_->contexts.resize(currentCtx_->contexts.size() * 2);
    }
    return 0;
}

int FftsDispatcherMinimal::MemcpyAsync(void* dst, const void* src, uint64_t size, uint32_t* taskId) {
    if (currentCtx_ == nullptr) return -1;
    
    EnsureContextSize();
    
    rtFftsPlusComCtx_t& ctx = currentCtx_->contexts[currentCtx_->refreshIndex];
    std::memset(&ctx, 0, sizeof(ctx));
    
#ifdef USE_NPU
    rtFftsPlusSdmaCtx_t* sdmaCtx = reinterpret_cast<rtFftsPlusSdmaCtx_t*>(&ctx);
    
    sdmaCtx->contextType = RT_CTX_TYPE_SDMA;
    sdmaCtx->threadDim = 1;
    sdmaCtx->sdmaSqeHeader = 0x1E70;  // SDMA_FP32_ATOMIC_MOVE_SQE
    
    const uint64_t lowMask = 0x00000000ffffffffULL;
    const uint64_t highMask = 0xffffffff00000000ULL;
    
    sdmaCtx->sourceAddressBaseL = reinterpret_cast<uint64_t>(src) & lowMask;
    sdmaCtx->sourceAddressBaseH = (reinterpret_cast<uint64_t>(src) & highMask) >> 32;
    sdmaCtx->destinationAddressBaseL = reinterpret_cast<uint64_t>(dst) & lowMask;
    sdmaCtx->destinationAddressBaseH = (reinterpret_cast<uint64_t>(dst) & highMask) >> 32;
    sdmaCtx->nonTailDataLength = size;
    sdmaCtx->tailDataLength = size;
#endif
    
    *taskId = currentCtx_->refreshIndex;
    currentCtx_->refreshIndex++;
    return 0;
}

int FftsDispatcherMinimal::AddTaskDependency(uint32_t predecessorId, uint32_t successorId) {
    if (currentCtx_ == nullptr) return -1;
    if (predecessorId >= currentCtx_->contexts.size() || successorId >= currentCtx_->contexts.size()) {
        return -1;
    }
    
#ifdef USE_NPU
    rtFftsPlusComCtx_t& predCtx = currentCtx_->contexts[predecessorId];
    if (predCtx.successorNum < RT_CTX_SUCCESSOR_NUM) {
        predCtx.successorList[predCtx.successorNum] = successorId;
        predCtx.successorNum++;
    }
    
    rtFftsPlusComCtx_t& succCtx = currentCtx_->contexts[successorId];
    succCtx.predCntInit++;
    succCtx.predCnt++;
#endif
    
    return 0;
}

int FftsDispatcherMinimal::LaunchFftsTask(aclrtStream stream, uint16_t readyCount, int ctxIndex) {
    if (currentCtx_ == nullptr) return -1;
    
    currentCtx_->ctxNum = currentCtx_->refreshIndex;
    
    if (currentCtx_->ctxNum == 0) return 0;
    
#ifdef USE_NPU
    rtFftsPlusSqe_t sqe = {};
    sqe.fftsType = RT_FFTS_PLUS_TYPE;
    sqe.totalContextNum = currentCtx_->ctxNum;
    sqe.readyContextNum = readyCount;
    sqe.preloadContextNum = (readyCount <= 128) ? readyCount : 128;
    sqe.timeout = 0;
    sqe.subType = 0x5A;  // Communication task
    
    rtFftsPlusTaskInfo_t task = {};
    task.fftsPlusSqe = &sqe;
    task.descBuf = currentCtx_->contexts.data();
    task.descBufLen = currentCtx_->ctxNum * sizeof(rtFftsPlusComCtx_t);
    task.descAddrType = 0;
    task.argsHandleInfoNum = 0;
    task.argsHandleInfoPtr = nullptr;
    
    uintptr_t input[2];
    input[0] = reinterpret_cast<uintptr_t>(&task);
    input[1] = reinterpret_cast<uintptr_t>(stream);
    
    int ret = rtGeneralCtrl(input, 2, RT_GNL_CTRL_TYPE_FFTS_PLUS);
    if (ret != 0) {
        return ret;
    }
#endif
    
    return 0;
}

int FftsDispatcherMinimal::ReuseCtx(int index) {
    if (index >= (int)ctxs_.size()) return -1;
    ctxs_[index].refreshIndex = 0;
    ctxs_[index].ctxNum = 0;
    return 0;
}