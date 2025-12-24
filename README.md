# wjh::slotmap

A C++20 header-only slot map with type-safe, bit-packed keys

## What is a Slot Map?

A slot map is an associative container where the **container generates keys** when you insert elements, unlike `std::map` where you provide the keys. Each key is a "generational index" - an index paired with a version counter - providing O(1) insertion, deletion, and lookup.

The version counter solves the "dangling reference" problem. When you erase an element and later try to access it with an old key, the version mismatch safely returns "not found" instead of accessing garbage or a different element. When a slot is reused, its version increments, invalidating all previous keys which pointed to that slot.

Slot maps are commonly used in game engines (entity-component systems), resource managers, and any scenario requiring stable handles to objects that may be created and destroyed frequently.

```
Key = [user bits | version | index]
       └─────────────┴─────────┘
                     ↓
SlotMap storage (slabs of slots):
  slot[0]: version=3, value or free-link
  slot[1]: version=1, value or free-link
  slot[2]: version=0, value or free-link
  ...
```

## Quick Example

```cpp
#include <wjh/slotmap.hpp>
using namespace wjh::slotmap;

struct Player {
    std::string name;
    int health;
};

// The map will have a key type with 17 index bits, 15 version bits, 0 user bits
SlotMap<Player, IndexBits(17), VersionBits(15)> players;

// Insert - container generates the key (throws if capacity exhausted)
auto key = players.emplace("Alice", 100);

// Access via callback
players.use(key, [](Player & p) {
    p.health -= 10;
});

// Check validity
assert(players.contains(key));

// Remove
players.erase(key);
// key is now invalid - contains(key) returns false
assert(not players.contains(key));
```

## Key Features

- **Header-only**: Single include, no library to link
- **Type-safe keys**: The slotmap value type is used as a phantom type parameter in the key type to prevent mixing keys from different SlotMaps at compile time
- **Configurable bit layout**: Choose how many bits for index, version, and user data (must total 16, 32, 64, or 128)
- **Strong types throughout**: `index_type`, `version_type`, `size_type` are distinct types, not raw integers
- **Fixed capacity**: Maximum simultaneous elements = 2^IndexBits; maximum total insertions = 2^IndexBits × 2^VersionBits - 1
- **No iterators**: Access is via `use()` callback or `for_each()` - deliberate design to prevent dangling iterator/reference/pointer bugs
- **Null key safety**: The all-zeros key is reserved and never returned by `emplace()`. The null key can be obtained from `try_emplace()` when capacity is exhausted.

## How This Implementation Differs

Compared to other slot map implementations (like the C++ standards proposal P0661 or SergeyMakeev/slot_map), this library makes different design choices.

They are not necessarily better.
They are different.

### Opinionated Design Choices

**1. No iterators by design**

Most slot maps provide iterators. This one deliberately omits them. Iterators can dangle, be invalidated, and create subtle bugs. Instead, use `for_each()` with a callback, or `use()` for single-element access. This forces explicit, safe access patterns:

```cpp
// Instead of this (not possible):
for (auto & player : players) {
    player.health = 100;
}

// Use this (safe):
players.for_each([](Player & p) {
    p.health = 100;
});
```

The standard range loop is nice.
If you ask nicely, I may add support for a use case like that without exposing iterators in an easy to access manner.

**2. Fixed lifetime**

Once a slot exhausts its version bits, it's permanently dead. The map has a finite total lifetime (2^IndexBits × 2^VersionBits -1 insertions). This is intentional - it guarantees that old keys **never** accidentally refer to new data, even after billions of operations. Other implementations may wrap versions around.

With a 16-bit version field, a single slot can be reused 65,536 times before becoming permanently dead.

The number of version bits is completely under user control, and on a system that supports 128-bit integrals, the version can be set pretty large.

**Note**: The very first slot, at index 0, starts with a version of 1, so it can only have 2^VersionBits -1 insertions. This is because we never want to generate a key where both the index and version are 0.

**3. Strong types, not integers**

Functions take/return `index_type`, `version_type`, etc., not `uint32_t`. This catches misuse at compile time:

