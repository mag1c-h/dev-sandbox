# Transfer 设计与实现指南

## 目录

1. [项目概述](#1-项目概述)
2. [架构设计](#2-架构设计)
3. [核心组件详解](#3-核心组件详解)
4. [扩展开发指南](#4-扩展开发指南)
5. [最佳实践](#5-最佳实践)
6. [常见问题](#6-常见问题)

---

## 1. 项目概述

### 1.1 Transfer 的定位

Transfer 是一个传输引擎抽象框架，提供：

- **统一的传输接口**：通过 `IStream` 接口抽象所有传输操作
- **多地址类型支持**：本地文件、网络地址、云存储地址等
- **多协议支持**：sendfile、HTTP、FTP、S3 等传输协议
- **类型安全**：CRTP 模式 + 类型包装器，避免运行时类型错误
- **自动注册**：使用 GCC constructor attribute 实现自动注册机制

### 1.2 核心特性

#### 自动注册机制

Transfer 使用 `__attribute__((constructor))` 实现 shared library 加载时的自动注册，无需手动调用注册函数：

```cpp
// 在 shared library 加载时自动执行注册
REGISTER_TRANSFER_STREAM_WITH_PROTOCOL(
    LocalFile2LocalFileSendfileStream,
    FileAddress,
    FileAddress,
    SendfileProtocol
);
```

#### 类型安全

使用 CRTP（Curiously Recurring Template Pattern）+ 类型包装器实现类型安全：

- **CRTP 基类**：`Address<T>` 和 `Protocol<T>` 提供静态类型信息
- **类型包装器**：`AnyAddress` 和 `AnyProtocol` 使用 `std::any` + `type_info` 实现类型安全存储和访问

#### 错误处理

使用 `Expected<T>` 模式替代传统的异常或错误码：

- **隐式构造**：成功时直接返回值，失败时返回 `Error` 对象
- **按值传递**：自动 move 语义，避免拷贝开销
- **查询方法**：`ok()` 查询状态，`value()` 获取值，`error()` 获取错误信息

### 1.3 适用场景

Transfer 框架是通用的数据传输抽象层，适用于多种存储介质和网络传输场景：

#### 节点内存储层次传输

在单个计算节点内的不同存储层次之间传输数据：

- **HBM ↔ DRAM**：GPU显存与主机内存之间的数据迁移
- **DRAM ↔ SSD**：内存与持久化存储之间的数据交换
- **HBM ↔ SSD**：显存与存储设备之间的直接数据传输

典型用例：
- AI训练时的数据加载和模型参数传输
- 大规模数据集加载和缓存管理
- 高性能计算中的临时数据交换

#### 跨节点网络传输

在不同计算节点之间通过网络传输数据：

- **RDMA传输**：节点间零拷贝、低延迟的内存直接访问传输
- **传统网络**：HTTP/FTP/WebSocket等标准协议传输
- **云存储**：S3、Azure Blob、Google Cloud Storage等云服务

典型用例：
- 分布式训练中的梯度同步
- 高性能计算集群的数据共享
- 云存储数据的上传和下载

#### 本地文件传输

传统文件系统操作：

- 文件拷贝、批量传输
- 目录同步、本地备份

#### 自定义扩展

Transfer 采用插件式架构，支持用户自定义：

- 新地址类型：自定义存储介质或数据源
- 新传输协议：自定义传输策略和优化实现

---

## 2. 架构设计

### 2.1 分层架构

Transfer 采用五层架构设计，每层职责清晰：

```
┌─────────────────────────────────────────────────┐
│  Application Layer (用户代码)                    │
│  - 调用 Factory::create()                       │
│  - 使用 IStream API                             │
└─────────────────────┬───────────────────────────┘
                      ↓ calls
┌─────────────────────────────────────────────────┐
│  API Layer (Factory)                            │
│  - create(src, dst, protocol)                   │
│  - list_protocols(src_type, dst_type)           │
│  - is_supported(src_type, dst_type)             │
│  - 返回 Expected<unique_ptr<IStream>>           │
└─────────────────────┬───────────────────────────┘
                      ↓ queries creator
┌─────────────────────────────────────────────────┐
│  Registration Layer (Registry)                  │
│  - creators_: map<string, Creator>              │
│  - register_creator() (启动时自动执行)           │
│  - get_creator() (运行时查询)                    │
│  - 键格式："src_type->dst_type:protocol_name"   │
└─────────────────────┬───────────────────────────┘
                      ↓ invokes Creator function
┌─────────────────────────────────────────────────┐
│  Implementation Layer (IStream)                 │
│  - submit(src, dst, size) → 单个提交             │
│  - submit(vector<tuple>) → 批量提交              │
│  - synchronize() → 执行所有任务                  │
│  - 任务队列管理 + IO 执行                        │
└─────────────────────┬───────────────────────────┘
                      ↓ uses
┌─────────────────────────────────────────────────┐
│  Type Layer (Address/Protocol)                  │
│  - Address<T>: CRTP 基类                        │
│  - Protocol<T>: CRTP 基类                       │
│  - AnyAddress: std::any + type_info 包装器       │
│  - AnyProtocol: std::any + type_info 包装器      │
└─────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────┐
│  Error Handling Layer (Expected)                │
│  - Expected<T>: 成功值 + 失败错误                │
│  - Error: code + message + system_errno         │
│  - ErrorCode: 错误码枚举                         │
└─────────────────────────────────────────────────┘
```

#### 层职责说明

| 层次 | 职责 | 关键类 |
|------|------|--------|
| **Application Layer** | 用户代码调用入口 | 用户代码 |
| **API Layer** | 提供统一的创建和查询接口 | Factory |
| **Registration Layer** | 存储和管理传输实现注册 | Registry |
| **Implementation Layer** | 实现具体传输逻辑 | IStream 及实现类 |
| **Type Layer** | 提供地址和协议的类型系统 | Address, Protocol, AnyAddress, AnyProtocol |
| **Error Handling Layer** | 提供统一的错误处理机制 | Expected, Error, ErrorCode |

### 2.2 注册流程详解

#### 2.2.1 Constructor Attribute 机制

GCC 提供的 `__attribute__((constructor))` 属性允许在 shared library 加载时自动执行函数：

```cpp
// 定义 constructor 函数
__attribute__((constructor)) static void init_function() {
    // 此函数在 shared library 加载时自动执行
    // 执行时机：main() 之前，或在 dlopen() 时
}

// shared library (.so) 加载时执行顺序：
// 1. 加载 shared library (dlopen 或程序启动)
// 2. 执行所有 constructor 函数（按定义顺序）
// 3. 执行 main() 或 dlopen() 返回
```

#### 2.2.2 注册宏工作原理

注册宏 `REGISTER_TRANSFER_STREAM_WITH_PROTOCOL` 展开为：

```cpp
// 假设调用：
REGISTER_TRANSFER_STREAM_WITH_PROTOCOL(
    LocalFile2LocalFileSendfileStream,
    FileAddress,
    FileAddress,
    SendfileProtocol
);

// 宏展开为：
__attribute__((constructor)) static void
_init_LocalFile2LocalFileSendfileStream_SendfileProtocol() {
    // 1. 获取 Registry 单例
    ::ucm::transfer::Registry::instance().register_creator(
        // 2. 提供类型信息
        FileAddress::type_name(),           // "local-file"
        FileAddress::type_name(),           // "local-file"
        SendfileProtocol::name(),           // "sendfile"

        // 3. 提供 Creator 函数
        [](const AnyAddress& src,
           const AnyAddress& dst,
           const AnyProtocol& proto)
            -> std::unique_ptr<IStream> {
            // 4. 类型转换（类型安全）
            return std::make_unique<LocalFile2LocalFileSendfileStream>(
                src.cast<FileAddress>(),
                dst.cast<FileAddress>(),
                proto.cast<SendfileProtocol>()
            );
        }
    );
}
```

#### 2.2.3 Shared Library 加载时机

注册在 shared library 加载时自动执行：

```cpp
// 程序启动流程：
main() {
    // 1. 加载 libucmtransfer.so
    // 2. 执行所有 constructor 函数（包括注册宏生成的函数）
    // 3. Registry 已填充所有注册信息

    // 4. 用户调用 Factory::create
    auto stream = Factory::create(src, dst);

    // 5. Factory 查询 Registry，找到 Creator
    // 6. Creator 创建 IStream 实例
}
```

#### 2.2.4 Registry 键格式

Registry 使用键格式 `"src_type->dst_type:protocol_name"` 存储注册信息：

```cpp
// 键生成规则：
std::string make_key(
    const std::string& src_type,
    const std::string& dst_type,
    const std::string& protocol_name
) {
    return src_type + "->" + dst_type + ":" + protocol_name;
}

// 示例键：
// "local-file->local-file:sendfile"       (File → File, sendfile协议)
// "local-file->s3:http"                   (File → S3, HTTP协议)
// "s3->local-file:http"                   (S3 → File, HTTP协议)
```

### 2.3 创建流程详解

#### 2.3.1 Factory::create 查询流程

Factory::create 的完整流程：

```cpp
// 步骤1：用户调用 Factory::create
auto result = Factory::create(src_addr, dst_addr);

// 步骤2：Factory 内部流程
Expected<std::unique_ptr<IStream>> create(
    const AnyAddress& src,
    const AnyAddress& dst
) {
    // 2.1 获取类型信息
    auto src_type = src.type_name();   // "local-file"
    auto dst_type = dst.type_name();   // "local-file"

    // 2.2 查询 Registry
    auto creator = Registry::instance().get_creator(
        src_type,
        dst_type,
        DefaultProtocol::name()  // ""
    );

    // 2.3 检查 Creator 是否存在
    if (!creator) {
        return Error{ErrorCode::UnsupportedTransfer,
                     "No transfer registered"};
    }

    // 2.4 创建 Protocol 包装器
    AnyProtocol protocol(DefaultProtocol{});

    // 2.5 调用 Creator
    auto stream = creator(src, dst, protocol);

    // 2.6 检查 stream 创建是否成功
    if (!stream) {
        return Error{ErrorCode::ResourceAllocationFailed,
                     "Failed to create stream"};
    }

    // 2.7 返回成功值（隐式构造）
    return stream;  // Expected<T> 自动包装
}
```

#### 2.3.2 Creator 调用机制

Creator 是一个 lambda 函数，负责：

```cpp
// Creator 类型定义：
using Creator = std::function<
    std::unique_ptr<IStream>(
        const AnyAddress& src,
        const AnyAddress& dst,
        const AnyProtocol& proto
    )
>;

// Creator 执行步骤：
// 1. 接收 AnyAddress/AnyProtocol 包装器
// 2. 类型转换（cast<T>()）获取具体类型
// 3. 创建 IStream 实例（make_unique）
// 4. 返回 unique_ptr<IStream>

// Creator 示例（注册宏中生成）：
auto creator = [](const AnyAddress& src,
                  const AnyAddress& dst,
                  const AnyProtocol& proto)
    -> std::unique_ptr<IStream> {
    // 类型安全转换
    return std::make_unique<LocalFile2LocalFileSendfileStream>(
        src.cast<FileAddress>(),
        dst.cast<FileAddress>(),
        proto.cast<SendfileProtocol>()
    );
};
```

#### 2.3.3 Expected 返回值处理

Factory 返回 `Expected<std::unique_ptr<IStream>>`：

```cpp
// 使用方式：
auto result = Factory::create(src, dst);

// 检查是否成功：
if (!result.ok()) {
    // 失败：获取错误信息
    std::cerr << result.error().message << std::endl;
    return;
}

// 成功：获取 stream
auto stream = result.take_value();  // 移动语义

// 使用 stream：
stream->submit(task);
stream->synchronize();
```

### 2.4 接口语义约定

#### 2.4.1 Submit 语义

**单个提交**：

- `submit(uint64_t src, uint64_t dst, std::size_t size)`
- **目的**：提交单个传输任务
- **参数**：
  - `src`：源地址偏移（字节）
  - `dst`：目标地址偏移（字节）
  - `size`：传输大小（字节）
- **返回值**：`Expected<void>`（成功或错误）
- **语义**：任务入队，立即返回；实际传输在 synchronize 时执行
- **约定**：size 为 0 返回 `ErrorCode::InvalidTask`

**批量提交**：

- `submit(std::vector<std::tuple<uint64_t, uint64_t, std::size_t>> ranges)`
- **目的**：提交多个传输任务
- **参数**：vector of tuple<src, dst, size>
- **返回值**：`Expected<void>`
- **语义**：所有 ranges 入队，立即返回
- **约定**：空 vector 返回 `ErrorCode::InvalidTask`

**使用示例**：

```cpp
// 单个提交
stream->submit(0, 0, 1024);

// 批量提交
stream->submit({
    {0, 0, 500},
    {500, 1000, 500}
});
```

#### 2.4.2 Synchronize 语义

- `synchronize()`
- **目的**：执行所有已提交的传输任务
- **返回值**：`Expected<void>`（成功或第一个错误）
- **语义**：顺序执行队列中的所有 ranges，遇到错误立即返回
- **约定**：执行后队列清空，失败时保留已完成的传输

### 2.5 设计原则

#### 2.5.1 CRTP 模式原理

CRTP（Curiously Recurring Template Pattern）用于提供静态类型信息：

```cpp
// CRTP 基类：
template<typename Derived>
struct Address {
    static std::string type_name() {
        return Derived::TYPE;  // 调用派生类的静态成员
    }
};

// CRTP 派生类：
struct FileAddress : Address<FileAddress> {  // 继承时传入自身
    static constexpr const char* TYPE = "local-file";
    std::filesystem::path path;
};

// 调用：
FileAddress::type_name();  // 返回 "local-file"
// 等价于 Address<FileAddress>::type_name()
// 等价于 FileAddress::TYPE
```

**优势**：
- 无虚函数开销（静态方法）
- 编译时类型检查
- 类型信息在编译时确定

#### 2.5.2 隐式构造设计

Expected<T> 使用隐式构造简化返回值：

```cpp
// Expected 定义：
template<typename T>
class Expected {
public:
    Expected(T value) : value_(std::move(value)), error_{} {}  // 成功
    Expected(Error error) : value_(std::nullopt), error_(error) {}  // 失败
};

// 使用方式：
Expected<std::unique_ptr<IStream>> create(...) {
    if (条件) {
        return Error{ErrorCode::..., "..."};  // 失败：隐式构造
    }
    return stream;  // 成功：隐式构造，自动 move
}

// 对比传统方式：
// 传统1：返回错误码 + 输出参数
// int create(..., IStream** out);  // 不优雅
// 传统2：抛异常
// IStream* create(...) { throw Exception; }  // 性能问题
```

#### 2.5.3 类型包装机制

AnyAddress/AnyProtocol 使用 std::any + type_info 实现类型安全包装：

```cpp
// AnyAddress 定义：
class AnyAddress {
private:
    std::any value_;              // 存储任意类型
    std::string type_name_;       // 类型名称
    const std::type_info* type_info_;  // 类型信息指针

public:
    // 构造：从任意地址类型
    template<typename Addr>
    AnyAddress(Addr addr)
        : value_(std::move(addr)),
          type_name_(Addr::type_name()),
          type_info_(&typeid(Addr)) {}

    // 类型转换：类型安全
    template<typename Addr>
    const Addr& cast() const {
        if (type_info_ != &typeid(Addr)) {
            throw std::bad_any_cast();  // 类型不匹配抛异常
        }
        return std::any_cast<const Addr&>(value_);
    }
};

// 使用：
FileAddress file_addr("/path/to/file");
AnyAddress any_addr(file_addr);  // 包装

const FileAddress& recovered = any_addr.cast<FileAddress>();  // 解包
// 类型检查：type_info_ == &typeid(FileAddress)
```

#### 2.5.4 单例模式应用

Registry 使用单例模式：

```cpp
class Registry {
public:
    // 单例访问：
    static Registry& instance() {
        static Registry registry;  // 静态局部变量（线程安全）
        return registry;
    }

private:
    Registry() = default;  // 私有构造
    Registry(const Registry&) = delete;  // 禁止拷贝
    Registry& operator=(const Registry&) = delete;  // 禁止赋值
};

// 使用：
Registry::instance().register_creator(...);
Registry::instance().get_creator(...);
```

**单例优势**：
- 全局唯一实例（所有注册信息集中）
- 线程安全（C++11 保证静态局部变量初始化线程安全）
- 无需手动管理生命周期

---

## 3. 核心组件详解

### 3.1 Registry（注册表）

#### 3.1.1 Singleton 实现原理

Registry 使用 Meyer's Singleton（静态局部变量）：

```cpp
class Registry {
public:
    static Registry& instance() {
        static Registry registry;  // 线程安全初始化
        return registry;
    }

    // 禁止拷贝和赋值
    Registry(const Registry&) = delete;
    Registry& operator=(const Registry&) = delete;

private:
    Registry() = default;  // 私有构造，防止外部创建
    ~Registry() = default;

    std::unordered_map<std::string, Creator> creators_;
};
```

**线程安全性**：
- C++11 标准：静态局部变量初始化是线程安全的
- 编译器生成同步代码，保证只初始化一次

#### 3.1.2 Creator 类型定义

Creator 是函数类型，负责创建 IStream 实例：

```cpp
// Creator 类型：
using Creator = std::function<
    std::unique_ptr<IStream>(
        const AnyAddress& src,
        const AnyAddress& dst,
        const AnyProtocol& proto
    )
>;

// Creator 的职责：
// 1. 接收 AnyAddress/AnyProtocol
// 2. 类型安全转换（cast<T>()）
// 3. 创建具体 IStream 实例
// 4. 返回 unique_ptr<IStream>
```

#### 3.1.3 键生成规则

Registry 使用键格式 `"src_type->dst_type:protocol_name"`：

```cpp
private:
    static std::string make_key(
        const std::string& src_type,
        const std::string& dst_type,
        const std::string& protocol_name
    ) {
        return src_type + "->" + dst_type + ":" + protocol_name;
    }

// 键示例：
// "local-file->local-file:sendfile"
// "local-file->s3:http"
// "s3->local-file:http"
// "http->http:default"
```

**键设计理由**：
- 三元组唯一标识（源类型 + 目标类型 + 协议）
- 易于查询和列表
- 字符串格式便于调试和日志

#### 3.1.4 核心方法说明

**注册方法**：

```cpp
void register_creator(
    const std::string& src_type,
    const std::string& dst_type,
    const std::string& protocol_name,
    Creator creator
) {
    std::string key = make_key(src_type, dst_type, protocol_name);
    creators_[key] = std::move(creator);  // 存入 map
}
```

**查询方法**：

```cpp
Creator get_creator(
    const std::string& src_type,
    const std::string& dst_type,
    const std::string& protocol_name
) const {
    std::string key = make_key(src_type, dst_type, protocol_name);
    auto it = creators_.find(key);
    if (it != creators_.end()) {
        return it->second;  // 找到：返回 Creator
    }
    return nullptr;  // 未找到：返回 nullptr
}
```

**列表方法**：

```cpp
std::vector<std::string> list_protocols(
    const std::string& src_type,
    const std::string& dst_type
) const {
    std::vector<std::string> protocols;
    std::string prefix = src_type + "->" + dst_type + ":";

    for (const auto& [key, creator] : creators_) {
        if (key.substr(0, prefix.size()) == prefix) {
            // 提取协议名称（去掉前缀）
            protocols.push_back(key.substr(prefix.size()));
        }
    }

    return protocols;
}
```

### 3.2 Factory（工厂）

#### 3.2.1 静态方法设计

Factory 所有方法都是静态方法，无需创建实例：

```cpp
class Factory {
public:
    // 默认协议创建：
    static Expected<std::unique_ptr<IStream>> create(
        const AnyAddress& src,
        const AnyAddress& dst
    );

    // 自定义协议创建：
    template<typename Protocol>
    static Expected<std::unique_ptr<IStream>> create(
        const AnyAddress& src,
        const AnyAddress& dst,
        Protocol protocol
    );

    // 查询方法：
    static std::vector<std::string> list_protocols(
        const std::string& src_type,
        const std::string& dst_type
    );

    static bool is_supported(
        const std::string& src_type,
        const std::string& dst_type
    );
};
```

#### 3.2.2 默认协议 vs 自定义协议

**默认协议**：

```cpp
static Expected<std::unique_ptr<IStream>> create(
    const AnyAddress& src,
    const AnyAddress& dst
) {
    // 使用 DefaultProtocol（空字符串）
    auto creator = Registry::instance().get_creator(
        src.type_name(),
        dst.type_name(),
        DefaultProtocol::name()  // ""
    );

    if (!creator) {
        return Error{ErrorCode::UnsupportedTransfer, "..."};
    }

    AnyProtocol protocol(DefaultProtocol{});
    auto stream = creator(src, dst, protocol);

    if (!stream) {
        return Error{ErrorCode::ResourceAllocationFailed, "..."};
    }

    return stream;
}
```

**自定义协议**：

```cpp
template<typename Protocol>
static Expected<std::unique_ptr<IStream>> create(
    const AnyAddress& src,
    const AnyAddress& dst,
    Protocol protocol  // 用户传入协议对象
) {
    // 使用 Protocol::name()
    auto creator = Registry::instance().get_creator(
        src.type_name(),
        dst.type_name(),
        Protocol::name()  // "sendfile", "http", etc.
    );

    if (!creator) {
        return Error{ErrorCode::UnsupportedProtocol, "..."};
    }

    AnyProtocol proto_wrapper(protocol);
    auto stream = creator(src, dst, proto_wrapper);

    if (!stream) {
        return Error{ErrorCode::ResourceAllocationFailed, "..."};
    }

    return stream;
}
```

#### 3.2.3 Expected 返回处理

Factory 返回 `Expected<std::unique_ptr<IStream>>`：

```cpp
// 成功场景：
return stream;  // Expected 隐式构造，自动 move

// 失败场景：
return Error{ErrorCode::UnsupportedTransfer,
             "No transfer registered for ..."};
```

### 3.3 IStream（流接口）

#### 3.3.1 接口定义

IStream 是核心接口，定义传输操作：

```cpp
class IStream {
public:
    virtual ~IStream() = default;

    // 单个提交：
    virtual Expected<void> submit(uint64_t src, uint64_t dst, std::size_t size) = 0;

    // 批量提交：
    virtual Expected<void> submit(std::vector<std::tuple<uint64_t, uint64_t, std::size_t>> ranges) = 0;

    // 执行所有任务：
    virtual Expected<void> synchronize() = 0;
};
```

**接口说明**：

- **单个 submit**：提交单个传输任务，参数直接传递（无结构体）
- **批量 submit**：提交多个传输任务，使用 vector<tuple<src, dst, size>>
- **synchronize**：执行队列中所有任务，返回成功或第一个错误

### 3.4 Address<T>（地址 CRTP）

#### 3.4.1 CRTP 基类定义

Address 使用 CRTP 提供静态类型信息：

```cpp
template<typename Derived>
struct Address {
    static std::string type_name() {
        return Derived::TYPE;
    }
};

// 调用方式：
// Derived::type_name() 返回 Derived::TYPE
```

#### 3.4.2 FileAddress 示例

FileAddress 是具体地址实现：

```cpp
struct FileAddress : Address<FileAddress> {
    static constexpr const char* TYPE = "local-file";
    std::filesystem::path path;

    // 构造函数：
    FileAddress() = default;
    FileAddress(std::filesystem::path p) : path(std::move(p)) {}
    FileAddress(const char* p) : path(p) {}
    FileAddress(std::string p) : path(std::move(p)) {}
};

// 使用：
FileAddress src("/path/to/source");
FileAddress dst("/path/to/destination");

auto result = Factory::create(
    AnyAddress(src),
    AnyAddress(dst)
);
```

#### 3.4.3 AnyAddress 包装器实现

AnyAddress 使用 std::any 包装任意地址类型：

```cpp
class AnyAddress {
private:
    std::any value_;
    std::string type_name_;
    const std::type_info* type_info_;

public:
    // 构造：从任意地址类型
    template<typename Addr>
    AnyAddress(Addr addr)
        : value_(std::move(addr)),
          type_name_(Addr::type_name()),
          type_info_(&typeid(Addr)) {}

    // 类型转换：类型安全
    template<typename Addr>
    const Addr& cast() const {
        if (type_info_ != &typeid(Addr)) {
            throw std::bad_any_cast();
        }
        return std::any_cast<const Addr&>(value_);
    }

    // 获取类型信息：
    const std::string& type_name() const { return type_name_; }
    const std::type_info& type() const { return *type_info_; }
};
```

**类型检查流程**：

```cpp
FileAddress file_addr("/path");
AnyAddress any_addr(file_addr);

// 正确转换：
const FileAddress& addr1 = any_addr.cast<FileAddress>();  // ✓

// 错误转换：
const HttpAddress& addr2 = any_addr.cast<HttpAddress>();  // ✗ 抛异常
// 原因：type_info_ == &typeid(FileAddress) != &typeid(HttpAddress)
```

### 3.5 Protocol<T>（协议 CRTP）

#### 3.5.1 CRTP 基类定义

Protocol 使用 CRTP 提供静态协议名称：

```cpp
template<typename Derived>
struct Protocol {
    static std::string name() {
        return Derived::PROTOCOL_NAME;
    }
};

// 调用方式：
// Derived::name() 返回 Derived::PROTOCOL_NAME
```

#### 3.5.2 DefaultProtocol 设计

DefaultProtocol 是默认协议（空字符串）：

```cpp
struct DefaultProtocol : Protocol<DefaultProtocol> {
    static constexpr const char* PROTOCOL_NAME = "";
};

// 使用场景：
// 当不指定协议时，使用 DefaultProtocol
auto result = Factory::create(src, dst);  // 使用 DefaultProtocol

// Registry 键：
// "local-file->local-file:"  (协议名是空字符串)
```

#### 3.5.3 AnyProtocol 包装器

AnyProtocol 包装任意协议类型：

```cpp
class AnyProtocol {
private:
    std::any value_;
    std::string name_;
    const std::type_info* type_info_;

public:
    // 构造：
    template<typename P>
    AnyProtocol(P protocol)
        : value_(std::move(protocol)),
          name_(P::name()),
          type_info_(&typeid(P)) {}

    // 类型转换：
    template<typename P>
    const P& cast() const {
        if (type_info_ != &typeid(P)) {
            throw std::bad_any_cast();
        }
        return std::any_cast<const P&>(value_);
    }

    // 获取协议名称：
    const std::string& name() const { return name_; }
};
```

**SendfileProtocol 示例**：

```cpp
struct SendfileProtocol : Protocol<SendfileProtocol> {
    static constexpr const char* PROTOCOL_NAME = "sendfile";
    size_t chunk_size = 64 * 1024;  // 64KB
    bool use_async = false;
};

// 使用：
SendfileProtocol proto;
proto.chunk_size = 128 * 1024;  // 128KB

AnyProtocol any_proto(proto);
const SendfileProtocol& recovered = any_proto.cast<SendfileProtocol>();
```

### 3.6 Expected<T>（错误处理）

#### 3.6.1 完整类定义

Expected<T> 完整定义：

```cpp
template<typename T>
class Expected {
private:
    std::optional<T> value_;
    Error error_;

public:
    // 成功构造（隐式）：
    Expected(T value)
        : value_(std::move(value)), error_{} {}

    // 失败构造（隐式）：
    Expected(Error error)
        : value_(std::nullopt), error_(std::move(error)) {}

    // 查询方法：
    bool ok() const { return value_.has_value(); }
    explicit operator bool() const { return ok(); }

    // 值访问：
    T& value() & { return *value_; }
    T&& value() && { return std::move(*value_); }
    const T& value() const& { return *value_; }

    // 移动语义：
    T take_value() { return std::move(*value_); }

    // 错误访问：
    const Error& error() const { return error_; }
};
```

#### 3.6.2 隐式构造原理

Expected 支持两种隐式构造：

```cpp
// 成功场景：返回值
Expected<std::unique_ptr<IStream>> create() {
    auto stream = std::make_unique<SomeStream>();
    return stream;  // 隐式构造 Expected(T)
    // 等价于 Expected<std::unique_ptr<IStream>>(std::move(stream))
}

// 失败场景：返回 Error
Expected<uint64_t> submit() {
    if (!open_) {
        return Error{ErrorCode::StreamClosed, "Stream is closed"};
        // 隐式构造 Expected(Error)
    }
    return 123;  // 隐式构造 Expected(uint64_t)
}
```

**按值传递 + 自动 move**：

```cpp
Expected(T value) : value_(std::move(value)), error_{} {}

// 调用时：
return stream;  // stream 是局部变量
// C++ 自动将局部变量视为右值（move）
// 等价于 return std::move(stream);
```

#### 3.6.3 使用模式对比

**传统模式**（错误码 + 输出参数）：

```cpp
// ❌ 不优雅
int create_stream(IStream** out, const Address& src, const Address& dst) {
    if (失败) return -1;
    *out = new IStream(...);
    return 0;
}

// 使用：
IStream* stream = nullptr;
int ret = create_stream(&stream, src, dst);
if (ret != 0) { 错误处理; }
```

**Expected 模式**：

```cpp
// ✓ 优雅
Expected<std::unique_ptr<IStream>> create(const Address& src, const Address& dst) {
    if (失败) return Error{ErrorCode::..., "..."};
    return std::make_unique<IStream>(...);
}

// 使用：
auto result = Factory::create(src, dst);
if (!result.ok()) {
    std::cerr << result.error().message << std::endl;
    return;
}
auto stream = result.take_value();
```

---

## 4. 扩展开发指南

### 4.1 扩展 Address 类型（5 步）

#### 步骤 1：定义 CRTP 继承类

```cpp
// 在 transfer/detail/address/ 目录创建新文件
// 例如：transfer/detail/address/http_address.h

#pragma once
#include "transfer/abstract/address.h"

namespace ucm::transfer {

struct HttpAddress : Address<HttpAddress> {
    // ...
};

}
```

#### 步骤 2：定义 TYPE 常量

```cpp
struct HttpAddress : Address<HttpAddress> {
    static constexpr const char* TYPE = "http";  // 类型名称
    // ...
};
```

#### 步骤 3：添加数据成员

```cpp
struct HttpAddress : Address<HttpAddress> {
    static constexpr const char* TYPE = "http";

    // 数据成员：根据地址类型定义
    std::string url;           // HTTP URL
    std::string host;          // 主机名
    uint16_t port = 80;        // 端口号
    bool use_ssl = false;      // 是否使用 SSL
};

// 示例使用：
HttpAddress addr;
addr.url = "https://example.com/path";
addr.host = "example.com";
addr.port = 443;
addr.use_ssl = true;
```

#### 步骤 4：实现构造函数

```cpp
struct HttpAddress : Address<HttpAddress> {
    static constexpr const char* TYPE = "http";

    std::string url;
    std::string host;
    uint16_t port = 80;
    bool use_ssl = false;

    // 构造函数：
    HttpAddress() = default;

    HttpAddress(std::string url)
        : url(std::move(url)) {
        // 解析 URL，提取 host/port/use_ssl
    }

    HttpAddress(std::string host, uint16_t port, std::string path = "/",
                bool use_ssl = false)
        : host(std::move(host)), port(port), use_ssl(use_ssl) {
        // 构建 URL
        url = (use_ssl ? "https://" : "http://") + host + ":" +
              std::to_string(port) + path;
    }
};
```

#### 完整示例代码

```cpp
// transfer/detail/address/http_address.h

#pragma once

#include <string>
#include "transfer/abstract/address.h"

namespace ucm::transfer {

struct HttpAddress : Address<HttpAddress> {
    static constexpr const char* TYPE = "http";

    std::string url;
    std::string host;
    uint16_t port = 80;
    bool use_ssl = false;
    std::map<std::string, std::string> headers;  // HTTP headers

    HttpAddress() = default;

    HttpAddress(std::string url) : url(std::move(url)) {
        // 解析 URL（简化版）
        if (url.substr(0, 8) == "https://") {
            use_ssl = true;
            url = url.substr(8);
        } else if (url.substr(0, 7) == "http://") {
            use_ssl = false;
            url = url.substr(7);
        }

        auto slash_pos = url.find('/');
        if (slash_pos != std::string::npos) {
            host = url.substr(0, slash_pos);
            path = url.substr(slash_pos);
        } else {
            host = url;
            path = "/";
        }

        auto colon_pos = host.find(':');
        if (colon_pos != std::string::npos) {
            port = std::stoi(host.substr(colon_pos + 1));
            host = host.substr(0, colon_pos);
        } else {
            port = use_ssl ? 443 : 80;
        }
    }

    HttpAddress(std::string host, uint16_t port, std::string path = "/",
                bool use_ssl = false)
        : host(std::move(host)), port(port), use_ssl(use_ssl) {
        url = (use_ssl ? "https://" : "http://") + host + ":" +
              std::to_string(port) + path;
    }

private:
    std::string path = "/";
};

}  // namespace ucm::transfer
```

### 4.2 扩展 Protocol 类型（4 步）

#### 步骤 1：定义 CRTP 继承类

```cpp
// 在 transfer/detail/protocol/ 目录创建新文件
// 例如：transfer/detail/protocol/http_protocol.h

#pragma once
#include "transfer/abstract/protocol.h"

namespace ucm::transfer {

struct HttpProtocol : Protocol<HttpProtocol> {
    // ...
};

}
```

#### 步骤 2：定义 PROTOCOL_NAME 常量

```cpp
struct HttpProtocol : Protocol<HttpProtocol> {
    static constexpr const char* PROTOCOL_NAME = "http";  // 协议名称
    // ...
};
```

#### 步骤 3：添加协议参数

```cpp
struct HttpProtocol : Protocol<HttpProtocol> {
    static constexpr const char* PROTOCOL_NAME = "http";

    // 协议参数：根据协议需求定义
    size_t buffer_size = 8 * 1024;       // 缓冲区大小
    size_t timeout_seconds = 30;         // 超时时间
    size_t max_retries = 3;              // 最大重试次数
    bool use_chunked_transfer = false;   // 是否使用分块传输
    std::string user_agent = "Transfer/1.0";  // User-Agent
};

// 示例使用：
HttpProtocol proto;
proto.buffer_size = 16 * 1024;  // 16KB
proto.timeout_seconds = 60;     // 60秒超时
proto.max_retries = 5;          // 最多重试5次
```

#### 完整示例代码

```cpp
// transfer/detail/protocol/http_protocol.h

#pragma once

#include <string>
#include "transfer/abstract/protocol.h"

namespace ucm::transfer {

struct HttpProtocol : Protocol<HttpProtocol> {
    static constexpr const char* PROTOCOL_NAME = "http";

    size_t buffer_size = 8 * 1024;
    size_t timeout_seconds = 30;
    size_t max_retries = 3;
    bool use_chunked_transfer = false;
    bool use_compression = false;
    std::string user_agent = "Transfer/1.0";
    std::string proxy;  // 代理地址（可选）

    HttpProtocol() = default;

    HttpProtocol(size_t buffer_size, size_t timeout_seconds = 30,
                 size_t max_retries = 3)
        : buffer_size(buffer_size),
          timeout_seconds(timeout_seconds),
          max_retries(max_retries) {}
};

}  // namespace ucm::transfer
```

### 4.3 实现新的 IStream（6 步）

#### 步骤 1：定义类继承 IStream

```cpp
// 在 transfer/detail/ 目录创建新文件
// 例如：transfer/detail/http_2_http_stream.cc

#pragma once

#include "transfer/abstract/stream.h"
#include "address/http_address.h"
#include "protocol/http_protocol.h"

namespace ucm::transfer {

class Http2HttpStream : public IStream {
public:
    // ...
private:
    // ...
};

}
```

#### 步骤 2：实现构造函数

```cpp
class Http2HttpStream : public IStream {
public:
    Http2HttpStream(HttpAddress src, HttpAddress dst, HttpProtocol protocol)
        : src_(std::move(src)),
          dst_(std::move(dst)),
          protocol_(protocol),
          src_wrapped_(src_),
          dst_wrapped_(dst_),
          open_(true),
          next_task_id_(1) {
        // 初始化 HTTP 连接
        // 例如：建立 HTTP session
    }

    ~Http2HttpStream() override {
        close();
    }

private:
    HttpAddress src_;
    HttpAddress dst_;
    HttpProtocol protocol_;
    AnyAddress src_wrapped_;
    AnyAddress dst_wrapped_;
    std::atomic<bool> open_;
    std::atomic<uint64_t> next_task_id_;
};
```

#### 步骤 3：实现 submit 方法

```cpp
Expected<void> submit(uint64_t src, uint64_t dst, std::size_t size) override {
    if (!src_file_.is_open() || !dst_file_.is_open()) {
        return Error{ErrorCode::StreamClosed, "File streams not open"};
    }

    if (size == 0) {
        return Error{ErrorCode::InvalidTask, "Size is 0"};
    }

    std::lock_guard<std::mutex> lock(mutex_);
    ranges_.emplace_back(src, dst, size);
    return Expected<void>();
}

Expected<void> submit(std::vector<std::tuple<uint64_t, uint64_t, std::size_t>> ranges) override {
    if (!src_file_.is_open() || !dst_file_.is_open()) {
        return Error{ErrorCode::StreamClosed, "File streams not open"};
    }

    if (ranges.empty()) {
        return Error{ErrorCode::InvalidTask, "Ranges is empty"};
    }

    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& range : ranges) {
        ranges_.push_back(range);
    }
    return Expected<void>();
}
```

#### 步骤 4：实现 synchronize 方法

```cpp
Expected<void> synchronize() override {
    if (!src_file_.is_open() || !dst_file_.is_open()) {
        return Error{ErrorCode::StreamClosed, "File streams not open"};
    }

    std::vector<std::tuple<uint64_t, uint64_t, std::size_t>> ranges;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ranges = std::move(ranges_);
        ranges_.clear();
    }

    for (const auto& range : ranges) {
        auto result = execute_range(range);
        if (!result.ok()) {
            return result.error();
        }
    }

    return Expected<void>();
}
```

#### 步骤 5：实现 execute_range 方法

```cpp
private:
Expected<void> execute_range(const std::tuple<uint64_t, uint64_t, std::size_t>& range) {
    uint64_t src_offset = std::get<0>(range);
    uint64_t dst_offset = std::get<1>(range);
    std::size_t size = std::get<2>(range);

    if (size == 0) {
        return Error{ErrorCode::InvalidTask, "Range size is 0"};
    }

    std::vector<char> buffer(protocol_.chunk_size);
    src_file_.seekg(src_offset);
    dst_file_.seekp(dst_offset);

    std::size_t remaining = size;
    while (remaining > 0 && src_file_.good() && dst_file_.good()) {
        std::size_t to_read = std::min(remaining, protocol_.chunk_size);
        src_file_.read(buffer.data(), to_read);
        std::size_t read_bytes = src_file_.gcount();

        if (read_bytes == 0) {
            break;
        }

        dst_file_.write(buffer.data(), read_bytes);
        if (!dst_file_.good()) {
            return Error{ErrorCode::DestinationWriteError, "Failed to write"};
        }

        remaining -= read_bytes;
    }

    return Expected<void>();
}
```

#### 步骤 6：添加成员变量

```cpp
private:
    HttpAddress src_;
    HttpAddress dst_;
    HttpProtocol protocol_;

    std::ifstream src_file_;
    std::ofstream dst_file_;

    std::vector<std::tuple<uint64_t, uint64_t, std::size_t>> ranges_;
    mutable std::mutex mutex_;
};
```

### 4.4 注册宏使用（2 步）

#### 步骤 1：在文件底部调用宏

```cpp
// transfer/detail/http_2_http_stream.cc

#include "transfer/abstract/registry.h"

namespace ucm::transfer {

class Http2HttpStream : public IStream {
    // ...
};

// 注册宏调用（文件底部）
REGISTER_TRANSFER_STREAM_WITH_PROTOCOL(
    Http2HttpStream,        // 实现类名
    HttpAddress,            // 源地址类型
    HttpAddress,            // 目标地址类型
    HttpProtocol            // 协议类型
)

}  // namespace ucm::transfer
```

#### 步骤 2：参数说明

```cpp
REGISTER_TRANSFER_STREAM_WITH_PROTOCOL(Impl, SrcAddr, DstAddr, Protocol)

// 参数：
// Impl:        IStream 实现类名（如 Http2HttpStream）
// SrcAddr:     源地址类型（如 HttpAddress）
// DstAddr:     目标地址类型（如 HttpAddress）
// Protocol:    协议类型（如 HttpProtocol）

// 宏展开效果：
// 1. 创建 constructor 函数：_init_Http2HttpStream_HttpProtocol
// 2. 注册键生成："http->http:http"
// 3. Creator 函数：
//    [](AnyAddress src, AnyAddress dst, AnyProtocol proto) {
//        return make_unique<Http2HttpStream>(
//            src.cast<HttpAddress>(),
//            dst.cast<HttpAddress>(),
//            proto.cast<HttpProtocol>()
//        );
//    }
```

### 4.5 CMakeLists 配置（3 步）

#### 步骤 1：target_sources 添加源文件

```cmake
# transfer/CMakeLists.txt

target_sources(ucmtransfer PRIVATE
    detail/local_file_2_local_file_sendfile.cc
    detail/http_2_http_stream.cc           # 新增
)
```

#### 步骤 2：确保链接选项正确

```cmake
# transfer/CMakeLists.txt

# 关键：--no-as-needed 选项必须保留
target_link_options(ucmtransfer INTERFACE
    "-Wl,--no-as-needed"
)

# 说明：
# 此选项确保 shared library 被链接，即使没有直接符号引用
# 这样 constructor 函数才能执行，注册才能生效
```

#### 步骤 3：完整 CMakeLists 示例

```cmake
# transfer/CMakeLists.txt

add_library(ucmtransfer SHARED)

target_include_directories(ucmtransfer PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

# 关键链接选项：
target_link_options(ucmtransfer INTERFACE
    "-Wl,--no-as-needed"
)

target_sources(ucmtransfer PRIVATE
    detail/local_file_2_local_file_sendfile.cc
    detail/http_2_http_stream.cc
)
```

### 4.6 编译与测试验证（3 步）

#### 步骤 1：构建步骤

```bash
# 标准 CMake 构建流程
mkdir -p build
cd build
cmake ..
make

# 验证 shared library 生成
ls -lh transfer/libucmtransfer.so

# 验证 constructor 符号存在
nm -C transfer/libucmtransfer.so | grep _init_
# 输出应包含：
# _init_LocalFile2LocalFileSendfileStream_SendfileProtocol
# _init_Http2HttpStream_HttpProtocol
```

#### 步骤 2：检查注册成功

```cpp
// 测试代码（可在 example.cc 中）

#include "transfer/abstract/factory.h"

int main() {
    // 检查注册是否成功
    auto protocols = Factory::list_protocols("http", "http");

    std::cout << "Registered protocols for http->http:" << std::endl;
    for (const auto& proto : protocols) {
        std::cout << "  - " << proto << std::endl;
    }

    if (Factory::is_supported("http", "http")) {
        std::cout << "http->http transfer is supported!" << std::endl;
    } else {
        std::cerr << "ERROR: Registration failed!" << std::endl;
        return 1;
    }

    return 0;
}
```

#### 步骤 3：测试调用流程

```cpp
// 完整测试流程

#include "transfer/detail/address/http_address.h"
#include "transfer/detail/protocol/http_protocol.h"
#include "transfer/abstract/factory.h"

int main() {
    // 1. 创建地址
    HttpAddress src("https://source.example.com/file.txt");
    HttpAddress dst("https://dest.example.com/upload");

    // 2. 创建协议
    HttpProtocol protocol;
    protocol.chunk_size = 16 * 1024;
    protocol.timeout_seconds = 60;

    // 3. 创建 stream
    auto result = Factory::create(
        AnyAddress(src),
        AnyAddress(dst),
        protocol
    );

    if (!result.ok()) {
        std::cerr << "Create failed: " << result.error().message << std::endl;
        return 1;
    }

    auto stream = result.take_value();

    // 4. 单个提交
    auto r1 = stream->submit(0, 0, 1024);
    if (!r1.ok()) {
        std::cerr << "Submit single failed: " << r1.error().message << std::endl;
        return 1;
    }
    std::cout << "Submitted single: 0, 0, 1024" << std::endl;

    // 5. 批量提交
    auto r2 = stream->submit({
        {1024, 0, 512},
        {1536, 0, 512}
    });
    if (!r2.ok()) {
        std::cerr << "Submit batch failed: " << r2.error().message << std::endl;
        return 1;
    }
    std::cout << "Submitted batch: {1024, 0, 512}, {1536, 0, 512}" << std::endl;

    // 6. 执行所有任务
    auto sync_result = stream->synchronize();
    if (!sync_result.ok()) {
        std::cerr << "Synchronize failed: " << sync_result.error().message << std::endl;
        return 1;
    }
    std::cout << "All tasks completed successfully" << std::endl;

    return 0;
}
```

---

## 5. 最佳实践

### 5.1 错误处理规范

#### Expected 隐式构造使用

```cpp
// ✓ 正确：使用隐式构造
Expected<std::unique_ptr<IStream>> create() {
    if (失败条件) {
        return Error{ErrorCode::..., "失败原因"};
    }

    auto stream = std::make_unique<SomeStream>();
    return stream;  // 自动 move + 隐式构造
}

// ✗ 错误：使用静态工厂方法（旧模式）
Expected<std::unique_ptr<IStream>> create() {
    if (失败) {
        return Expected<...>::fail(ErrorCode::..., "...");
    }
    return Expected<...>::ok(std::move(stream));  // 已废弃
}
```

#### Error 构造方式

```cpp
// ✓ 推荐：使用初始化列表
return Error{ErrorCode::StreamClosed, "Stream is closed"};

// ✓ 可接受：使用显式构造
Error err;
err.code = ErrorCode::StreamClosed;
err.message = "Stream is closed";
return err;

// ✗ 避免：不设置错误码
return Error{};  // 默认 ErrorCode::Success，语义不清
```

#### 失败场景处理

```cpp
// 常见失败场景：

// 1. Stream 未打开
if (!open_) {
    return Error{ErrorCode::StreamClosed, "Stream is closed"};
}

// 2. 任务不存在
if (tasks_.find(task_id) == tasks_.end()) {
    return Error{ErrorCode::TaskNotFound,
                 "Task not found: " + std::to_string(task_id)};
}

// 3. IO 失败
if (!file.good()) {
    return Error{ErrorCode::IoError, "IO operation failed", errno};
}

// 4. 传输未注册
if (!creator) {
    return Error{ErrorCode::UnsupportedTransfer,
                 "No transfer for " + src_type + "->" + dst_type};
}

// 5. 协议不支持
if (!creator) {
    return Error{ErrorCode::UnsupportedProtocol,
                 "Protocol '" + protocol + "' not supported"};
}
```

### 5.2 Include 路径规范

#### 使用项目根路径

```cpp
// ✓ 正确：使用项目根路径
#include "transfer/abstract/stream.h"
#include "transfer/abstract/registry.h"
#include "transfer/detail/address/local_file.h"

// ✗ 错误：使用相对路径
#include "../abstract/stream.h"           // ❌ 脆弱
#include "abstract/stream.h"              // ❌ 依赖当前目录
#include "../../abstract/registry.h"      // ❌ 复杂且脆弱
```

#### Include 顺序

```cpp
// ✓ 推荐：Include 顺序（由 clang-format 自动排序）
// 1. 对应头文件（如果是 .cc 文件）
#include "transfer/detail/http_2_http_stream.h"

// 2. 系统头文件 <...>（Priority 2）
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <tuple>
#include <vector>

// 3. 项目头文件 "..."（Priority 3）
#include "address/local_file.h"
#include "protocol/sendfile.h"
#include "transfer/abstract/registry.h"
#include "transfer/abstract/stream.h"
```

### 5.3 常见错误避免

#### inline static const 注册问题

```cpp
// ✗ 错误：inline static const 注册（不执行）
struct AutoRegister {
    inline static const bool registered = []() {
        Registry::instance().register_creator(...);
        return true;
    }();
};

// 原因：inline static const 可能是弱符号
// shared library 加载时可能不执行初始化

// ✓ 正确：使用 __attribute__((constructor))
REGISTER_TRANSFER_STREAM_WITH_PROTOCOL(Impl, SrcAddr, DstAddr, Protocol)
```

#### --as-needed 链接问题

```cmake
# ✗ 错误：使用 --as-needed
target_link_options(ucmtransfer INTERFACE "-Wl,--as-needed")

# 原因：--as-needed 会移除"不需要"的库
# 如果没有直接符号引用，libucmtransfer.so 可能被移除
# constructor 函数不会执行，注册失败

# ✓ 正确：使用 --no-as-needed
target_link_options(ucmtransfer INTERFACE "-Wl,--no-as-needed")
```

#### Include 路径错误

```cpp
// ✗ 错误：各种 Include 路径错误

// 错误1：相对路径
#include "../abstract/stream.h"

// 错误2：假设当前目录
#include "stream.h"  // 假设在 abstract/ 目录

// 错误3：复杂相对路径
#include "../../abstract/../abstract/stream.h"

// ✓ 正确：统一使用项目根路径
#include "transfer/abstract/stream.h"
```

---

## 6. 常见问题（FAQ）

### 6.1 注册失败排查

#### 检查清单

1. **检查 CMakeLists 配置**：
   ```bash
   # 检查是否有 --no-as-needed
   grep "no-as-needed" transfer/CMakeLists.txt

   # 检查源文件是否添加到 target_sources
   grep "your_stream.cc" transfer/CMakeLists.txt
   ```

2. **检查注册宏使用**：
   ```cpp
   // 检查位置：文件底部（namespace 内）
   // 检查参数：类名、地址类型、协议类型是否正确

   REGISTER_TRANSFER_STREAM_WITH_PROTOCOL(
       YourStream,      // ✓ 类名正确
       YourAddress,     // ✓ 地址类型正确
       YourAddress,     // ✓ 目标地址正确
       YourProtocol     // ✓ 协议正确
   )
   ```

3. **检查 shared library 加载**：
   ```bash
   # 检查 constructor 符号
   nm -C build/transfer/libucmtransfer.so | grep _init_

   # 应输出：
   # _init_YourStream_YourProtocol

   # 如果没有输出：说明宏未展开或未链接
   ```

#### 排查步骤

```bash
# 步骤1：清理重新构建
rm -rf build
mkdir build
cd build
cmake ..
make

# 步骤2：检查 shared library
ls -l transfer/libucmtransfer.so

# 步骤3：检查符号
nm -C transfer/libucmtransfer.so | grep _init_

# 步骤4：测试注册
./examples/transfer_example
# 如果输出 "Registration failed"：说明注册未执行

# 步骤5：检查链接选项
cat transfer/CMakeLists.txt | grep link_options
```

### 6.2 类型转换错误

#### bad_any_cast 原因

```cpp
// 错误示例：
FileAddress file_addr("/path");
AnyAddress any_addr(file_addr);

// 错误转换：类型不匹配
const HttpAddress& addr = any_addr.cast<HttpAddress>();
// 抛出 std::bad_any_cast

// 原因：any_addr 存储的是 FileAddress，不是 HttpAddress
// type_info_ == &typeid(FileAddress) != &typeid(HttpAddress)
```

#### 解决方法

```cpp
// ✓ 正确：类型匹配
const FileAddress& addr = any_addr.cast<FileAddress>();

// ✓ 安全：先检查类型
if (any_addr.type_name() == "http") {
    const HttpAddress& addr = any_addr.cast<HttpAddress>();
} else if (any_addr.type_name() == "local-file") {
    const FileAddress& addr = any_addr.cast<FileAddress>();
}

// ✓ 推荐：使用 Factory，让 Registry 处理类型转换
auto result = Factory::create(any_addr_src, any_addr_dst);
```

### 6.3 编译链接问题

#### 未定义符号原因

```bash
# 错误输出：
undefined reference to `ucm::transfer::Registry::instance()'

# 原因：
# 1. libucmtransfer.so 未链接
# 2. CMakeLists.txt 缺少 target_link_libraries
```

**解决方法**：

```cmake
# examples/CMakeLists.txt
target_link_libraries(transfer_example PRIVATE
    ucmtransfer  # 链接 shared library
)
```

#### Shared library 加载问题

```bash
# 错误输出：
error while loading shared libraries: libucmtransfer.so:
cannot open shared object file

# 原因：运行时找不到 libucmtransfer.so

# 解决方法：
export LD_LIBRARY_PATH=/path/to/transfer/build/transfer:$LD_LIBRARY_PATH

# 或：使用 RPATH（CMake 配置）
set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib")
```

### 6.4 Expected 使用问题

#### 返回值转换失败

```cpp
// ✗ 错误：返回类型不匹配
Expected<std::unique_ptr<IStream>> create() {
    return 123;  // ❌ 类型错误：Expected 接受 IStream，不是 int
}

// ✗ 错误：Error 类型不匹配
Expected<uint64_t> submit() {
    return Error{ErrorCode::...};  // ✓ 正确：Error 匹配
}

// ✓ 正确：类型匹配
Expected<uint64_t> submit() {
    return next_task_id_++;  // uint64_t 匹配
}

Expected<std::unique_ptr<IStream>> create() {
    return std::make_unique<SomeStream>();  // unique_ptr<IStream> 匹配
}
```

#### Error 构造错误

```cpp
// ✗ 错误：Error 构造参数缺失
return Error{ErrorCode::StreamClosed};  // 缺少 message

// ✓ 正确：提供完整参数
return Error{ErrorCode::StreamClosed, "Stream is closed"};

// ✓ 可接受：只有错误码（message 为空）
return Error{ErrorCode::StreamClosed, ""};

// ✗ 避免：默认构造（Success）
return Error{};  // ErrorCode::Success，语义不清
```

---

**文档结束**

本文档涵盖了 Transfer 框架的完整设计和实现细节，提供了从基础概念到扩展开发的全面指南。开发者可以参考本文档理解架构设计、实现自定义传输类型，并遵循最佳实践避免常见错误。
