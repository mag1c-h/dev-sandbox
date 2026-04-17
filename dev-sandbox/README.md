# dev-sandbox

离散→离散 H2D 拷贝原型验证项目。

## 目标

验证五种 H2D 离散地址拷贝方案的可行性与性能：

1. **方案 A**: aclrtMemcpyBatch 直接离散→离散
2. **方案 B**: aclrtMemcpyAsync 异步流水线
3. **方案 C**: FFTS SDMA Host→Device 任务图（待验证硬件支持）
4. **方案 D**: H2H + H2D + FFTS D2D 三阶段（普通内存场景）
5. **方案 E**: HUGE_FFTS 双流流水线 + FFTS Scatter（锁页内存场景）

## 方案对比

| 方案 | 输入内存 | 流程 | 特点 |
|------|---------|------|------|
| **A** | Host Pin | Host→Device | 简单批量 |
| **B** | Host Pin | Host→Device | 异步流水线 |
| **C** | Host Pin | Host→Device | FFTS 直接（待验证） |
| **D** | Host DRAM | H2H→H2D→D2D | 普通内存场景 |
| **E** | Host Pin | H2D→D2D | 双流+Notify+FFTS Scatter |

## 构建

```bash
# 确保 CANN 已安装
source /usr/local/Ascend/ascend-toolkit/set_env.sh

# 构建
mkdir build && cd build
cmake ..
make -j
```

## 运行测试

```bash
# 可行性验证（5种方案）
./feasibility_test

# 性能对比（5种方案）
./performance_benchmark
```

## 测试参数

- Blob 数量: 1, 8, 64
- Blob 大小: 4KB, 64KB, 1MB

## 预期结果

```
可行性测试:
  ✅ Direct discrete H2D feasible
  ✅ Async pipeline H2D feasible
  ❓ FFTS H2D (depends on hardware)
  ✅ Three stage H2D feasible
  ✅ Three stage H2D (HugeFFTS) feasible

性能对比:
--- BlobCount=64, BlobSize=1MB ---
  Direct:      X.XX GB/s
  Async:       X.XX GB/s
  FFTS:        X.XX GB/s (if supported)
  3-Stage:     X.XX GB/s (普通内存，有H2H开销)
  3-Stage-H:   X.XX GB/s (锁页内存，跳过H2H)
```

## 关键设计

### 方案 E (HUGE_FFTS) 双流流水线

```
双槽位交替 + Notify同步 + FFTS Scatter

Object 0: HostPin[0] ──► DevicePin[slot0] ──► DestBlob[0,1,2]
           (H2D DMA)        (FFTS D2D)

Object 1: HostPin[1] ──► DevicePin[slot1] ──► DestBlob[3,4,5]
           (H2D DMA)        (FFTS D2D)

流水线效果：
  slot0 H2D 与 slot1 D2D 可并行执行
  slot1 H2D 与 slot0 D2D 可并行执行
```

## 文件结构

```
src/
  ffts_dispatcher_minimal.cpp  - FFTS 最小封装
  direct_discrete_h2d.cpp      - 方案 A 实现
  async_pipeline_h2d.cpp       - 方案 B 实现
  ffts_h2d.cpp                 - 方案 C 实现
  three_stage_h2d.cpp          - 方案 D 实现
  three_stage_h2d_huge.cpp     - 方案 E 实现（双流流水线）

tests/
  feasibility_test.cpp         - 可行性验证
  performance_benchmark.cpp    - 性能对比
```

## 依赖

- CANN 8.2.RC1+
- Ascend NPU 硬件