```cpp
index_type idx = key.index();
version_type ver = key.version();
// Can't accidentally use idx where ver is expected
```

**4. Configurable key sizes**

Choose 16-bit, 32-bit, 64-bit, or 128-bit keys (128-bit requires compiler support for `__uint128_t`):

```cpp
// 16-bit key: 8 index + 8 version = 256 capacity, 256 generations
using TinyKey = wjh::SlotMapKey<MyType, 8, 8>;

// 32-bit key: 20 index + 12 version = 1M capacity, 4K generations
using SmallKey = wjh::SlotMapKey<MyType, 20, 12>;

// 64-bit key: 32 index + 32 version = 4B capacity, 4B generations
using LargeKey = wjh::SlotMapKey<MyType, 32, 32>;
```

**5. Slab-based allocation**

Memory is allocated in fixed-size slabs. Each slab is a contiguous array of slots plus metadata. Exhausted slabs (all slots dead) can be recycled to new index ranges. This provides predictable memory behavior and avoids per-element allocations.

**6. No direct element access**

You can't directly get a `T*` or `T&` that outlives the `use()` call. This prevents dangling pointers:

```cpp
// Not possible (by design):
Player* p = players.get(key);  // No such access
p->health = 0;

// Required pattern (safe):
players.use(key, [](Player & p) {
    p.health = 0;  // Reference only valid in this scope
});
```

### Trade-offs to Consider

If you need STL-compatible iterators, this isn't the slotmap for you. The `for_each()` callback pattern requires a different programming style.

If you need unlimited insertions over time, the fixed lifetime may not work. Calculate your requirements: with IndexBits=20 and VersionBits=12, you get 1,048,576 slots × 4,096 generations - 1 = 4,294,967,295 total insertions over the lifetime of the container, with a limit of 1,048,576 simultaneously active objects.

If you need to store pointers or references to elements, the callback-based access requires refactoring your code. You must complete all operations on an element within the callback.

But, if you need to store pointers or references, then why are you using a slotmap?

I've heard slotmap keys referred to as safe pointers.
I didn't say that.
I heard it.
From a friend.
Yeah, that's the ticket.
From a friend.


## Installation

This is a header-only library requiring C++20. Choose your preferred integration method:

### Method 1: CMake FetchContent (Recommended)

```cmake
include(FetchContent)
FetchContent_Declare(wjh_slotmap
    GIT_REPOSITORY https://github.com/jodyhagins/slotmap.git
    GIT_TAG v0.1.0  # or main for latest
)
FetchContent_MakeAvailable(wjh_slotmap)
target_link_libraries(your_target PRIVATE wjh::slotmap)
```

This automatically:
- Provides C++20 as a compile feature requirement
- Sets up include directories for all headers (including the generated version header)
- Works with Debug builds (`WJH_SLOTMAP_DEBUG_MODE` enabled automatically)

### Method 2: System Installation

```bash
# Configure and build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

# Install to system (default: /usr/local)
cmake --install build

# Or install to custom prefix
cmake --install build --prefix /opt/local
```

Then in your CMakeLists.txt:

```cmake
find_package(wjh_slotmap REQUIRED)  # Uses SameMajorVersion compatibility
target_link_libraries(your_target PRIVATE wjh::slotmap)
```

### Method 3: Direct Include

```bash
git clone https://github.com/jodyhagins/slotmap.git
# Add to compiler flags: -I/path/to/slot_map/src -std=c++20
```

**Note**: With direct include, the version header (`<wjh/slotmap/version.hpp>`) is not available since it's generated by CMake. The umbrella header will still work but won't include version information.

In your code:

```cpp
#include <wjh/slotmap.hpp>  // Includes all public headers + version info
// Or include specific headers:
#include <wjh/slotmap/SlotMap.hpp>
#include <wjh/slotmap/Key.hpp>
```

### CMake Options

When building the library directly (as top-level project), these options are available:

