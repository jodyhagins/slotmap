# wjh::slotmap

A C++20 header-only slot map with type-safe, bit-packed keys

## What is a Slot Map?

A slot map is an associative container where the **container generates keys** when you insert elements, unlike `std::map` where you provide the keys. Each key is a "generational index" - an index paired with a version counter - providing O(1) insertion, deletion, and lookup.

The version counter solves the "dangling reference" problem. When you erase an element and later try to access it with an old key, the version mismatch safely returns "not found" instead of accessing garbage or a different element. When a slot is reused, its version increments, invalidating all previous keys that pointed to that slot.

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
#include <wjh/slotmap/SlotMap.hpp>

struct Player {
    std::string name;
    int health;
};

// Define a key type: 16 index bits, 16 version bits, 0 user bits
using PlayerKey = wjh::SlotMapKey<16, 16, 0, Player>;
wjh::SlotMap<PlayerKey> players;

// Insert - container generates the key
auto key = players.emplace("Alice", 100);

// Access via callback
players.use(key, [](Player & p) {
    p.health -= 10;
});

// Check validity
if (players.contains(key)) {
    // Element exists
}

// Remove
players.erase(key);
// key is now invalid - contains(key) returns false
```

## Key Features

- **Header-only**: Single include, no library to link
- **Type-safe keys**: The phantom type parameter `T` prevents mixing keys from different SlotMaps at compile time
- **Configurable bit layout**: Choose how many bits for index, version, and user data (must total 32, 64, or 128)
- **Strong types throughout**: `index_type`, `version_type`, `size_type` are distinct types, not raw integers
- **Fixed capacity**: Maximum simultaneous elements = 2^IndexBits; maximum total insertions = 2^IndexBits × 2^VersionBits - 1
- **No iterators**: Access is via `use()` callback or `for_each()` - deliberate design to prevent dangling iterator bugs
- **Constexpr keys**: All key operations are constexpr
- **Null key safety**: The all-zeros key is reserved and never returned by `emplace()` for a valid object (it is returned if the slotmap is full and no more elements can be added).

## How This Implementation Differs

Compared to other slot map implementations (like the C++ standards proposal P0661 or SergeyMakeev/slot_map), this library makes different design choices.

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

**2. Fixed lifetime**

Once a slot exhausts its version bits, it's permanently dead. The map has a finite total lifetime (2^IndexBits × 2^VersionBits -1 insertions). This is intentional - it guarantees that old keys **never** accidentally refer to new data, even after billions of operations. Other implementations may wrap versions around.

With a 16-bit version field, a single slot can be reused 65,536 times before becoming permanently dead. When all slots in a slab become dead, the slab can be recycled to a new index range with reset versions.

**Note**: The very first slot, at index 0, starts with a version of 1, so it can only have 2^VersionBits -1 insertions. This is because we never want to generate a key where both the index and version are 0.

**3. Strong types, not integers**

Functions take/return `index_type`, `version_type`, etc., not `uint32_t`. This catches misuse at compile time:

```cpp
index_type idx = key.index();
version_type ver = key.version();
// Can't accidentally use idx where ver is expected
```

**4. Configurable key sizes**

Choose 32-bit, 64-bit, or 128-bit keys (128-bit requires compiler support for `__uint128_t`):

```cpp
// 32-bit key: 20 index + 12 version = 1M capacity, 4K generations
using SmallKey = wjh::SlotMapKey<20, 12, 0, MyType>;

// 64-bit key: 32 index + 32 version = 4B capacity, 4B generations
using LargeKey = wjh::SlotMapKey<32, 32, 0, MyType>;
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

If you need STL-compatible iterators, this isn't the right choice. The `for_each()` callback pattern requires a different programming style.

If you need unlimited insertions over time, the fixed lifetime may not work. Calculate your requirements: with IndexBits=20 and VersionBits=12, you get 1,048,576 slots × 4,096 generations - 1 = 4,294,967,295 total insertions over the lifetime of the container, with a limit of 1,048,576 simultaneously active objects.

If you need to store pointers or references to elements, the callback-based access requires refactoring your code. You must complete all operations on an element within the callback.

