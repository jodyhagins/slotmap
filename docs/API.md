# wjh::slotmap API Reference

Complete API reference for `wjh::slotmap`. For a conceptual overview, see the [README](../README.md).

## Table of Contents

- [Headers and Namespaces](#headers-and-namespaces)
- [Key Types](#key-types)
- [Traits](#traits)
- [SlotMap Class](#slotmap-class)
- [Strong Types](#strong-types)
- [Capacity and Lifetime Limits](#capacity-and-lifetime-limits)
- [Thread Safety](#thread-safety)
- [Exception Safety](#exception-safety)
- [Common Patterns](#common-patterns)

---

## Headers and Namespaces

```cpp
#include <wjh/slotmap/SlotMap.hpp>  // Includes everything

namespace wjh::slotmap { /* primary namespace */ }
namespace wjh { /* convenience aliases */ }
```

---

## Key Types

### Key Template

```cpp
template <typename T, IndexBits I, VersionBits V, UserBits U = UserBits{0}>
class Key;
```

A type-safe, bit-packed key. `T` is a phantom type for compile-time safety—keys with different `T` are incompatible types.

**Template Parameters:**
- `I` (IndexBits) - Bits for slot index. Determines max capacity: 2^I. Must be > 0 and < 64.
- `V` (VersionBits) - Bits for version counter. Determines slot reuse count: 2^V. Must be > 0.
- `U` (UserBits) - Bits for user-defined data (default 0).

Total bits must equal 16, 32, 64, or 128. Use strong types: `IndexBits(16)`, `VersionBits(16)`, `UserBits(8)`.
Literals also available in `wjh::slotmap::literals`: `16_ib`, `16_vb`, `8_ub`.

**Bit Layout:** `[user][version][index]` from MSB to LSB.

```cpp
// 32-bit key: 65K capacity, 65K reuses per slot
using PlayerKey = wjh::SlotMapKey<Player, IndexBits(16), VersionBits(16)>;

// 64-bit key with user data
using EntityKey = wjh::SlotMapKey<Entity, IndexBits(20), VersionBits(20), UserBits(24)>;
```

### Key Member Types

```cpp
using value_type   = /* uint16_t, uint32_t, uint64_t, or __uint128 */;
using tag_type     = T;                    // The phantom type (also the stored element type)
using index_type   = /* strong type */;    // IndexBits bits
using version_type = /* strong type */;    // VersionBits bits
using user_type    = /* strong type */;    // UserBits bits
using size_type    = /* strong type */;    // IndexBits+1 bits (for counts/sentinels)
```

### Key Constructors

```cpp
constexpr Key();  // Null key (all bits zero)
explicit constexpr Key(index_type, version_type, user_type) noexcept;
explicit constexpr Key(index_type, version_type) noexcept;  // user = 0
```

Users typically don't construct keys directly—they come from `SlotMap::emplace()`.

### Key Static Members

```cpp
static constexpr unsigned index_bits;
static constexpr unsigned version_bits;
static constexpr unsigned user_bits;

[[nodiscard]] static constexpr Key null() noexcept;  // Returns null key
```

### Key Member Functions

```cpp
[[nodiscard]] constexpr index_type   index() const noexcept;
[[nodiscard]] constexpr version_type version() const noexcept;
[[nodiscard]] constexpr user_type    user() const noexcept;
[[nodiscard]] constexpr value_type   to_underlying() const noexcept;  // Raw bits
[[nodiscard]] constexpr Key with_user(user_type) const noexcept;      // New key, same slot
[[nodiscard]] constexpr bool is_null() const noexcept;
[[nodiscard]] explicit constexpr operator bool() const noexcept;      // true if not null
[[nodiscard]] constexpr std::size_t hash() const noexcept;            // Constexpr hash
[[nodiscard]] constexpr bool identifies_same_object(Key const &) const noexcept;
```

`identifies_same_object()` compares only index and version, ignoring user bits. Two keys with different user bits but same index/version access the same element.

### Key Operators

```cpp
friend constexpr bool operator==(Key, Key) noexcept;  // Compares all bits
friend constexpr auto operator<=>(Key, Key) noexcept; // Total ordering
friend constexpr value_type to_underlying(Key) noexcept;  // ADL-findable
```

`std::hash<Key>` is specialized and delegates to `key.hash()`.

### TrivialKey Alias

```cpp
template<typename T, IndexBits I, VersionBits V, UserBits U = UserBits{0}>
using TrivialKey = Key<detail::Trivial<T>, I, V, U>;
```

Makes the key trivially default constructible (implicit lifetime type for shared memory use). The default constructor is private to prevent accidental uninitialized keys.

### is_key Trait

```cpp
template<typename T> inline constexpr bool is_key_v;
template<typename T> concept KeyC = is_key_v<T>;
```

---

## Traits

Advanced configuration for `SlotMap` behavior.

```cpp
template <KeyC KeyT, SlotsPerSlab nslots, UseAliveBitForLookup alive_bit,
          DefaultUserBits default_user = DefaultUserBits{0}>
struct Traits;
```

**Note:** The `Traits` template requires explicit values for `nslots` and `alive_bit`. Default values come through the `SlotMap` alias template (see below).

### SlotsPerSlab

```cpp
enum class SlotsPerSlab : std::size_t {
    Dynamic = 0,     // Set via constructor (defaults to Default)
    Default = 4096,
    All = size_t(-1) // Single slab with all slots
};
// Custom: SlotsPerSlab(1024), SlotsPerSlab(16384)
```

### UseAliveBitForLookup

```cpp
enum class UseAliveBitForLookup : bool { No = false, Yes = true };
```

`Yes` (default): Faster lookups via alive-bit in version field. Ignored if version_bits isn't a power of 2.

### DefaultUserBits

```cpp
enum class DefaultUserBits : std::size_t {};
```

Default user bits for keys from `emplace()`/`try_emplace()`. Automatically masked to fit.

---

## SlotMap Class

### Template Declaration

The `SlotMap` alias template is flexible and accepts multiple forms:

```cpp
// Form 1: Traits type
SlotMap<MyTraits>

// Form 2: Key type with optional Traits parameters
SlotMap<MyKey>
SlotMap<MyKey, SlotsPerSlab::All>

// Form 3: Value type with bit specifications (parameters can be in any order!)
SlotMap<int, IndexBits(16), VersionBits(16)>
SlotMap<Player, VersionBits(8), IndexBits(20), SlotsPerSlab::All>
```

Unspecified bits default to 0. Unspecified `SlotsPerSlab` defaults to `Dynamic`. Unspecified `UseAliveBitForLookup` defaults to `Yes`.

```cpp
// These are all equivalent:
SlotMap<Key<int, IndexBits(10), VersionBits(6)>, SlotsPerSlab::All>
SlotMap<int, IndexBits(10), VersionBits(6), SlotsPerSlab::All>
SlotMap<int, VersionBits(6), SlotsPerSlab::All, IndexBits(10)>  // Any order!
```

### Member Types

```cpp
using key_type        = KeyT;
using mapped_type     = typename key_type::tag_type;  // The stored element type
using index_type      = typename key_type::index_type;
using version_type    = typename key_type::version_type;
using user_type       = typename key_type::user_type;
using size_type       = typename key_type::size_type;
using statistics_type = Statistics;
```

### Constants

```cpp
static constexpr size_type max_slots;              // 2^IndexBits
static constexpr size_type max_simultaneous_objects = max_slots;
static constexpr TotalSize max_total_objects;      // 2^IndexBits × 2^VersionBits - 1
```

### Constructors

```cpp
SlotMap();                          // Auto-sized slabs based on index space
explicit SlotMap(size_type slots);  // Custom slab size (power of 2, multi-slab only)
```

### Copy and Move

```cpp
SlotMap(SlotMap const &) requires std::is_copy_constructible_v<mapped_type>;
SlotMap & operator=(SlotMap const &) requires std::is_copy_constructible_v<mapped_type>;
SlotMap(SlotMap &&) noexcept;
SlotMap & operator=(SlotMap &&) noexcept;
void swap(SlotMap &) noexcept;
```

### Element Insertion

```cpp
template <typename... Args>
[[nodiscard]] key_type emplace(Args &&...);      // Throws std::length_error if full

template <typename... Args>
[[nodiscard]] key_type try_emplace(Args &&...);  // Returns null key if full
```

Both provide strong exception guarantee. `emplace()` never returns null; `try_emplace()` never throws for capacity exhaustion.

### Element Access

#### Options Struct

```cpp
struct Options {
    bool stop = false;   // for_each: stop iteration
    bool erase = false;  // Erase element after callback
};
```

#### use()

```cpp
template <typename F> [[nodiscard]] auto use(key_type key, F && func);
template <typename F> [[nodiscard]] auto use(key_type key, F && func) const;
```

Access an element by key. Invokes callback if key is valid.

**Non-const callback signatures:**
- `R(key_type, T &, Options &)`
- `R(key_type, T &)`
- `R(T &, Options &)`
- `R(T &)`

**Const callback signatures:**
- `R(key_type, T const &)`
- `R(T const &)`

**Return type:**
- If callback returns `void`: returns `bool` (true = key valid, callback invoked)
- If callback returns `R`: returns `std::optional<R>`

```cpp
// Void callback -> bool
if (map.use(key, [](int & v) { v *= 2; })) { /* found */ }

// Value-returning callback -> std::optional
auto val = map.use(key, [](int v) { return v * 2; });
if (val) { process(*val); }

// With Options for conditional erase
map.use(key, [](int & v, Options & opts) {
    if (should_remove(v)) opts.erase = true;
});
```

**Note:** `[[nodiscard]]` because you almost always care whether the key was valid.

#### contains()

```cpp
[[nodiscard]] bool contains(key_type key) const;
```

Prefer `use()` when you'll access the element anyway (avoids double lookup).

### Element Removal

```cpp
bool erase(key_type key);  // Returns true if erased

[[nodiscard]] std::optional<mapped_type> pop(key_type key)
    requires std::is_move_constructible_v<mapped_type>;  // Moves element out
```

### Iteration

```cpp
template <typename F> size_type for_each(F && func);
template <typename F> size_type for_each(F && func) const;
```

Iterates all alive elements. Returns count of elements visited.

**Callback signatures:** Same as `use()`, but return type must be `void` or `bool`.

**Early exit:** Return `false` from a bool-returning callback, or set `opts.stop = true`.

```cpp
// Value only
map.for_each([](int & v) { v *= 2; });

// With key
map.for_each([](MyKey k, int & v) { log(k, v); });

// Early exit via bool return
map.for_each([](int v) { return v < 100; });  // Stop when v >= 100

// Erase during iteration
map.for_each([](int v, Options & opts) {
    if (v < 0) opts.erase = true;
});
```

**Warning:** Calling `emplace()` during iteration is undefined behavior.

### Bulk Operations

```cpp
void clear();  // Destroys elements, keeps memory allocated
void reset();  // Destroys elements, deallocates all memory
```

### Capacity

```cpp
[[nodiscard]] bool is_empty() const noexcept;
[[nodiscard]] size_type size() const noexcept;
void reserve(size_type n);  // Pre-allocate slabs
```

### Statistics

```cpp
[[nodiscard]] Statistics statistics() const noexcept;  // O(slab_count)
```

Returns comprehensive metrics about slot usage, memory, and capacity:

```cpp
struct Statistics {
    // Configuration
    std::size_t slots_per_slab;
    std::size_t max_slots;          // 2^IndexBits
    max_objects_type max_objects;   // 2^IndexBits × 2^VersionBits - 1

    // Slot accounting
    std::size_t active_slots;       // Currently holding objects
    std::size_t free_slots;         // Available for immediate use
    std::size_t dead_slots;         // Version exhausted
    std::size_t allocated_slots;    // active + free + dead
    std::size_t unallocated_slots;

    // Lifetime metrics
    std::size_t objects_created;
    max_objects_type objects_remaining;

    // Slab and memory metrics
    std::size_t slab_count;
    std::size_t total_memory_bytes;

    // Derived ratios
    double slot_utilization;      // active / remaining
    double dead_slot_ratio;       // dead / allocated
    double lifetime_exhaustion;   // created / max
};
```

`max_objects_type` is `__uint128_t` when available, otherwise `uint64_t`.

---

## Strong Types

`index_type`, `version_type`, `user_type`, and `size_type` are distinct types, not raw integers.

```cpp
MyKey::index_type idx = key.index();
MyKey::version_type ver = key.version();

// idx = ver;           // Compile error - different types
auto raw = idx.value;   // Access underlying value
++idx;                  // Increment works
// idx + ver;           // Compile error - no cross-type arithmetic
```

This prevents bugs like accidentally swapping index and version when constructing keys.

---

## Capacity and Lifetime Limits

**Max simultaneous elements:** 2^IndexBits

**Max lifetime insertions:** 2^IndexBits × 2^VersionBits - 1

| IndexBits | VersionBits | Capacity | Lifetime |
|-----------|-------------|----------|----------|
| 16 | 16 | 65,536 | ~4.3 billion |
| 20 | 12 | 1,048,576 | ~4.3 billion |
| 8 | 8 | 256 | 65,535 |

**Capacity exhaustion:** `emplace()` throws `std::length_error`; `try_emplace()` returns null key.

**Version exhaustion:** When a slot reaches max version, it becomes "dead" and won't be reused. When all slots in a slab die, the slab can be recycled to a new index range (an optimization, not a capacity increase).

---

## Thread Safety

**SlotMap is NOT thread-safe.**

- **Multiple readers OK:** Concurrent const operations (`use`, `contains`, `for_each`, `size`)
- **Single writer:** Any mutation requires exclusive access

Use external synchronization (mutex, shared_mutex) for concurrent access.

---

## Exception Safety

| Operation | Guarantee | Notes |
|-----------|-----------|-------|
| `emplace()` | Strong | Throws `std::length_error` if capacity exhausted |
| `try_emplace()` | Strong | Returns null key if capacity exhausted |
| `erase()`, `clear()`, `reset()` | No-throw | Assumes noexcept destructors |
| `pop()` | Strong | If move is noexcept |
| Copy ctor/assign | Strong | Copy-and-swap |
| Move ctor/assign | No-throw | |
| `use()`, `for_each()` | Basic | If callback throws |

---

## Common Patterns

### Value Extraction

```cpp
// Extract value or default
int val = map.use(key, [](int v) { return v; }).value_or(-1);

// Extract multiple fields
auto stats = map.use(key, [](Player const & p) {
    return std::tuple{p.health, p.mana};
});
```

### Keys in Containers

Keys work with all standard containers (hashable, comparable):

```cpp
std::vector<MyKey> keys;
std::unordered_set<MyKey> active;
std::map<MyKey, std::string> names;
```

### User Bits for Metadata

```cpp
using TaggedKey = wjh::SlotMapKey<Entity, IndexBits(16), VersionBits(8), UserBits(8)>;
auto key = entities.emplace(Entity{});
auto tagged = key.with_user(TaggedKey::user_type{priority});
// key and tagged access the same element; user bits are in the key, not the map
```

### Avoiding Double Lookup

```cpp
// Less efficient
if (map.contains(key)) { map.use(key, handler); }

// Better
if (not map.use(key, handler)) { /* not found */ }
```

### Erasing During Iteration

```cpp
map.for_each([](int v, Options & opts) {
    if (v < 0) opts.erase = true;
});
```

---

For more details, see the source code in `src/wjh/slotmap/`.
