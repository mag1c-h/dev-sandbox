#ifndef FFTS_DISPATCHER_MINIMAL_H
#define FFTS_DISPATCHER_MINIMAL_H

#include <vector>
#include <cstdint>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <future>
#include <memory>

#include "simple_thread_pool.h"

#ifdef USE_NPU
#include <runtime/rt_ffts_plus.h>
#include <runtime/rt_ffts_plus_define.h>
#include <runtime/dev.h>
#else
typedef void* aclrtStream;
typedef void* rtNotify_t;
typedef struct {} rtFftsPlusComCtx_t;
constexpr uint16_t RT_CTX_TYPE_SDMA = 1;
constexpr uint16_t RT_FFTS_PLUS_TYPE = 1;
constexpr uint32_t RT_GNL_CTRL_TYPE_FFTS_PLUS = 1;
#endif

struct H2DTaskInfo {
    size_t index;
    void* hostPinPtr;
    size_t size;
    void* deviceDestPtr;
};

struct H2DTaskQueue {
    std::vector<H2DTaskInfo> tasks;
    bool IsEmpty() const { return tasks.empty(); }
    void Clear() { tasks.clear(); }
};

class FftsDispatcherMinimal {
public:
    FftsDispatcherMinimal();
    ~FftsDispatcherMinimal();

    int Init();
    int CreateFftsCtxs(int amount);
    int SetFftsCtx(int index);
    int GetCurrentCtxIndex() const;
    int MemcpyAsync(void* dst, const void* src, uint64_t size, uint32_t* taskId);
    int AddTaskDependency(uint32_t predecessorId, uint32_t successorId);
    int LaunchFftsTask(aclrtStream stream, uint16_t readyCount, int ctxIndex);
    int ReuseCtx(int index);
    
    void SetH2hThreadPool(std::shared_ptr<SimpleThreadPool> pool);
    std::shared_ptr<SimpleThreadPool> GetH2hThreadPool() const;
    
    void SetH2dThreadPool(std::shared_ptr<SimpleThreadPool> pool);
    std::shared_ptr<SimpleThreadPool> GetH2dThreadPool() const;

private:
    struct FftsCtx {
        std::vector<rtFftsPlusComCtx_t> contexts;
        uint32_t refreshIndex = 0;
        uint32_t ctxNum = 0;
    };
    
    std::vector<FftsCtx> ctxs_;
    FftsCtx* currentCtx_ = nullptr;
    int deviceId_ = 0;
    int currentCtxIndex_ = 0;
    
    std::shared_ptr<SimpleThreadPool> h2hThreadPool_;
    std::shared_ptr<SimpleThreadPool> h2dThreadPool_;
    
    int EnsureContextSize();
};

#endif