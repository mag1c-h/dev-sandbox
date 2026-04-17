#ifndef FFTS_DISPATCHER_MINIMAL_H
#define FFTS_DISPATCHER_MINIMAL_H

#include <vector>
#include <cstdint>

#ifdef USE_NPU
#include <runtime/rt_ffts_plus.h>
#include <runtime/rt_ffts_plus_define.h>
#else
typedef void* aclrtStream;
typedef struct {} rtFftsPlusComCtx_t;
typedef struct {} rtFftsPlusSdmaCtx_t;
typedef struct {} rtFftsPlusSqe_t;
typedef struct {} rtFftsPlusTaskInfo_t;
constexpr uint16_t RT_CTX_TYPE_SDMA = 1;
constexpr uint16_t RT_FFTS_PLUS_TYPE = 1;
constexpr uint32_t RT_GNL_CTRL_TYPE_FFTS_PLUS = 1;
#endif

class FftsDispatcherMinimal {
public:
    FftsDispatcherMinimal();
    ~FftsDispatcherMinimal();

    int Init();
    int CreateFftsCtxs(int amount);
    int SetFftsCtx(int index);
    int MemcpyAsync(void* dst, const void* src, uint64_t size, uint32_t* taskId);
    int AddTaskDependency(uint32_t predecessorId, uint32_t successorId);
    int LaunchFftsTask(aclrtStream stream, uint16_t readyCount, int ctxIndex);
    int ReuseCtx(int index);

private:
    struct FftsCtx {
        std::vector<rtFftsPlusComCtx_t> contexts;
        uint32_t refreshIndex = 0;
        uint32_t ctxNum = 0;
    };
    
    std::vector<FftsCtx> ctxs_;
    FftsCtx* currentCtx_ = nullptr;
    int deviceId_ = 0;
    
    int EnsureContextSize();
};

#endif