| Option | Default | Description |
|--------|---------|-------------|
| `WJH_SLOTMAP_BUILD_TESTS` | `ON` | Build unit tests |
| `WJH_SLOTMAP_SANITIZE` | `ON` | Enable address sanitizer (Debug builds) |
| `WJH_SLOTMAP_BUILD_BENCHMARKS` | `OFF` | Build benchmarks (Release only) |
| `WJH_SLOTMAP_ENABLE_INLINE_NAMESPACE` | `OFF` | Enable ABI versioning via inline namespace |
| `WJH_SLOTMAP_INLINE_NAMESPACE_NAME` | `v{major}_{minor}` | Inline namespace name |

When using via FetchContent, tests and benchmarks are automatically disabled.

## Requirements

- C++20 compiler (GCC 10+, Clang 12+)
- No external dependencies for the library itself
- Tests require doctest and rapidcheck (fetched automatically by CMake)
- 128-bit keys require `__uint128_t` support (GCC/Clang on 64-bit platforms)

**Note:** MSVC is not currently tested or supported.

## Building and Testing

```bash
mkdir build && cd build
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Debug \
    -DWJH_SLOTMAP_BUILD_TESTS=ON \
    -DWJH_SLOTMAP_SANITIZE=ON
cmake --build .
ctest -jN --output-on-failure
```

### ABI Versioning (Inline Namespaces)

The library supports optional inline namespace versioning for ABI safety. When enabled, all symbols include a version identifier in their mangled names, catching version mismatches at link time instead of runtime.

**Default (disabled):**
```cpp
#include <wjh/slotmap.hpp>
// Symbols: wjh::slotmap::Key, wjh::slotmap::SlotMap
```

**Enabled via CMake:**
```bash
cmake -DWJH_SLOTMAP_ENABLE_INLINE_NAMESPACE=ON \
      -DWJH_SLOTMAP_INLINE_NAMESPACE_NAME=v1 ..
# Symbols: wjh::slotmap::v1::Key (but wjh::slotmap::Key still works)
```

**Enabled via preprocessor (before any includes):**
```cpp
#define WJH_SLOTMAP_USE_INLINE_NAMESPACE 1
#define WJH_SLOTMAP_INLINE_NAMESPACE_NAME v1
#include <wjh/slotmap.hpp>
```

When enabled, user code remains unchanged - `wjh::slotmap::Key` resolves transparently to `wjh::slotmap::v1::Key`. The version only appears in mangled symbol names, causing linker errors if mismatched versions are combined.

This is primarily useful for binary distribution or complex dependency scenarios. For typical header-only usage where everything recompiles together, it's unnecessary overhead.

### Debug Mode

The library includes debug-mode assertions that help catch misuse during development:

- **Debug builds** (`CMAKE_BUILD_TYPE=Debug`): Automatically enables `WJH_SLOTMAP_DEBUG_MODE`, which adds runtime checks in the internal `Slot` class to detect accessing free slots or double-free bugs.
- **Release builds** (`CMAKE_BUILD_TYPE=Release`): Debug mode is disabled for zero overhead.

The debug checks are only active when there's spare room in the version storage bytes. You can manually override this by defining or undefining `WJH_SLOTMAP_DEBUG_MODE` before including the library headers.

## API Overview

### Construction

```cpp
// Default constructor (chooses slab size based on key configuration)
wjh::SlotMap<MyKey> map;

// Explicit number of slots per slab (must be power of 2)
wjh::SlotMap<MyKey> map(1024);
```

### Insertion and Removal

```cpp
// Emplace (forwards arguments to T's constructor)
// Throws std::length_error if capacity exhausted
auto key = map.emplace(arg1, arg2, ...);

// Try emplace (returns null key if capacity exhausted, does not throw)
auto key = map.try_emplace(arg1, arg2, ...);
if (key.is_null()) {
    // Handle capacity exhaustion
}

// Erase (returns true if element was found and erased)
bool erased = map.erase(key);

// Pop (returns std::optional<T>, moving the value out)
auto value = map.pop(key);  // requires T to be move constructible
```

### Access

