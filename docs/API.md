# wjh::slotmap API Reference

This is the complete API reference for the `wjh::slotmap` library. For a conceptual overview and getting started guide, see the [README](../README.md).

## Table of Contents

- [Headers and Namespaces](#headers-and-namespaces)
- [Key Types](#key-types)
  - [Key Template](#key-template)
  - [Key Member Types](#key-member-types)
  - [Key Constructors](#key-constructors)
  - [Key Static Members](#key-static-members)
  - [Key Member Functions](#key-member-functions)
  - [Key Operators](#key-operators)
  - [std::hash Specialization](#stdhash-specialization)
  - [TrivialKey Alias](#trivialkey-alias)
  - [is_key Trait](#is_key-trait)
- [SlotMap Class](#slotmap-class)
  - [Template Declaration](#template-declaration)
  - [Member Types](#member-types)
  - [Constants](#constants)
  - [Constructors](#constructors)
  - [Copy and Move Operations](#copy-and-move-operations)
  - [Element Insertion](#element-insertion)
  - [Element Access](#element-access)
  - [Element Removal](#element-removal)
  - [Bulk Operations](#bulk-operations)
  - [Capacity](#capacity)
- [Strong Types](#strong-types)
- [Capacity and Lifetime Limits](#capacity-and-lifetime-limits)
- [Thread Safety](#thread-safety)
- [Exception Safety](#exception-safety)
- [Common Patterns](#common-patterns)

---

## Headers and Namespaces

```cpp
#include <wjh/slotmap/SlotMap.hpp>  // Includes everything you need

// Primary namespace
namespace wjh::slotmap { /* ... */ }

// Convenience aliases in wjh namespace
namespace wjh {
    template<typename T, unsigned I, unsigned V, unsigned U = 0>
    using SlotMapKey = slotmap::Key<T, I, V, U>;

    template<typename T, unsigned I, unsigned V, unsigned U = 0>
    using TrivialSlotMapKey = slotmap::TrivialKey<T, I, V, U>;

    template<typename KeyT>
    using SlotMap = slotmap::SlotMap<KeyT>;
}
```

All public API types are in `wjh::slotmap`. Convenience aliases are provided in `wjh` for common types.

---

## Key Types

### Key Template

```cpp
template <
    typename T,
    unsigned IndexBits,
    unsigned VersionBits,
    unsigned UserBits = 0>
class Key;
```

A type-safe, bit-packed key with compile-time validation. Keys are lightweight handles that reference elements in a `SlotMap`.

**Template Parameters:**

- `T` - The value type that will be stored in a `SlotMap`. For purposes of the `Key`, this is a phantom type since it is not actually used by the `Key`. Keys with different `T` are incompatible types, preventing accidental mixing.
- `IndexBits` - Number of bits for the slot index. Determines maximum simultaneous elements (2^IndexBits). Must be > 0.
- `VersionBits` - Number of bits for the version/generation counter. Determines how many times a slot can be reused before exhaustion (2^VersionBits). Must be > 0.
- `UserBits` - Number of bits for user-defined data stored in the key. Can be 0 if no user data is needed.

**Constraints:**

`IndexBits + VersionBits + UserBits` must equal 32, 64, or 128. On platforms without `__int128`, 128-bit keys are not available.

**Bit Layout:**

Bits are packed as `[user][version][index]` from MSB to LSB. This layout ensures that index (the most frequently accessed component) is in the lowest bits for efficient extraction.

**Examples:**

```cpp
// 32-bit key: 16 index bits (65536 slots), 16 version bits, no user data
using PlayerKey = wjh::SlotMapKey<Player, 16, 16>;

// 64-bit key with user data: 20 index, 20 version, 24 user bits
using EntityKey = wjh::SlotMapKey<Entity, 20, 20, 24>;

// Different types prevent mixing even with identical bit layout
using EnemyKey = wjh::SlotMapKey<Enemy, 16, 16>;
// PlayerKey and EnemyKey are incompatible types - compile error if mixed
```

**When to use which configuration:**

- **16+16 bits (32-bit key)**: Good default for most applications. 65K simultaneous elements, 4.3 billion lifetime insertions.
- **20+20+24 bits (64-bit key)**: When you need user data in keys (e.g., entity flags, priority tags).
- **24+8+0 bits**: Lots of simultaneous elements (16M), fewer reuses per slot (256).
- **8+24+0 bits**: Few simultaneous elements (256), many reuses per slot (16M).

---

### Key Member Types

```cpp
using value_type = /* uint32_t, uint64_t, or unsigned __int128 */;
using tag_type = T;  // The phantom type parameter
using index_type = /* Strong type wrapping index bits */;
using version_type = /* Strong type wrapping version bits */;
using user_type = /* Strong type wrapping user bits */;
using size_type = /* Strong type with IndexBits+1 bits */;
```

**`value_type`**: The underlying unsigned integer type used to store the key's bits. This is `uint32_t` for 32-bit keys, `uint64_t` for 64-bit keys, or `unsigned __int128` for 128-bit keys.

**`tag_type`**: The phantom type `T`. This is also the element type stored in `SlotMap<Key<...>>`.

**`index_type`**: Strong type for the index component. Has `IndexBits` bits. Prevents accidental mixing with version or size values.

**`version_type`**: Strong type for the version component. Has `VersionBits` bits.

**`user_type`**: Strong type for the user data component. Has `UserBits` bits.

**`size_type`**: Strong type with `IndexBits + 1` bits. Can hold values from 0 to 2^IndexBits (inclusive), which is necessary for representing counts and sentinel values like "end of free list".

**Example:**

```cpp
using MyKey = wjh::SlotMapKey<int, 16, 16>;
static_assert(std::is_same_v<MyKey::value_type, uint32_t>);
static_assert(std::is_same_v<MyKey::tag_type, int>);

MyKey::index_type idx;     // Strong type, not a raw integer
MyKey::version_type ver;   // Different strong type
// idx = ver;  // Compile error - different types
```

---

### Key Constructors

```cpp
// Default constructor - initializes to null key (all bits zero)
constexpr Key();

// Construct from components with user data
explicit constexpr Key(index_type index, version_type version, user_type user) noexcept;

// Construct from components without user data (user bits set to 0)
explicit constexpr Key(index_type index, version_type version) noexcept;
```

**Default Constructor:**

Creates the null key where all bits are zero. The null key never refers to a valid element.

**Component Constructors:**

Construct a key from its component parts. The `explicit` qualifier prevents accidental implicit conversions.

**Note:** Users typically don't construct keys directly. Keys are returned by `SlotMap::emplace()` and should be treated as opaque handles. Manual construction is mainly useful for testing and advanced use cases.

**Examples:**

```cpp
using MyKey = wjh::SlotMapKey<int, 16, 16>;

// Default construction yields null key
constexpr MyKey null_key;
static_assert(null_key.is_null());

// Manual construction (rare in application code)
auto key = MyKey(
    MyKey::index_type{42},
    MyKey::version_type{1}
);
// This key has index=42, version=1, user=0

// With user data
using TaggedKey = wjh::SlotMapKey<int, 16, 8, 8>;
auto tagged = TaggedKey(
    TaggedKey::index_type{10},
    TaggedKey::version_type{5},
    TaggedKey::user_type{99}
);
```

---

### Key Static Members

```cpp
[[nodiscard]]
static constexpr Key null() noexcept;
```

**Description:**

Returns the null key (all bits zero). The null key is used to indicate "no element" or failure conditions (e.g., when `try_emplace()` fails due to capacity exhaustion).

**Returns:** A key with all bits set to zero.

**Example:**

```cpp
using MyKey = wjh::SlotMapKey<int, 16, 16>;
wjh::SlotMap<MyKey> map;

// try_emplace returns null key on capacity exhaustion
auto key = map.try_emplace(42);
if (key == MyKey::null()) {
    // Capacity exhausted - handle error
    std::cerr << "Failed to insert element\n";
} else {
    // Key is valid
}

// Equivalent check using is_null()
if (key.is_null()) {
    // Handle error
}

// emplace throws on capacity exhaustion
try {
    auto key2 = map.emplace(99);
    // Key is always valid here
} catch (std::length_error const &) {
    std::cerr << "Capacity exhausted\n";
}
```

---

### Key Member Functions

#### `index()`

```cpp
[[nodiscard]]
constexpr index_type index() const noexcept;
```

**Description:** Extracts the index component from the key.

**Returns:** The index bits as a strong `index_type`.

**Example:**

```cpp
auto key = map.emplace(value);
auto idx = key.index();  // Returns index_type
std::cout << "Index: " << idx.value << '\n';  // Access underlying value
```

---

#### `version()`

```cpp
[[nodiscard]]
constexpr version_type version() const noexcept;
```

**Description:** Extracts the version component from the key. The version is used to detect stale keys (when a slot has been reused).

**Returns:** The version bits as a strong `version_type`.

**Example:**

```cpp
auto key = map.emplace(value);
auto ver = key.version();
std::cout << "Version: " << ver.value << '\n';

// Version increments after each erase
map.erase(key);
auto key2 = map.emplace(value);
// If key2 reuses the same slot, key2.version() > key.version()
```

---

#### `user()`

```cpp
[[nodiscard]]
constexpr user_type user() const noexcept;
```

**Description:** Extracts the user data component from the key. Returns zero if `UserBits == 0`.

**Returns:** The user bits as a strong `user_type`.

**Example:**

```cpp
using TaggedKey = wjh::SlotMapKey<Entity, 16, 8, 8>;
auto key = map.emplace(entity);
auto tagged = key.with_user(TaggedKey::user_type{42});

std::cout << "User data: " << tagged.user().value << '\n';  // Prints 42
// Original key has user=0, tagged has user=42, both access same element
```

---

#### `to_underlying()`

```cpp
[[nodiscard]]
constexpr value_type to_underlying() const noexcept;
```

**Description:** Returns the raw bit representation of the key as an unsigned integer. Useful for serialization or debugging.

**Returns:** The complete key as `value_type` (uint32_t, uint64_t, or unsigned __int128).

**Example:**

```cpp
auto key = map.emplace(42);
auto bits = key.to_underlying();
// Save bits to file/network
// ...
// Restore from bits
MyKey restored;
restored = MyKey(/* reconstruct from bits */);
```

**Note:** There's also a hidden friend function `to_underlying(key)` available via ADL (Argument-Dependent Lookup).

---

#### `with_user()`

```cpp
[[nodiscard]]
constexpr Key with_user(user_type new_user) const noexcept;
```

**Description:** Creates a new key with the same index and version but different user bits. The returned key still refers to the same element.

**Parameters:**
- `new_user` - The new user data value.

**Returns:** A new key with updated user bits.

**Example:**

```cpp
using TaggedKey = wjh::SlotMapKey<Entity, 16, 8, 8>;
wjh::SlotMap<TaggedKey> map;

auto key = map.emplace(Entity{});
std::cout << key.user().value << '\n';  // 0

// Tag the key with priority
auto high_priority = key.with_user(TaggedKey::user_type{255});
auto low_priority = key.with_user(TaggedKey::user_type{1});

// All three keys refer to the same element
map.use(key, [](Entity & e) { e.name = "Original"; });
map.use(high_priority, [](Entity & e) {
    // Same element
    assert(e.name == "Original");
});

// User bits are preserved in the key, not in the map
std::cout << high_priority.user().value << '\n';  // 255
std::cout << low_priority.user().value << '\n';   // 1
```

**Use Case:** Storing transient metadata (priority, flags, generation) in keys without modifying the stored element or allocating additional memory.

---

#### `is_null()`

```cpp
[[nodiscard]]
constexpr bool is_null() const noexcept;
```

**Description:** Checks if this is the null key (all bits zero).

**Returns:** `true` if the key is null, `false` otherwise.

**Example:**

```cpp
// With emplace (throws on failure)
try {
    auto key = map.emplace(42);
    // Key is always valid here
    map.use(key, [](int & val) { val *= 2; });
} catch (std::length_error const &) {
    std::cerr << "Emplace failed - capacity exhausted\n";
}

// With try_emplace (returns null key on failure)
auto key = map.try_emplace(42);
if (key.is_null()) {
    std::cerr << "Emplace failed - capacity exhausted\n";
    return;
}

// Safe to use the key
map.use(key, [](int & val) { val *= 2; });
```

---

#### `hash()`

```cpp
[[nodiscard]]
constexpr std::size_t hash() const noexcept;
```

**Description:** Returns a constexpr hash value for the key. Uses a relatively high-quality hash function (splitmix64) with good avalanche properties.

**Returns:** Hash value as `std::size_t`.

**Note:** This hash is `constexpr`, unlike `std::hash`. The hash value is NOT guaranteed to be the same as `std::hash<value_type>` would produce, but it's compatible with `std::hash<Key>`.

**Example:**

```cpp
// Hash can be computed at compile time
using MyKey = wjh::SlotMapKey<int, 16, 16>;
constexpr auto key = MyKey(MyKey::index_type{42}, MyKey::version_type{1});
constexpr auto h = key.hash();  // Computed at compile time

// Use in unordered containers (via std::hash specialization)
std::unordered_set<MyKey> key_set;
key_set.insert(map.emplace(1));
key_set.insert(map.emplace(2));
```

---

### Key Operators

#### Equality Comparison

```cpp
[[nodiscard]]
friend constexpr bool operator==(Key const & x, Key const & y) noexcept;
```

**Description:** Compares two keys for equality by comparing their raw bits.

**Returns:** `true` if all bits are identical, `false` otherwise.

**Example:**

```cpp
auto key1 = map.emplace(1);
auto key2 = map.emplace(2);

assert(key1 != key2);  // Different slots
assert(key1 == key1);  // Same key

auto copy = key1;
assert(copy == key1);  // Bit-for-bit identical
```

---

#### Three-Way Comparison (Spaceship)

```cpp
[[nodiscard]]
friend constexpr auto operator<=>(Key const & x, Key const & y) noexcept;
```

**Description:** Three-way comparison operator for ordering keys. Compares the raw bits as unsigned integers.

**Returns:** `std::strong_ordering` result.

**Example:**

```cpp
std::vector<MyKey> keys;
keys.push_back(map.emplace(1));
keys.push_back(map.emplace(2));
keys.push_back(map.emplace(3));

// Keys are totally ordered
std::sort(keys.begin(), keys.end());

// Can use in ordered containers
std::set<MyKey> ordered_keys(keys.begin(), keys.end());
std::map<MyKey, std::string> key_to_name;
```

---

#### `to_underlying()` Hidden Friend

```cpp
[[nodiscard]]
friend constexpr value_type to_underlying(Key const & key) noexcept;
```

**Description:** Hidden friend function that extracts the raw bits via ADL (Argument-Dependent Lookup).

**Returns:** The key's raw bits as `value_type`.

**Example:**

```cpp
auto key = map.emplace(42);
auto bits = to_underlying(key);  // Found via ADL
// Equivalent to key.to_underlying()
```

---

### std::hash Specialization

```cpp
template<typename T, unsigned I, unsigned V, unsigned U>
struct std::hash<wjh::slotmap::Key<T, I, V, U>> {
    std::size_t operator()(wjh::slotmap::Key<T, I, V, U> const & key) const noexcept;
};
```

**Description:** Specialization of `std::hash` for `Key` types. Enables use in `std::unordered_map`, `std::unordered_set`, etc.

**Implementation:** Delegates to `key.hash()`.

**Example:**

```cpp
using MyKey = wjh::SlotMapKey<int, 16, 16>;
wjh::SlotMap<MyKey> map;

// Use keys in unordered containers
std::unordered_map<MyKey, std::string> key_names;
std::unordered_set<MyKey> active_keys;

auto k1 = map.emplace(1);
auto k2 = map.emplace(2);

key_names[k1] = "First";
key_names[k2] = "Second";
active_keys.insert(k1);
active_keys.insert(k2);

// Lookup by key
if (auto it = key_names.find(k1); it != key_names.end()) {
    std::cout << it->second << '\n';  // "First"
}
```

---

### TrivialKey Alias

```cpp
template<typename T, unsigned IndexBits, unsigned VersionBits, unsigned UserBits = 0>
using TrivialKey = Key<detail::Trivial<T>, IndexBits, VersionBits, UserBits>;
```

**Description:** Alias template for a `Key` that is trivially default constructible. When default-constructed, a `TrivialKey`'s bits are **uninitialized** (unlike regular `Key`, which zeroes its bits).

**When to use:** Use `TrivialKey` when you'll immediately assign a valid value and want to avoid the cost of zero-initialization. This is a micro-optimization and rarely necessary, though important for cases where you want the key to be an implicit lifetime type.

**Example:**

```cpp
using MyKey = wjh::SlotMapKey<int, 16, 16>;
using MyTrivialKey = wjh::TrivialSlotMapKey<int, 16, 16>;

static_assert(not std::is_trivially_default_constructible_v<MyKey>);
static_assert(std::is_trivially_default_constructible_v<MyTrivialKey>);

MyKey k1;  // Bits are zeroed
MyTrivialKey k2;  // Bits are uninitialized - DANGEROUS if used before assignment

// Safe usage: immediate assignment
MyTrivialKey k3;
k3 = map.emplace(42);  // Now initialized
```

**Warning:** Never use a default-constructed `TrivialKey` before assigning it a valid value. The behavior is undefined.

---

### is_key Trait

```cpp
template<typename T>
inline constexpr bool is_key_v = /* true if T is a Key instantiation */;
```

**Description:** Type trait to check if a type is a `Key` instantiation. Used internally for `static_assert` and SFINAE.

**Example:**

```cpp
using MyKey = wjh::SlotMapKey<int, 16, 16>;

static_assert(wjh::slotmap::is_key_v<MyKey>);
static_assert(not wjh::slotmap::is_key_v<int>);
static_assert(not wjh::slotmap::is_key_v<std::string>);

// Used in templates
template<typename KeyT>
void process_keys(KeyT key)
requires wjh::slotmap::is_key_v<KeyT>
{
    // Only accepts Key types
}
```

---

## SlotMap Class

### Template Declaration

```cpp
template <typename KeyT>
class SlotMap;
```

A high-performance slot map container providing O(1) insertion, deletion, and lookup using persistent unique keys.

**Template Parameters:**

- `KeyT` - Must be a `Key` instantiation (satisfies `is_key_v<KeyT>`).

The stored element type is `KeyT::tag_type`.

**Example:**

```cpp
using PlayerKey = wjh::SlotMapKey<struct Player, 16, 16>;
wjh::SlotMap<PlayerKey> players;
// Stores Player instances, accessed via PlayerKey handles

struct Enemy { int health; std::string name; };
using EnemyKey = wjh::SlotMapKey<Enemy, 16, 16>;
wjh::SlotMap<EnemyKey> enemies;
// Stores Enemy instances, accessed via EnemyKey handles
```

---

### Member Types

```cpp
using key_type = KeyT;
using mapped_type = typename key_type::tag_type;
using index_type = typename key_type::index_type;
using version_type = typename key_type::version_type;
using user_type = typename key_type::user_type;
using size_type = typename key_type::size_type;
```

**`key_type`**: The key type (same as template parameter `KeyT`).

**`mapped_type`**: The stored element type (`KeyT::tag_type`).

**`index_type`, `version_type`, `user_type`**: Strong types from the key.

**`size_type`**: Strong type for counts and sizes (has `IndexBits + 1` bits).

---

### Constants

```cpp
static constexpr size_type end_of_free_list = /* 2^IndexBits */;
```

**Description:** Sentinel value marking the end of the internal free list. This value is one past the maximum valid index (2^IndexBits), which fits in `size_type` but cannot be a valid `index_type`.

**Use Case:** Internal bookkeeping. Users don't typically need this constant, but it's useful for understanding capacity limits.

**Example:**

```cpp
using MyKey = wjh::SlotMapKey<int, 8, 8>;  // 8 index bits
wjh::SlotMap<MyKey> map;

// Maximum valid index: 255 (2^8 - 1)
// end_of_free_list: 256 (2^8)
static_assert(MyKey::index_type::mask == 255);
static_assert(map.end_of_free_list.value == 256);
```

---

### Constructors

#### Default Constructor

```cpp
SlotMap();
```

**Description:** Creates an empty SlotMap with automatic slab sizing based on the index space and value type size:

- If the entire index space fits in approximately 2MB, uses a single slab.
- Otherwise, uses 4096 slots per slab (or the largest power of 2 that fits in the index space if smaller).

**Throws:** `std::bad_alloc` if initial setup allocation fails (rare, as no slabs are allocated until first `emplace()`).

**Example:**

```cpp
using MyKey = wjh::SlotMapKey<int, 16, 16>;
wjh::SlotMap<MyKey> map;  // Default slab size chosen automatically

auto key = map.emplace(42);
// First emplace triggers slab allocation
```

---

#### Explicit Slab Size Constructor

```cpp
explicit SlotMap(size_type slots_per_slab);
```

**Description:** Constructs with an explicit slab size. Slabs are the unit of memory allocation for slots.

**Parameters:**
- `slots_per_slab` - Number of slots per slab. Must be a power of 2 and greater than 0. Cannot exceed 2^IndexBits.

**Throws:**
- `std::invalid_argument` if `slots_per_slab` is not a power of 2, is zero, or exceeds the maximum index value.
- `std::bad_alloc` if allocation fails.

**When to use explicit slab size:**

- **Testing**: Small slabs (e.g., 4, 8) to trigger edge cases like slab recycling.
- **Memory control**: Large slabs for bulk allocation, small slabs for fine-grained memory management.
- **Performance tuning**: Match slab size to cache lines or allocation patterns.

**Example:**

```cpp
using MyKey = wjh::SlotMapKey<int, 16, 16>;

// Small slabs for testing
wjh::SlotMap<MyKey> test_map(MyKey::size_type{8});

// Large slabs for bulk allocation
wjh::SlotMap<MyKey> bulk_map(MyKey::size_type{16384});

// Invalid: not a power of 2
// wjh::SlotMap<MyKey> bad_map(MyKey::size_type{100});  // Throws std::invalid_argument
```

---

### Copy and Move Operations

#### Copy Constructor

```cpp
SlotMap(SlotMap const & other)
requires std::is_copy_constructible_v<mapped_type>;
```

**Description:** Creates a deep copy of another SlotMap. All keys valid in the source will be valid in the copy.

**Parameters:**
- `other` - The SlotMap to copy from.

**Throws:**
- `std::bad_alloc` if allocation fails.
- Any exception from `mapped_type`'s copy constructor.

**Exception Safety:** Strong guarantee. If an exception is thrown, the constructed object is not created.

**Availability:** Only available if `mapped_type` is copy constructible.

**Example:**

```cpp
wjh::SlotMap<MyKey> original;
auto k1 = original.emplace(1);
auto k2 = original.emplace(2);

wjh::SlotMap<MyKey> copy(original);  // Deep copy

// Keys from original are valid in copy
int val = 0;
original.use(k1, [&](int & v) { val = v; });
assert(val == 1);

copy.use(k1, [&](int & v) { val = v; });
assert(val == 1);  // Same key, same value

// Modifying copy doesn't affect original
copy.erase(k1);
assert(not copy.contains(k1));
assert(original.contains(k1));  // Original unaffected
```

---

#### Copy Assignment

```cpp
SlotMap & operator=(SlotMap const & other)
requires std::is_copy_constructible_v<mapped_type>;
```

**Description:** Replaces contents with a deep copy of another SlotMap using copy-and-swap idiom.

**Parameters:**
- `other` - The SlotMap to copy from.

**Returns:** Reference to `*this`.

**Throws:**
- `std::bad_alloc` if allocation fails.
- Any exception from `mapped_type`'s copy constructor.

**Exception Safety:** Strong guarantee. If an exception is thrown, `*this` is unchanged.

**Availability:** Only available if `mapped_type` is copy constructible.

**Example:**

```cpp
wjh::SlotMap<MyKey> map1, map2;
auto k1 = map1.emplace(1);

map2 = map1;  // Deep copy
assert(map2.contains(k1));
```

---

#### Move Constructor

```cpp
SlotMap(SlotMap && other) noexcept;
```

**Description:** Constructs by moving from another SlotMap. The moved-from map is left in an empty but valid state.

**Parameters:**
- `other` - The SlotMap to move from.

**Postcondition:** `other.is_empty() == true`, `other` is valid and can be destroyed or assigned to.

**Exception Safety:** `noexcept` guarantee.

**Example:**

```cpp
wjh::SlotMap<MyKey> map1;
auto k1 = map1.emplace(1);

wjh::SlotMap<MyKey> map2(std::move(map1));  // Move

assert(map2.contains(k1));  // Keys moved to map2
assert(map1.is_empty());    // map1 is empty
assert(map1.size().value == 0);
```

---

#### Move Assignment

```cpp
SlotMap & operator=(SlotMap && other) noexcept;
```

**Description:** Replaces contents by moving from another SlotMap. The moved-from map is left in an empty but valid state.

**Parameters:**
- `other` - The SlotMap to move from.

**Returns:** Reference to `*this`.

**Postcondition:** `other.is_empty() == true`.

**Exception Safety:** `noexcept` guarantee.

**Example:**

```cpp
wjh::SlotMap<MyKey> map1, map2;
auto k1 = map1.emplace(1);
auto k2 = map2.emplace(2);

map2 = std::move(map1);  // Move assignment

assert(map2.contains(k1));   // k1 now in map2
assert(not map2.contains(k2));  // k2 destroyed
assert(map1.is_empty());
```

---

#### swap()

```cpp
void swap(SlotMap & other) noexcept;
```

**Description:** Swaps the contents of two SlotMaps. All keys remain valid but now refer to elements in the swapped map.

**Parameters:**
- `other` - The SlotMap to swap with.

**Exception Safety:** `noexcept` guarantee.

**Example:**

```cpp
wjh::SlotMap<MyKey> map1, map2;
auto k1 = map1.emplace(1);
auto k2 = map2.emplace(2);

map1.swap(map2);

// Keys now refer to elements in swapped maps
assert(not map1.contains(k1));  // k1 is in map2 now
assert(map1.contains(k2));   // k2 is in map1 now
assert(map2.contains(k1));
assert(not map2.contains(k2));
```

---

### Element Insertion

#### emplace()

```cpp
template <typename... Args>
[[nodiscard]] key_type emplace(Args &&... args);
```

**Description:** Constructs a new element in-place with the provided arguments. Returns a key for the new element. If capacity is exhausted, throws `std::length_error`.

**Parameters:**
- `args` - Arguments to forward to `mapped_type`'s constructor.

**Returns:**
- A valid key referring to the new element (never returns null key).

**Throws:**
- Any exception thrown by `mapped_type`'s constructor.
- `std::length_error` if capacity is exhausted (no free slots available).

**Exception Safety:** Strong guarantee. If construction throws or capacity is exhausted, the map is unchanged (no slot is consumed).

**Important:** The returned key is ALWAYS valid. If capacity is exhausted, an exception is thrown instead of returning a null key. Use `try_emplace()` for non-throwing behavior.

**Example:**

```cpp
struct Player {
    std::string name;
    int health;

    Player(std::string n, int h) : name(std::move(n)), health(h) {}
};

using PlayerKey = wjh::SlotMapKey<Player, 16, 16>;
wjh::SlotMap<PlayerKey> players;

// Construct in-place (throws on capacity exhaustion)
try {
    auto k1 = players.emplace("Alice", 100);
    auto k2 = players.emplace("Bob", 75);
    auto k3 = players.emplace("Charlie", 50);

    // Keys are always valid here
    players.use(k1, [](Player & p) {
        std::cout << p.name << " has " << p.health << " health\n";
    });
} catch (std::length_error const &) {
    std::cerr << "Cannot add more players - capacity exhausted\n";
}
```

**Capacity Limits:**

Capacity is exhausted when:
1. All 2^IndexBits slots are in use (map is full), OR
2. All slots have been reused 2^VersionBits times (lifetime exhaustion).

After exhaustion, `emplace()` throws `std::length_error`. See [Capacity and Lifetime Limits](#capacity-and-lifetime-limits) for details.

---

#### try_emplace()

```cpp
template <typename... Args>
[[nodiscard]] key_type try_emplace(Args &&... args);
```

**Description:** Constructs a new element in-place with the provided arguments. Returns a key for the new element, or the null key if capacity is exhausted. This is the non-throwing alternative to `emplace()`.

**Parameters:**
- `args` - Arguments to forward to `mapped_type`'s constructor.

**Returns:**
- A valid key referring to the new element, OR
- `key_type::null()` if capacity is exhausted (no free slots available).

**Throws:** Any exception thrown by `mapped_type`'s constructor (but NOT `std::length_error`).

**Exception Safety:** Strong guarantee. If construction throws, the map is unchanged (no slot is consumed).

**Important:** Use `try_emplace()` when you want to handle capacity exhaustion gracefully without exceptions. Always check `key.is_null()` after calling.

**Example:**

```cpp
using PlayerKey = wjh::SlotMapKey<Player, 16, 16>;
wjh::SlotMap<PlayerKey> players;

// Construct in-place (returns null key on capacity exhaustion)
auto k1 = players.try_emplace("Alice", 100);
if (k1.is_null()) {
    std::cerr << "Cannot add player - capacity exhausted\n";
    return;
}

auto k2 = players.try_emplace("Bob", 75);
if (k2.is_null()) {
    std::cerr << "Cannot add player - capacity exhausted\n";
    return;
}

// Use the keys
players.use(k1, [](Player & p) {
    std::cout << p.name << " has " << p.health << " health\n";
});
```

---

### Element Access

#### Options Struct

```cpp
struct Options {
    bool stop = false;
    bool erase = false;
};
```

**Description:** Control struct for `use()` and `for_each()` operations.

**Members:**
- `stop` - Set `true` to stop iteration early (`for_each()` only).
- `erase` - Set `true` to erase the element after the callback returns.

**Note:** `Options` is not allowed for const-value callbacks in `use`. Setting `erase` for const-value callbacks in `for_each` is meaningless, and asserts in debug mode.

**Example:**

```cpp
wjh::SlotMap<MyKey> map;
auto key = map.emplace(42);

// Conditional erase based on value
map.use(key, [](int & value, wjh::slotmap::Options & opts) {
    if (value < 0) {
        opts.erase = true;  // Request erasure
    }
});

// After the callback, if opts.erase was set to true, the element is erased
assert(not map.contains(key));  // True if value was < 0
```

---

#### use()

```cpp
template <typename F>
bool use(key_type key, F && func);

template <typename F>
bool use(key_type key, F && func) const;
```

**Description:** Access an element by key. If the key is valid and refers to an alive element, invokes the callback with the element and returns `true`. Otherwise, returns `false` without calling the callback.

This is **the** primary way to access elements. It combines lookup and access into a single safe operation.

**Non-const Callable Signatures:**

The non-const `use()` supports multiple callback signatures:
- `void(key_type, mapped_type &, Options &)` - Full access with key, value, and erase capability
- `void(key_type, mapped_type &)` - Key and value
- `void(mapped_type &, Options &)` - Value with erase capability
- `void(mapped_type &)` - Value only

**Const Callable Signatures:**

The const `use()` supports read-only callbacks without Options (since Options only supports erase which can't work on const):
- `void(key_type, mapped_type const &)` - Key and value (read-only)
- `void(mapped_type const &)` - Value only (read-only)

**Parameters:**
- `key` - The key to look up.
- `func` - Callable with one of the signatures listed above.

**Returns:**
- `true` if the key was valid and `func` was called.
- `false` if the key was invalid, stale, or null.

**When is a key invalid?**
- The key is null (`key.is_null()`).
- The slot has been erased and reused (version mismatch).
- The index is out of bounds (should never happen with keys from `emplace()`).

**Erase-After-Callback:**

When using the non-const `use()` with an `Options &` parameter, the callback can request element erasure by setting `opts.erase = true`. The element will be erased AFTER the callback returns, ensuring the element is accessible within the callback.

**Examples:**

```cpp
wjh::SlotMap<MyKey> map;
auto key = map.emplace(42);

// Basic use: value only
int value = 0;
if (map.use(key, [&](int const & v) { value = v; })) {
    std::cout << "Value: " << value << '\n';  // 42
} else {
    std::cerr << "Key is invalid\n";
}

// Use with key parameter
map.use(key, [](MyKey k, int & v) {
    std::cout << "Key index: " << k.index().value << ", value: " << v << '\n';
    v *= 2;
});

// Use with Options for conditional erase
map.use(key, [](int & v, wjh::slotmap::Options & opts) {
    if (v > 100) {
        opts.erase = true;  // Request erasure after callback
    }
});

// Full signature with all parameters
map.use(key, [](MyKey k, int & v, wjh::slotmap::Options & opts) {
    std::cout << "Processing key " << k.index().value << '\n';
    if (v < 0) {
        /* map.erase(k); --> UB ; don't do it! */
        opts.erase = true;  // Erase negative values
    }
});

// Const access (read-only)
map.use(key, [](int const & v) {
    std::cout << "Value: " << v << '\n';
});

// Const access with key parameter
map.use(key, [](MyKey k, int const & v) {
    std::cout << "Key: " << k.index().value << ", value: " << v << '\n';
});

// After erase, key becomes invalid
map.erase(key);
bool found = map.use(key, [](int & v) { v = 100; });
assert(not found);  // Key is stale, func not called
```

**Pattern: Safe Access Without Exceptions**

```cpp
// Instead of:
// try {
//     auto & elem = map.at(key);  // Hypothetical - SlotMap doesn't have this
//     elem.update();
// } catch (...) { /* handle */ }

// Use:
if (not map.use(key, [](auto & elem) { elem.update(); })) {
    // Handle invalid key
}
```

**Pattern: Conditional Erase**

```cpp
// Erase elements that meet a condition
map.use(key, [](Player & p, wjh::slotmap::Options & opts) {
    if (p.health <= 0) {
        std::cout << p.name << " has been defeated!\n";
        opts.erase = true;  // Remove from map
    }
});
```

---

#### contains()

```cpp
[[nodiscard]]
bool contains(key_type key) const;
```

**Description:** Checks if a key refers to an alive element.

**Parameters:**
- `key` - The key to check.

**Returns:** `true` if the key is valid and refers to an alive element, `false` otherwise.

**Implementation Note:** Equivalent to `use(key, [](auto &){})`.

**Example:**

```cpp
auto key = map.emplace(42);
assert(map.contains(key));

map.erase(key);
assert(not map.contains(key));

// Null key is never contained
assert(not map.contains(MyKey::null()));
```

**Use Case:** When you only need to check existence without accessing the element. However, if you plan to access the element afterward, prefer `use()` to avoid double lookup:

```cpp
// Less efficient (double lookup)
if (map.contains(key)) {
    map.use(key, [](auto & elem) { elem.update(); });
}

// Better (single lookup)
if (not map.use(key, [](auto & elem) { elem.update(); })) {
    // Handle invalid key
}
```

---

### Element Removal

#### erase()

```cpp
bool erase(key_type key);
```

**Description:** Erases an element by key. If the key is valid, destroys the element, increments the slot's version, and returns the slot to the free list (or marks it dead if version is exhausted).

**Parameters:**
- `key` - The key of the element to erase.

**Returns:**
- `true` if an element was erased.
- `false` if the key was invalid or already erased.

**Postcondition:**
- The key becomes invalid (version mismatch).
- If the slot's version is exhausted, it may trigger slab recycling.

**Exception Safety:** No-throw guarantee. Element destruction is assumed to be `noexcept`.

**Example:**

```cpp
auto key = map.emplace(42);
assert(map.contains(key));

bool erased = map.erase(key);
assert(erased);
assert(not map.contains(key));  // Key is now stale

// Erasing again returns false
bool erased_again = map.erase(key);
assert(not erased_again);

// Null key returns false
assert(not map.erase(MyKey::null()));
```

---

#### pop()

```cpp
[[nodiscard]]
std::optional<mapped_type> pop(key_type key)
requires std::is_move_constructible_v<mapped_type>;
```

**Description:** Removes an element by key and returns it. If the key is valid, moves the element out, destroys the slot, and returns the moved value wrapped in `std::optional`. If the key is invalid, returns `std::nullopt`.

**Parameters:**
- `key` - The key of the element to remove.

**Returns:**
- `std::optional<mapped_type>` containing the moved element if the key was valid.
- `std::nullopt` if the key was invalid.

**Availability:** Only available if `mapped_type` is move constructible.

**Exception Safety:** Strong guarantee if `mapped_type`'s move constructor is `noexcept`. Otherwise, basic guarantee (slot is erased even if move throws).

**Example:**

```cpp
struct Player {
    std::string name;
    int score;
};

using PlayerKey = wjh::SlotMapKey<Player, 16, 16>;
wjh::SlotMap<PlayerKey> players;

auto key = players.emplace(Player{"Alice", 100});

// Pop the player out
if (auto player = players.pop(key)) {
    std::cout << player->name << " scored " << player->score << '\n';
    // Player is moved out, no longer in map
    assert(not players.contains(key));
} else {
    std::cerr << "Key was invalid\n";
}

// Popping again returns nullopt
auto again = players.pop(key);
assert(not again.has_value());
```

**Use Case:** When you need to remove an element and continue using it afterward (e.g., transferring ownership, logging, processing before destruction).

---

### Bulk Operations

#### for_each()

```cpp
template <typename F>
size_type for_each(F && func);

template <typename F>
size_type for_each(F && func) const;
```

**Description:** Iterates over all alive elements, invoking `func` for each. Supports multiple callable signatures for flexibility.

**Parameters:**
- `func` - Callable with one of these signatures:
  - `void(key_type, mapped_type &, Options &)` - Full access with stop/erase
  - `void(key_type, mapped_type &)` - Key and value
  - `void(mapped_type &, Options &)` - Value with stop/erase
  - `void(mapped_type &)` - Value only

**Returns:** Number of elements visited (may be less than `size()` if early exit occurred).

**Iteration Order:** Unspecified but consistent within a single call. Not guaranteed to be the same across calls (especially after erase/emplace).

**Options:**
- `options.stop = true` - Stop iteration after this callback.
- `options.erase = true` - Erase the current element after the callback returns.

**Note:** The const overload does not support `Options`; setting `erase` asserts in debug mode.

**Example:**

```cpp
wjh::SlotMap<MyKey> map;
map.emplace(1);
map.emplace(2);
map.emplace(3);

// Iterate over values only
map.for_each([](int & val) {
    val *= 2;  // Double all values
});

// Iterate with keys
std::vector<MyKey> all_keys;
map.for_each([&](MyKey key, int & val) {
    all_keys.push_back(key);
    std::cout << "Key index: " << key.index().value
              << ", value: " << val << '\n';
});

// Early exit
auto count = map.for_each([](int & val, wjh::slotmap::Options & opts) {
    if (val > 10) {
        opts.stop = true;  // Stop iteration
    }
});
std::cout << "Visited " << count.value << " elements\n";

// Erase elements matching a predicate
map.for_each([](int const & val, wjh::slotmap::Options & opts) {
    if (val < 0) {
        opts.erase = true;  // Erase negative values
    }
});

// Const iteration (no Options support)
map.for_each([](int const & val) {
    std::cout << val << '\n';
});
```

**Note:** Calling `emplace()` during iteration is undefined behavior. Use `options.erase` for erasure instead of calling `erase()` directly.

---

#### clear()

```cpp
void clear();
```

**Description:** Destroys all alive elements, increments all versions, and rebuilds the free list. Memory is retained for reuse (slabs are not deallocated).

**Postcondition:**
- `is_empty() == true`
- `size() == 0`
- All keys that were valid before `clear()` are now invalid (version mismatch).
- Slabs remain allocated and ready for reuse.

**Exception Safety:** No-throw guarantee. Element destructors are assumed to be `noexcept`.

**Example:**

```cpp
wjh::SlotMap<MyKey> map;
auto k1 = map.emplace(1);
auto k2 = map.emplace(2);

assert(map.size().value == 2);
assert(map.contains(k1));

map.clear();

assert(map.is_empty());
assert(map.size().value == 0);
assert(not map.contains(k1));  // k1 is now stale
assert(not map.contains(k2));  // k2 is now stale

// Can emplace new elements immediately (no allocation needed)
auto k3 = map.emplace(3);
assert(map.size().value == 1);
```

**Use Case:** When you want to remove all elements but continue using the map afterward. Memory is retained to avoid reallocation on subsequent insertions.

---

#### reset()

```cpp
void reset();
```

**Description:** Destroys all elements and deallocates all slabs, returning the map to a freshly-constructed state. All memory is released.

**Postcondition:**
- `is_empty() == true`
- `size() == 0`
- All slabs are deallocated.
- The map is equivalent to a newly constructed SlotMap.

**Exception Safety:** No-throw guarantee.

**Example:**

```cpp
wjh::SlotMap<MyKey> map;
auto k1 = map.emplace(1);
auto k2 = map.emplace(2);

assert(map.size().value == 2);

map.reset();

assert(map.is_empty());
assert(map.size().value == 0);

// Memory has been released
// Next emplace will allocate a new slab
auto k3 = map.emplace(3);
assert(map.size().value == 1);
```

**Use Case:** When you're done with the map and want to reclaim memory. Prefer `clear()` if you'll reuse the map soon (to avoid reallocation).

**Difference from clear():**

| Operation | Destroys Elements | Deallocates Memory | Invalidates Keys |
|-----------|-------------------|-------------------|------------------|
| `clear()` | Yes | No | Yes |
| `reset()` | Yes | Yes | Yes |

---

### Capacity

#### is_empty()

```cpp
[[nodiscard]]
bool is_empty() const noexcept;
```

**Description:** Checks if the map contains any elements.

**Returns:** `true` if `size() == 0`, `false` otherwise.

**Example:**

```cpp
wjh::SlotMap<MyKey> map;
assert(map.is_empty());

auto key = map.emplace(42);
assert(not map.is_empty());

map.erase(key);
assert(map.is_empty());
```

---

#### size()

```cpp
[[nodiscard]]
size_type size() const noexcept;
```

**Description:** Returns the number of alive elements currently in the map.

**Returns:** Count of alive elements as `size_type`.

**Example:**

```cpp
wjh::SlotMap<MyKey> map;
assert(map.size().value == 0);

auto k1 = map.emplace(1);
assert(map.size().value == 1);

auto k2 = map.emplace(2);
assert(map.size().value == 2);

map.erase(k1);
assert(map.size().value == 1);

map.clear();
assert(map.size().value == 0);
```

---

#### reserve()

```cpp
void reserve(size_type n);
```

**Description:** Pre-allocates enough slabs to hold at least `n` elements without further allocation. Useful for avoiding allocations during hot paths or when you know the expected size.

**Parameters:**
- `n` - Desired capacity (number of elements).

**Throws:** `std::bad_alloc` if allocation fails.

**Note:** If the index space is exhausted (cannot allocate more slabs), `reserve()` stops early without throwing.

**Example:**

```cpp
using MyKey = wjh::SlotMapKey<int, 16, 16>;
wjh::SlotMap<MyKey> map;

// Pre-allocate space for 10000 elements
map.reserve(MyKey::size_type{10000});

// Now emplace won't need to allocate until we exceed 10000 elements
for (int i = 0; i < 10000; ++i) {
    auto key = map.emplace(i);  // No allocations
    assert(not key.is_null());
}
```

**Use Case:**
- **Performance**: Avoid allocations in tight loops or real-time code.
- **Predictability**: Ensure allocations happen upfront, not during critical sections.

---

## Strong Types

The library uses strong types (via `detail::TypeBase`) for `index_type`, `version_type`, `user_type`, and `size_type`. These are NOT raw integers but distinct types that prevent accidental mixing.

**Key Points:**

- Strong types prevent bugs (e.g., passing a version where an index is expected).
- Access the underlying value via the `.value` member.
- Comparison and increment operators are supported.
- Strong types from different keys are incompatible.

**Example:**

```cpp
using MyKey = wjh::SlotMapKey<int, 16, 16>;
MyKey::index_type idx;
MyKey::version_type ver;

// idx = ver;  // Compile error - different types

// Access underlying value
auto raw_idx = idx.value;  // uint16_t (or appropriate type)

// Comparison works
MyKey::index_type idx2;
if (idx == idx2) { /* ... */ }

// Increment works
++idx;
idx++;

// But cannot accidentally mix types
// auto sum = idx + ver;  // Compile error
```

**Why strong types?**

Without strong types, it's easy to make mistakes like:

```cpp
// Hypothetical without strong types:
auto idx = key.index();    // Returns uint16_t
auto ver = key.version();  // Returns uint16_t
// ...
auto new_key = Key(ver, idx, user);  // OOPS! Swapped index and version
```

With strong types, this error is caught at compile time:

```cpp
auto idx = key.index();    // Returns index_type
auto ver = key.version();  // Returns version_type
auto new_key = Key(ver, idx, user);  // Compile error - types don't match
```

---

## Capacity and Lifetime Limits

Understanding capacity limits is critical for using SlotMap correctly.

### Maximum Simultaneous Elements

**Limit:** 2^IndexBits

The map can hold at most 2^IndexBits elements at any given time. This is the number of unique slots available.

**Examples:**

| IndexBits | Max Simultaneous |
|-----------|------------------|
| 8         | 256              |
| 16        | 65,536           |
| 20        | 1,048,576        |
| 24        | 16,777,216       |

### Maximum Total Insertions (Lifetime)

**Limit:** 2^IndexBits × 2^VersionBits - 1

This is the total number of elements that can be inserted over the lifetime of the map before all slots are permanently exhausted.

**Examples:**

| IndexBits | VersionBits | Max Lifetime Insertions |
|-----------|-------------|------------------------|
| 16        | 16          | ~4.3 billion           |
| 8         | 24          | ~4.3 billion           |
| 24        | 8           | ~4.3 billion           |
| 20        | 20          | ~1.1 trillion          |

**Explanation:**

Each slot can be reused 2^VersionBits times (except the first slot of the first slab). After that, the slot becomes "dead" and can no longer be used.

### When Capacity is Exhausted

**Symptom:** `emplace()` throws `std::length_error`, or `try_emplace()` returns the null key.

**Handling:**

```cpp
// Option 1: Use try_emplace for graceful error handling
auto key = map.try_emplace(value);
if (key.is_null()) {
    // Capacity exhausted
    // Options:
    // 1. Remove old elements to free slots
    // 2. Use a different map with larger IndexBits/VersionBits
    // 3. Report error to user
    std::cerr << "Cannot add element - capacity exhausted\n";
}

// Option 2: Use emplace with exception handling
try {
    auto key = map.emplace(value);
    // Key is always valid here
} catch (std::length_error const &) {
    std::cerr << "Cannot add element - capacity exhausted\n";
}
```

**Prevention:**

- Choose appropriate `IndexBits` and `VersionBits` for your use case.
- Monitor `size()` if you're approaching 2^IndexBits.
- If you frequently create and destroy elements (high churn), use more `VersionBits` to avoid version exhaustion.

### Version Exhaustion

**What happens:**

Each slot has a version counter. Every time a slot is erased and reused, the version increments. Once a slot's version reaches 2^VersionBits - 1, the slot becomes "dead" and is not returned to the free list.

**Slab Recycling:**

When all slots in a slab become dead, the slab can be recycled:
- All versions are reset to 0.
- The slab is moved to a new index range.
- Slots become available again, but with a new base index.
- This is purely an optimization to not have to allocate another slab when more slots are needed.

This extends the slot's lifetime but does NOT increase the total number of simultaneous elements.


---

## Thread Safety

**SlotMap is NOT thread-safe.**

### Safe Usage:

- **Multiple readers**: Multiple threads can safely call `const` operations (`use()`, `contains()`, `for_each()`, `size()`, `is_empty()`) concurrently.
- **Single writer**: Any non-`const` operation (`emplace()`, `erase()`, `pop()`, `clear()`, `reset()`) requires exclusive access.

### Unsafe Usage:

```cpp
// UNSAFE - multiple threads calling emplace() concurrently
std::thread t1([&]() { map.emplace(1); });
std::thread t2([&]() { map.emplace(2); });
// DATA RACE - undefined behavior
```

### Safe with Synchronization:

```cpp
std::mutex mtx;

std::thread t1([&]() {
    std::lock_guard lock(mtx);
    map.emplace(1);
});

std::thread t2([&]() {
    std::lock_guard lock(mtx);
    map.emplace(2);
});
// Safe - synchronized access
```

### Read-Write Lock Pattern:

```cpp
std::shared_mutex mtx;

// Reader threads
std::thread reader([&]() {
    std::shared_lock lock(mtx);
    map.use(key, [](auto const & val) { /* read */ });
});

// Writer thread
std::thread writer([&]() {
    std::unique_lock lock(mtx);
    map.emplace(42);
});
```

---

## Exception Safety

| Operation           | Guarantee | Notes |
|---------------------|-----------|-------|
| `emplace()`         | Strong    | If `T`'s constructor throws, map is unchanged; throws `std::length_error` if capacity exhausted |
| `try_emplace()`     | Strong    | If `T`'s constructor throws, map is unchanged; returns null key if capacity exhausted (no throw) |
| `erase()`           | No-throw  | Assumes `T`'s destructor is `noexcept` |
| `pop()`             | Strong    | If `T`'s move is `noexcept`; otherwise basic |
| `clear()`           | No-throw  | Assumes `T`'s destructor is `noexcept` |
| `reset()`           | No-throw  | Assumes `T`'s destructor is `noexcept` |
| Copy constructor    | Strong    | If allocation or `T`'s copy constructor throws, no object created |
| Copy assignment     | Strong    | Uses copy-and-swap; if copy throws, `*this` unchanged |
| Move constructor    | No-throw  | Always `noexcept` |
| Move assignment     | No-throw  | Always `noexcept` |
| `use()`             | Basic     | If callable throws, element is unchanged but exception propagates |
| `for_each()`        | Basic     | If callable throws, some elements may have been modified |

**Strong Guarantee:** If an exception is thrown, the operation has no effect (rollback to previous state).

**Basic Guarantee:** If an exception is thrown, the object is in a valid but unspecified state (no leaks, but partial modifications may have occurred).

**No-throw Guarantee:** The operation never throws exceptions.

**Example:**

```cpp
struct ThrowingType {
    ThrowingType(int x) {
        if (x < 0) throw std::runtime_error("Negative value");
    }
};

using ThrowKey = wjh::SlotMapKey<ThrowingType, 16, 16>;
wjh::SlotMap<ThrowKey> map;

try {
    auto key = map.emplace(-1);  // Constructor throws
    // Never reached
} catch (std::runtime_error const &) {
    // Strong guarantee: map is unchanged
    assert(map.is_empty());
}

// Success case
auto key = map.emplace(1);  // No throw
assert(map.size().value == 1);
```

---

## Common Patterns

### Storing Keys in Other Containers

Keys are regular types with comparison and hashing support, so they work with standard containers.

```cpp
using PlayerKey = wjh::SlotMapKey<Player, 16, 16>;
wjh::SlotMap<PlayerKey> players;

// Store keys in a vector
std::vector<PlayerKey> active_players;
auto k1 = players.emplace(Player{"Alice", 100});
auto k2 = players.emplace(Player{"Bob", 75});
active_players.push_back(k1);
active_players.push_back(k2);

// Store keys in a set
std::unordered_set<PlayerKey> vip_players;
vip_players.insert(k1);

// Store keys in a map
std::unordered_map<PlayerKey, std::string> player_guilds;
player_guilds[k1] = "Warriors";
player_guilds[k2] = "Mages";

// Iterate over keys
for (auto key : active_players) {
    players.use(key, [](Player & p) {
        p.health -= 10;
    });
}
```

---

### Using User Bits for Metadata

User bits allow storing small amounts of metadata directly in keys without modifying the stored element or allocating additional memory.

```cpp
using TaggedKey = wjh::SlotMapKey<Entity, 16, 8, 8>;
wjh::SlotMap<TaggedKey> entities;

// Emplace an entity
auto key = entities.emplace(Entity{});

// Tag with priority
auto high_priority = key.with_user(TaggedKey::user_type{255});
auto low_priority = key.with_user(TaggedKey::user_type{1});

// Store tagged keys
std::vector<TaggedKey> high_prio_entities;
high_prio_entities.push_back(high_priority);

// Process by priority
for (auto tagged_key : high_prio_entities) {
    auto priority = tagged_key.user().value;
    entities.use(tagged_key, [priority](Entity & e) {
        std::cout << "Processing entity with priority " << priority << '\n';
        e.update();
    });
}

// Important: user bits are in the KEY, not in the map
// key, high_priority, and low_priority all access the SAME element
entities.use(key, [](Entity & e) { e.name = "Original"; });
entities.use(high_priority, [](Entity & e) {
    assert(e.name == "Original");  // Same element
});
```

**Use Cases for User Bits:**

- **Priority/Ordering**: Store priority or sort order in keys.
- **Flags**: Store small flags (alive, dirty, marked, etc.) without modifying the element.
- **Generation**: Track a separate generation counter for additional versioning.
- **Type Tags**: Store entity type or category in 64-bit entities with 24 user bits.

---

### Checking Before Heavy Operations

Prefer `use()` over `contains()` + `use()` to avoid double lookup.

```cpp
// Less efficient (double lookup)
if (map.contains(key)) {
    map.use(key, [](auto & elem) {
        elem.expensive_operation();
    });
}

// Better (single lookup)
if (not map.use(key, [](auto & elem) {
    elem.expensive_operation();
})) {
    // Handle invalid key
    std::cerr << "Key not found\n";
}
```

**Exception:** If you need to check validity without side effects, `contains()` is appropriate:

```cpp
// Check validity for logic, but don't modify yet
if (map.contains(player_key) && map.contains(enemy_key)) {
    // Both are valid, now process
    map.use(player_key, [&](Player & p) { /* ... */ });
    map.use(enemy_key, [&](Enemy & e) { /* ... */ });
}
```

---

### Erasing During Iteration

Use `options.erase` to erase elements during `for_each()`.

```cpp
wjh::SlotMap<MyKey> map;
// ... populate map ...

// Erase negative values during iteration
map.for_each([](int const & val, wjh::slotmap::Options & opts) {
    if (val < 0) {
        opts.erase = true;
    }
});

// Alternative: collect keys first (if you need to process them afterward)
std::vector<MyKey> negative_keys;
map.for_each([&](MyKey key, int const & val) {
    if (val < 0) {
        negative_keys.push_back(key);
    }
});
for (auto key : negative_keys) {
    // Log, process, then erase
    map.erase(key);
}
```

**Note:** Calling `emplace()` during iteration is still undefined behavior.

---

### Iterating with Early Exit

Use the `Options` parameter to stop iteration when a condition is met.

```cpp
#include <wjh/slotmap/SlotMap.hpp>

wjh::SlotMap<MyKey> map;
// ... populate map ...

// Find first element > 100
MyKey found_key = MyKey::null();
map.for_each([&](MyKey key, int const & val, wjh::slotmap::Options & brk) {
    if (val > 100) {
        found_key = key;
        brk.stop = true;
    }
});

if (not found_key.is_null()) {
    std::cout << "Found element > 100\n";
} else {
    std::cout << "No element > 100\n";
}
```

---

### Building Indices

Create auxiliary data structures (indices) that map from some property to keys.

```cpp
struct Player {
    std::string name;
    int team_id;
    int health;
};

using PlayerKey = wjh::SlotMapKey<Player, 16, 16>;
wjh::SlotMap<PlayerKey> players;

// Index: team_id -> vector of keys
std::unordered_map<int, std::vector<PlayerKey>> teams;

// Insert players
auto k1 = players.emplace(Player{"Alice", 1, 100});
auto k2 = players.emplace(Player{"Bob", 2, 75});
auto k3 = players.emplace(Player{"Charlie", 1, 90});

// Build index
players.for_each([&](PlayerKey key, Player const & p) {
    teams[p.team_id].push_back(key);
});

// Query by team
for (auto key : teams[1]) {
    players.use(key, [](Player const & p) {
        std::cout << p.name << " is on team 1\n";
    });
}
// Prints: Alice is on team 1
//         Charlie is on team 1
```

---

### Swap Pattern for Batch Updates

Use `swap()` to atomically replace a map's contents.

```cpp
wjh::SlotMap<MyKey> current_state;
// ... current_state is being read by other parts of the program ...

// Build new state in a separate map
wjh::SlotMap<MyKey> new_state;
new_state.emplace(1);
new_state.emplace(2);
new_state.emplace(3);

// Atomically swap (if synchronized properly)
{
    std::lock_guard lock(mtx);  // Assuming external synchronization
    current_state.swap(new_state);
}
// current_state now has new contents, new_state has old contents

// new_state can be discarded or reused
new_state.clear();
```

---

This completes the comprehensive API reference for `wjh::slotmap`. For more information, see the [README](../README.md) and the source code in `src/wjh/slotmap/`.