But, if you need to store pointers or references, then why are you using a slotmap?

## Installation

```bash
# Header-only - just copy or add as subdirectory
git clone https://github.com/jodyhagins/slotmap.git
# Include path: -I/path/to/slot_map/src

# Or with CMake FetchContent
include(FetchContent)
FetchContent_Declare(wjh_slotmap
    GIT_REPOSITORY https://github.com/jodyhagins/slotmap.git
    GIT_TAG main
)
FetchContent_MakeAvailable(wjh_slotmap)
target_link_libraries(your_target PRIVATE wjh::slotmap)
```

## Requirements

- C++20 compiler (GCC 10+, Clang 12+, MSVC 19.29+)
- No external dependencies for the library itself
- Tests require doctest and rapidcheck (fetched automatically by CMake)

## Building and Testing

```bash
mkdir build && cd build
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Debug \
    -DWJH_SLOTMAP_BUILD_TESTS=ON \
    -DWJH_SLOTMAP_SANITIZE=ON
cmake --build .
ctest -jN --output-on-failure
```

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
auto key = map.emplace(arg1, arg2, ...);

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

// Const access
bool found = const_map.use(key, [](MyType const & value) {
    // Read-only access
});
```

### Iteration

```cpp
// Iterate over all elements
size_t count = map.for_each([](MyKey k, MyType & v) {
    // Process each element
});

// Early exit
map.for_each([](MyType & v, wjh::slotmap::Break & brk) {
    if (some_condition) {
        brk.stop = true;
    }
});

// Value-only iteration
map.for_each([](MyType & v) {
    // Just process the value
});
```

The `for_each()` member function template supports multiple signatures:
- `void(key_type, T &, Break &)` - full access with early exit
- `void(key_type, T &)` - key and value
- `void(T &, Break &)` - value with early exit
- `void(T &)` - value only

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
    unsigned IndexBits,   // Number of bits for index (must be > 0)
    unsigned VersionBits, // Number of bits for version (must be > 0)
    unsigned UserBits,    // Number of bits for user data (can be 0)
    typename T = void     // Phantom type for type safety
>
class Key;
```

Total bits must equal 32, 64, or 128.

### Examples

```cpp
// 32-bit key: 16-bit index, 16-bit version
using Key32 = wjh::SlotMapKey<16, 16, 0, MyType>;
// Capacity: 65,536 slots
// Lifetime: 65,536 × 65,536 - 1 = 4,294,967,295 insertions

// 64-bit key with user bits
using Key64 = wjh::SlotMapKey<24, 32, 8, MyType>;
// Capacity: 16,777,216 slots
// 8 bits available for user-defined data
// Access user bits: key.user()
// Create key with user bits: key.with_user(UserType{42})

// 128-bit key (requires __uint128_t)
using Key128 = wjh::SlotMapKey<64, 64, 0, MyType>;
// Capacity: 18,446,744,073,709,551,616 slots
```

### User Bits

User bits allow storing additional metadata in the key itself:

```cpp
using MyKey = wjh::SlotMapKey<20, 8, 4, Entity>;
auto key = entities.emplace(/*...*/);

// Set user bits (creates new key with same index/version)
auto tagged = key.with_user(MyKey::user_type{7});

// Read user bits
auto user_data = tagged.user();  // Returns MyKey::user_type
```

User bits travel with the key and are not validated by the container. They're useful for flags, priorities, or small metadata that you want to associate with the key.

## Null Key Handling

The all-zeros key (index=0, version=0, user=0) is reserved as the null key:

```cpp
auto null_key = MyKey::null();
assert(null_key.is_null());

// The container only returns the null key if there is no room for a new item.
auto key = map.emplace(value);
assert(map.contains(key) || key.is_null());

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
| `emplace()` | Strong | If T's constructor throws, slot remains free |
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

- [DESIGN.md](/Users/jhagins/src/claude-play/slot_map/DESIGN.md) - Complete design specification and implementation notes

## License

MIT License - see LICENSE file

Copyright 2025 Jody Hagins