```cpp
// Check existence
bool exists = map.contains(key);

// Access with callback (returns true if element found)
bool found = map.use(key, [](MyType & value) {
    // Modify value here
});

// Access with key parameter
map.use(key, [](MyKey k, MyType & value) {
    // Access both key and value
});

// Access with erase option
map.use(key, [](MyType & value, wjh::slotmap::Options & opts) {
    if (should_remove(value)) {
        opts.erase = true;  // Element erased after callback
    }
});

// Full signature with key, value, and options
map.use(key, [](MyKey k, MyType & value, wjh::slotmap::Options & opts) {
    process(k, value);
    if (needs_removal(value)) {
        opts.erase = true;  // Conditional erase
    }
});

// Const access (read-only)
bool found = const_map.use(key, [](MyType const & value) {
    // Read-only access
});

// Const access with key parameter
const_map.use(key, [](MyKey k, MyType const & value) {
    // Read-only access to both key and value
});
```

### Iteration

```cpp
// Iterate over all elements
size_t count = map.for_each([](MyKey k, MyType & v) {
    // Process each element
});

// Early exit via Options
map.for_each([](MyType & v, wjh::slotmap::Options & opts) {
    if (some_condition) {
        opts.stop = true;
    }
});

// Early exit via bool return (simpler syntax)
map.for_each([](MyType & v) {
    // Return false to stop, true to continue
    return v.health > 0;
});

// Value-only iteration
map.for_each([](MyType & v) {
    // Just process the value
});
```

The `for_each()` member function template supports multiple signatures. Callbacks can return `void` or `bool`:
- `void|bool (key_type, T &, Options &)` - full access with early exit
- `void|bool (key_type, T &)` - key and value
- `void|bool (T &, Options &)` - value with early exit
- `void|bool (T &)` - value only

For `bool`-returning callbacks, return `false` to stop iteration early, or `true` to continue. This is equivalent to setting `opts.stop = true` but with simpler syntax.

Const overloads use `T const &`.

### Capacity

```cpp
// Query
bool empty = map.is_empty();
auto count = map.size();

// Reserve space (avoids allocations during hot path)
map.reserve(1000);
```

### Bulk Operations

```cpp
// Clear all elements (keeps memory allocated, increments all versions)
map.clear();

// Reset to initial state (deallocates all memory)
map.reset();

// Swap with another map
map.swap(other_map);
```

### Copy and Move

```cpp
// Copy (requires T to be copy constructible)
wjh::SlotMap<MyKey> copy = map;

// Move
wjh::SlotMap<MyKey> moved = std::move(map);
```

## Key Configuration

The `Key` template takes four parameters:

```cpp
template <
    typename T,           // Type of mapped value
    unsigned IndexBits,   // Number of bits for index (must be > 0)
    unsigned VersionBits, // Number of bits for version (must be > 0)
    unsigned UserBits = 0 // Number of bits for user data (can be 0)
>
class Key;
```

Total bits must equal 16, 32, 64, or 128.

### Examples

```cpp
// 32-bit key: 16-bit index, 16-bit version
using Key32 = wjh::SlotMapKey<MyType, 16, 16>;
// Capacity: 65,536 slots
// Lifetime: 65,536 × 65,536 - 1 = 4,294,967,295 insertions

// 64-bit key with user bits
using Key64 = wjh::SlotMapKey<MyType, 24, 32, 8>;
// Capacity: 16,777,216 slots
// 8 bits available for user-defined data
// Access user bits: key.user()
// Create key with user bits: key.with_user(UserType{42})

// 128-bit key (requires __uint128_t)
using Key128 = wjh::SlotMapKey<MyType, 64, 64>;
// Capacity: 18,446,744,073,709,551,616 slots
```

### User Bits

User bits allow storing additional metadata in the key itself:

```cpp
using MyKey = wjh::SlotMapKey<Entity, 20, 8, 4>;
auto key = entities.emplace(/*...*/);

// Set user bits (creates new key with same index/version)
auto tagged = key.with_user(MyKey::user_type{7});

// Read user bits
auto user_data = tagged.user();  // Returns MyKey::user_type
```

User bits travel with the key and are not validated by the container. They're useful for flags, priorities, or small metadata that you want to associate with the key.

#### Default User Bits

By default, keys returned by `emplace()` and `try_emplace()` have user bits set to 0. You can configure a different default:

