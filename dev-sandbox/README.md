# dev-sandbox

离散→离散 H2D 拷贝原型验证项目。

## 目标

验证三种 H2D 离散地址拷贝方案的可行性与性能：

1. **方案 A**: aclrtMemcpyBatch 直接离散→离散
2. **方案 B**: aclrtMemcpyAsync 异步流水线
3. **方案 C**: FFTS SDMA 任务图（待验证硬件支持）

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
# 可行性验证
./feasibility_test

# 性能对比
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

性能对比:
  Direct:   X.XX GB/s
  Async:    X.XX GB/s
  FFTS:     X.XX GB/s (if supported)
```

## 文件结构

```
src/
  ffts_dispatcher_minimal.cpp  - FFTS 最小封装
  direct_discrete_h2d.cpp      - 方案 A 实现
  async_pipeline_h2d.cpp       - 方案 B 实现
  ffts_h2d.cpp                 - 方案 C 实现

tests/
  feasibility_test.cpp         - 可行性验证
  performance_benchmark.cpp    - 性能对比
```

## 依赖

- CANN 8.2.RC1+
- Ascend NPU 硬件