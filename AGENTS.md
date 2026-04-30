# AGENTS.md

## Build Commands

```bash
# Standard build
mkdir -p build && cd build && cmake .. && make

# Run example
./build/examples/transfer_example
```

## Architecture

### Registration Pattern (Critical)

Uses `__attribute__((constructor))` in macros, NOT `inline static const`. See `transfer/abstract/registry.h:109-133`.

```cpp
REGISTER_TRANSFER_STREAM_WITH_PROTOCOL(Impl, SrcAddr, DstAddr, Protocol)
```

This creates a constructor function `_init_##Impl##_##Protocol()` that runs at shared library load time.

### Link Requirement (Critical)

`transfer/CMakeLists.txt:31-33` propagates `-Wl,--no-as-needed` via INTERFACE. This ensures:
- Shared library is linked even without direct symbol references
- Constructor functions execute at load time
- Registry is populated before any Factory::create() call

Do NOT remove this option or use `--as-needed`.

### Registry Key Format

Keys are `"src_type->dst_type:protocol_name"`. See `transfer/abstract/registry.h:92-96`.

### Lock Optimization Pattern

In `synchronize()` methods, tasks are extracted from the map inside the lock, then executed outside the lock. See `transfer/detail/local_file_2_local_file_sendfile.cc:113-127`.

```cpp
// Extract tasks (short lock)
std::vector<...> tasks;
{
    std::lock_guard lock(mutex_);
    // move tasks out
    tasks_.clear();
}
// Execute IO (no lock)
for (auto& ti : tasks) { execute_task(ti); }
```

## Code Style

`.clang-format` uses Google base with:
- IndentWidth: 4
- ColumnLimit: 100
- IncludeBlocks: Merge (system headers first, then project headers)
- AfterFunction: true (brace on new line after function signature)

Run formatter on all C++ changes:
```bash
find . -name "*.h" -o -name "*.cc" | xargs clang-format -i
```

## Namespace

All code in `ucm::transfer`.

## Directory Structure

- `transfer/abstract/` - Interfaces: IStream, Registry, Factory, Address, Protocol, Error
- `transfer/detail/` - Implementations: address/, protocol/, *.cc files
- `examples/` - Usage examples

## Adding New Transfer Implementation

1. Create `.cc` file in `transfer/detail/`
2. Add to `transfer/CMakeLists.txt` target_sources
3. Use registration macro at bottom of file
4. Include paths use `transfer/abstract/...` from project root

## Common Mistakes

- Using `inline static const` for registration (won't execute in shared lib)
- Removing or overriding `--no-as-needed` link option
- Executing IO inside mutex lock (blocks other threads)
- Wrong include paths (use `transfer/abstract/stream.h`, not relative)