```cpp
using namespace wjh::slotmap;

// Keys will have user bits set to 0x7 by default
using MyMap = SlotMap<Entity, IndexBits(20), VersionBits(8), UserBits(4),
                      DefaultUserBits(0x7)>;

MyMap entities;
auto key = entities.emplace(/*...*/);
assert(key.user().value == 0x7);  // Default user bits applied
```

The default value is automatically masked to fit within the configured user bits count.

## Null Key Handling

The all-zeros key (index=0, version=0, user=0) is reserved as the null key:

```cpp
auto null_key = MyKey::null();
assert(null_key.is_null());

// emplace() throws if capacity exhausted
try {
    auto key = map.emplace(value);
    assert(map.contains(key));  // Always valid if no exception
} catch (std::length_error const &) {
    // Handle capacity exhaustion
}

// try_emplace() returns null key if capacity exhausted (non-throwing)
auto key = map.try_emplace(value);
if (key.is_null()) {
    // Handle capacity exhaustion gracefully
} else {
    assert(map.contains(key));
}

// Operations on null keys are safe (return false/nullopt)
assert(not map.contains(null_key));
assert(not map.erase(null_key));
```

This is implemented by initializing slot 0 of the first slab to version 1, ensuring that the first allocation from slot 0 will have version 1, not 0.

## Lifetime and Recycling

A slot has a finite lifetime determined by the version bits. With N version bits, a slot can be used 2^N times before becoming permanently dead (except the very first slot which can only be used 2^N -1 times):

```cpp
// 4-bit version: slot can be used 16 times
// Versions: 0, 1, 2, ..., 14, 15
// After 16th erase, slot becomes DEAD (version cannot increment beyond 15)
```

When all slots in a slab become dead, the slab can be **recycled**: it's moved to a new index range with all versions reset to 0. This is an optimization, and does not extend the number of indexes that can be used.

Recycling is automatic and transparent. It only fails if the index space is completely exhausted (next_slab_base_index >= 2^IndexBits).

## Exception Safety

| Operation | Exception Safety | Notes |
|-----------|------------------|-------|
| `emplace()` | Strong | If T's constructor throws, slot remains free; throws std::length_error if capacity exhausted |
| `try_emplace()` | Strong | If T's constructor throws, slot remains free; returns null key if capacity exhausted (no throw) |
| `erase()` | No-throw | Assumes T's destructor doesn't throw |
| `pop()` | Strong | If T is nothrow move constructible |
| `use()` | Basic | Container state unchanged if callback throws |
| `for_each()` | Basic | Container state unchanged if callback throws |
| `clear()` | No-throw | Assumes T's destructor doesn't throw |
| `reset()` | No-throw | - |
| Copy constructor | Strong | If any T copy throws, no resources leak |
| Copy assignment | Strong | Uses copy-and-swap |
| Move constructor | No-throw | - |
| Move assignment | No-throw | - |

## Performance Characteristics

| Operation | Complexity | Notes |
|-----------|------------|-------|
| `emplace()` | O(1) amortized | May allocate new slab |
| `erase()` | O(1) | May trigger slab recycling |
| `use()` | O(1) | Direct array access |
| `contains()` | O(1) | Version check + bitmap lookup |
| `for_each()` | O(capacity) | Iterates all slots, checks alive bitmap |
| `clear()` | O(capacity) | Destroys all alive elements |
| `reset()` | O(slabs) | Deallocates all slabs |
| `reserve()` | O(slabs) | Pre-allocates slabs |

Memory overhead per slab:
- Metadata: dead count, slots per slab count
- Alive bitmap: ceil(slots_per_slab / 8) bytes
- Each slot: sizeof(T) or sizeof(size_type), whichever is larger, plus sizeof(version_type)

## Documentation

- [docs/DESIGN.md](docs/DESIGN.md) - Complete design specification and implementation notes
- [docs/API.md](docs/API.md) - Documentation about using the API
- [docs/DEVELOPER.md](docs/DEVELOPER.md) - Documentation aimed at a slotmap developer or maintainer, with a bit more information on design and implementation details that matter to working with the code.

## License

MIT License - see LICENSE file

Copyright 2025 Jody Hagins
