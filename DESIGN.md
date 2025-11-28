# SlotMap Design Document

## Overview

This document specifies the implementation of `SlotMap<Key<IndexBits, VersionBits, UserBits, T>>`, a high-performance associative container providing O(1) insertion, deletion, and lookup with persistent unique keys.

Unlike `std::unordered_map`, the container generates and returns keys upon insertion rather than accepting user-provided keys.


NOTE: This is an initial design. As implementation unfolds, we may need to change certain things, so this is a design GUIDE not a design REQUIREMENT.
---

## Table of Contents

1. [Key Concepts](#key-concepts)
2. [Class Template Specification](#class-template-specification)
3. [Memory Architecture](#memory-architecture)
4. [Slot Lifecycle](#slot-lifecycle)
5. [Free List Management](#free-list-management)
6. [Slab Recycling](#slab-recycling)
7. [Public API Specification](#public-api-specification)
8. [Implementation Details](#implementation-details)
9. [Error Handling](#error-handling)
10. [Test Cases](#test-cases)

---

## Key Concepts

### Terminology

| Term | Definition |
|------|------------|
| **Key** | Opaque handle containing version + index (+ optional user bits). Returned by `emplace()`. |
| **Index** | Direct array offset for O(1) lookup. Encoded in the key. |
| **Version** | Monotonically increasing counter per slot to detect stale keys. |
| **User Bits** | Optional user-defined metadata stored within the key. |
| **Slot** | Storage unit containing either a value (when alive) or a free-list link (when free). |
| **Slab** | Fixed-size memory block containing an array of slots plus metadata. |
| **Null Key** | A key with all bits zero (version=0, index=0, user=0). Never returned by `emplace()`. |

### Invariants

1. **Null Key Safety**: Version 0 is reserved. The first insertion at any index uses version 1.
2. **Key Uniqueness**: A key uniquely identifies a specific object. Once erased, that exact key is never valid again.
3. **ABA Protection**: Version numbers prevent returning stale data when a slot is reused.
4. **Contiguous Storage**: Values within a slab are stored contiguously for cache efficiency.

---

## Class Template Specification

### Template Declaration

```cpp
namespace wjh::slotmap {

template <typename...> class SlotMap;

template <
    unsigned IndexBits,
    unsigned VersionBits,
    unsigned UserBits,
    typename T>
class SlotMap<Key<IndexBits, VersionBits, UserBits, T>>;

} // namespace wjh::slotmap
```

### Type Aliases

```cpp
// Within the class:
using key_type = Key<IndexBits, VersionBits, UserBits, T>;
using value_type = T;
using size_type = typename key_type::Index::value_type;
using version_type = typename key_type::Version::value_type;
using index_type = typename key_type::Index::value_type;
```

### Convenience Aliases

```cpp
namespace wjh {

template <typename KeyT>
using SlotMap = slotmap::SlotMap<KeyT>;

} // namespace wjh
```

---

## Memory Architecture

### Slab Structure

Each slab is a contiguous memory allocation containing:

1. **Slot Array**: `slots_per_slab` slots, each holding either a value or free-list link
2. **Slab Metadata**: Dead slot count for this slab

#### Slab Layout (Conceptual)

```
┌─────────────────────────────────────────────────────────────┐
│                        Slab                                  │
├─────────────────────────────────────────────────────────────┤
│  Metadata: { dead_count: size_type }                        │
├─────────────────────────────────────────────────────────────┤
│  Slot[0] │ Slot[1] │ Slot[2] │ ... │ Slot[slots_per_slab-1] │
└─────────────────────────────────────────────────────────────┘
```

### Slot Structure

Each slot contains a union and version storage:

```cpp
struct Slot {
    union Storage {
        index_type next;    // Active when slot is FREE
        value_type value;   // Active when slot is ALIVE
    } storage_;

    // Version stored as byte array to avoid padding issues
    std::array<std::byte, sizeof(version_type)> version_bytes_;
};
```

#### Slot Memory Layout

```
┌──────────────────────────────────────────┐
│                  Slot                     │
├────────────────────────┬─────────────────┤
│  union Storage         │  version_bytes_ │
│  (sizeof(value_type)   │  (sizeof        │
│   or sizeof(index_type)│   version_type) │
│   whichever larger)    │                 │
└────────────────────────┴─────────────────┘
```

**Note**: The union's size is `max(sizeof(value_type), sizeof(index_type))`. This may waste space if `value_type` is small, but simplifies implementation.

### Slot Operations

```cpp
// Version access - always valid
version_type version() const;
void set_version(version_type v);

// Free-list access - only valid when FREE
index_type next() const;
void set_next(index_type i);

// Value access - only valid when ALIVE
template <typename... Args>
value_type& emplace(Args&&... args);
void destroy();
value_type& value();
value_type const& value() const;
```

### SlotMap Container Structure

```cpp
template <...>
class SlotMap<Key<...>> {
private:
    std::vector<Slab*> slabs_;           // Indexed by slab_index = key.index() >> log2_slots_per_slab_
    index_type free_list_head_;          // Head of global free list (null_index if empty)
    size_type size_;                     // Number of alive elements
    size_type slots_per_slab_;           // Power of 2, set at construction
    unsigned log2_slots_per_slab_;       // For fast division: slab_index = index >> this
    index_type next_slab_base_index_;    // Base index for next slab to allocate
};
```

### Index to Slab Mapping

Given a key's index:
```cpp
slab_index = index >> log2_slots_per_slab_;
slot_index_within_slab = index & (slots_per_slab_ - 1);
```

### Slab Pointer Vector

- `slabs_[slab_index]` may be `nullptr` if that slab was exhausted and recycled
- Maximum vector size: `(max_index + 1) / slots_per_slab_`
- Vector grows lazily as slabs are allocated

---

## Slot Lifecycle

### State Diagram

```
                    ┌─────────────┐
                    │   UNUSED    │  Initial state (version = 0)
                    │  (in free   │
                    │    list)    │
                    └──────┬──────┘
                           │
                           │ emplace() → version becomes 1
                           ↓
                    ┌─────────────┐
                    │    ALIVE    │  Value is constructed
                    │  (in use)   │
                    └──────┬──────┘
                           │
                           │ erase() → version++
                           ↓
           ┌───────────────┴───────────────┐
           │                               │
           │ version < max_version         │ version == max_version
           ↓                               ↓
    ┌─────────────┐                 ┌─────────────┐
    │   UNUSED    │                 │    DEAD     │
    │  (back to   │                 │ (permanent, │
    │  free list) │                 │  never      │
    └─────────────┘                 │  reused)    │
                                    └─────────────┘
```

### Version Semantics

- **Version 0**: Reserved for null key. Slot is FREE but has never been used.
- **Version 1 to max_version-1**: Slot has been used and can be reused after erase.
- **Version max_version**: Slot is DEAD. It remains in the slab but is never placed in the free list.

Where `max_version = (1 << VersionBits) - 1`.

### State Transitions

| From | Event | To | Actions |
|------|-------|-----|---------|
| UNUSED (v=0) | emplace() | ALIVE (v=1) | Construct value, remove from free list |
| UNUSED (v>0) | emplace() | ALIVE (v unchanged) | Construct value, remove from free list |
| ALIVE | erase() where v < max | UNUSED | Destroy value, increment version, add to free list head |
| ALIVE | erase() where v == max | DEAD | Destroy value, increment slab's dead_count |

---

## Free List Management

### Structure

The free list is a singly-linked list using the `next` field of the slot's union.

```
free_list_head_ ──→ Slot[i].next ──→ Slot[j].next ──→ ... ──→ null_index
```

Where `null_index` is the maximum representable index value plus one (or a sentinel value).

### Sentinel Value

```cpp
static constexpr index_type null_index =
    static_cast<index_type>((index_type{1} << IndexBits) - 1);
```

**Note**: This means the maximum usable index is `null_index - 1`. The null_index itself is reserved as the end-of-list sentinel.

**Alternative**: Use a separate boolean or high bit. But since index 0 with version 0 is the null key, we could use max_index as sentinel. Document the chosen approach.

**Decision**: Use `(1 << IndexBits) - 1` as null_index sentinel. This reduces usable slots by 1, which is acceptable.

### Operations

#### Allocate from Free List

```cpp
index_type allocate_slot() {
    if (free_list_head_ == null_index) {
        if (!allocate_new_slab()) {
            return null_index;  // No more room
        }
    }
    index_type idx = free_list_head_;
    Slot& slot = get_slot(idx);
    free_list_head_ = slot.next();
    return idx;
}
```

#### Return to Free List

```cpp
void free_slot(index_type idx) {
    Slot& slot = get_slot(idx);
    slot.set_next(free_list_head_);
    free_list_head_ = idx;
}
```

### Slab Initialization

When a new slab is created with base index `base`:

```cpp
// Link all slots in the new slab
for (size_type i = 0; i < slots_per_slab_ - 1; ++i) {
    slab->slot(i).set_next(base + i + 1);
    slab->slot(i).set_version(0);
}
// Last slot in slab points to old free list head
slab->slot(slots_per_slab_ - 1).set_next(free_list_head_);
slab->slot(slots_per_slab_ - 1).set_version(0);

// New free list head is first slot in new slab
free_list_head_ = base;
```

---

## Slab Recycling

### When Recycling Occurs

A slab becomes exhausted when all its slots are DEAD:
```cpp
slab->dead_count() == slots_per_slab_
```

### Recycling Process

When a slab at `slab_index` becomes exhausted:

1. **Check if there's room for more slabs**:
   - If `next_slab_base_index_ + slots_per_slab_ <= null_index`: Can recycle
   - Otherwise: Delete the slab and set `slabs_[slab_index] = nullptr`

2. **Recycle the slab**:
   ```cpp
   // Move slab to next available position
   size_type new_slab_index = next_slab_base_index_ >> log2_slots_per_slab_;

   // Ensure vector is large enough
   if (new_slab_index >= slabs_.size()) {
       slabs_.resize(new_slab_index + 1, nullptr);
   }

   // Reset slab metadata
   slab->reset_dead_count();

   // Re-initialize all slots as FREE with version 0
   index_type base = next_slab_base_index_;
   for (size_type i = 0; i < slots_per_slab_ - 1; ++i) {
       slab->slot(i).set_next(base + i + 1);
       slab->slot(i).set_version(0);
   }
   slab->slot(slots_per_slab_ - 1).set_next(free_list_head_);
   slab->slot(slots_per_slab_ - 1).set_version(0);

   // Update bookkeeping
   slabs_[slab_index] = nullptr;
   slabs_[new_slab_index] = slab;
   free_list_head_ = base;
   next_slab_base_index_ += slots_per_slab_;
   ```

### Example: Slab Recycling

```
Initial state (slots_per_slab = 1024):
  slabs_[0] → Slab covering indices 0-1023
  slabs_[1] → Slab covering indices 1024-2047
  slabs_[2] → Slab covering indices 2048-3071
  next_slab_base_index_ = 3072

Slab[1] becomes exhausted:
  slabs_[0] → Slab covering indices 0-1023
  slabs_[1] → nullptr (was recycled)
  slabs_[2] → Slab covering indices 2048-3071
  slabs_[3] → Recycled slab, now covering indices 3072-4095
  next_slab_base_index_ = 4096
```

---

## Public API Specification

### Constructors

#### Default Constructor

```cpp
SlotMap();
```

- **Effect**: Constructs an empty SlotMap with default slab size
- **Default Slab Size Logic**:
  - If `(1 << IndexBits) * sizeof(Slot) <= 2MB`: Use single slab covering all indices
  - Otherwise: Use 4096 slots per slab (or nearest power of 2 fitting in ~256KB)
- **Postconditions**: `is_empty() == true`, `size() == 0`
- **Throws**: `std::bad_alloc` if initial slab allocation fails

#### Explicit Size Constructor

```cpp
explicit SlotMap(size_type slots_per_slab);
```

- **Preconditions**:
  - `slots_per_slab > 0`
  - `slots_per_slab` is a power of 2
  - `slots_per_slab <= (1 << IndexBits)`
- **Effect**: Constructs an empty SlotMap with specified slab size
- **Throws**:
  - `std::invalid_argument` if `slots_per_slab` is not a power of 2
  - `std::bad_alloc` if initial slab allocation fails

### Copy/Move Operations

#### Copy Constructor

```cpp
SlotMap(SlotMap const& other);
```

- **Constraints**: Only participates in overload resolution if `std::is_copy_constructible_v<T>`
- **Effect**: Creates a deep copy with identical structure
- **Exception Safety**: Strong guarantee. If any copy throws, no resources leak.
- **Postconditions**: `size() == other.size()`, all keys from `other` are valid in `*this`

#### Copy Assignment

```cpp
SlotMap& operator=(SlotMap const& other);
```

- **Constraints**: Only participates in overload resolution if `std::is_copy_constructible_v<T> && std::is_copy_assignable_v<T>`
- **Effect**: Replaces contents with deep copy of `other`
- **Exception Safety**: Strong guarantee (copy-and-swap idiom)
- **Returns**: `*this`

#### Move Constructor

```cpp
SlotMap(SlotMap&& other) noexcept;
```

- **Effect**: Takes ownership of `other`'s resources
- **Postconditions**: `other.is_empty() == true`, `other` is in valid but unspecified state

#### Move Assignment

```cpp
SlotMap& operator=(SlotMap&& other) noexcept;
```

- **Effect**: Releases current resources and takes ownership of `other`'s
- **Postconditions**: `other` is in valid but unspecified state
- **Returns**: `*this`

### Destructor

```cpp
~SlotMap();
```

- **Effect**: Destroys all alive values and deallocates all slabs
- **Note**: Calls destructor for each alive `T`

### Element Access

#### use()

```cpp
template <typename F>
bool use(key_type key, F&& func);

template <typename F>
bool use(key_type key, F&& func) const;
```

- **Effect**: If `key` is valid and refers to an alive element, invokes `func(value)`
- **Returns**: `true` if element was found and `func` was called, `false` otherwise
- **Callable Signature**: `void(T&)` or `void(T const&)` for const overload
- **Note**: Returns `false` for null keys without accessing any slab

#### contains()

```cpp
bool contains(key_type key) const;
```

- **Returns**: `true` if `key` refers to an alive element, `false` otherwise
- **Note**: Returns `false` for null keys without accessing any slab

### Modifiers

#### emplace()

```cpp
template <typename... Args>
key_type emplace(Args&&... args);
```

- **Effect**: Constructs a new element in-place with the given arguments
- **Returns**: A valid key for the new element, or `key_type::null()` if no slots available
- **Exception Safety**: Strong guarantee. If construction throws, the slot remains free.
- **Postconditions**: If returned key is not null, `contains(key) == true` and `size()` increased by 1
- **Note**: The returned key's version is always >= 1

#### erase()

```cpp
bool erase(key_type key);
```

- **Effect**: If `key` is valid, destroys the element and frees or retires the slot
- **Returns**: `true` if an element was erased, `false` otherwise
- **Postconditions**: `contains(key) == false`, `size()` decreased by 1 if returned `true`
- **Note**: May trigger slab recycling if slot reaches max version and slab becomes exhausted

#### pop()

```cpp
std::optional<T> pop(key_type key);
```

- **Constraints**: Only participates in overload resolution if `std::is_move_constructible_v<T>`
- **Effect**: If `key` is valid, moves the element out, destroys the slot's value, and frees/retires the slot
- **Returns**: The moved element wrapped in `optional`, or `std::nullopt` if key invalid
- **Exception Safety**: Strong guarantee if `T`'s move constructor is noexcept

#### clear()

```cpp
void clear();
```

- **Effect**: Destroys all alive elements, increments all versions, resets free list
- **Postconditions**: `is_empty() == true`, `size() == 0`
- **Note**: Memory is retained. Keys that were valid before `clear()` are now invalid (version mismatch).

#### reset()

```cpp
void reset();
```

- **Effect**: Destroys all elements and deallocates all slabs
- **Postconditions**: `is_empty() == true`, `size() == 0`
- **Note**: Returns to freshly-constructed state (one empty slab)

#### swap()

```cpp
void swap(SlotMap& other) noexcept;
```

- **Effect**: Exchanges contents with `other`
- **Postconditions**: `*this` has `other`'s former contents and vice versa

### Capacity

#### is_empty()

```cpp
[[nodiscard]] bool is_empty() const noexcept;
```

- **Returns**: `true` if `size() == 0`

#### size()

```cpp
[[nodiscard]] size_type size() const noexcept;
```

- **Returns**: Number of alive elements

#### reserve()

```cpp
void reserve(size_type n);
```

- **Effect**: Pre-allocates enough slabs to hold at least `n` elements without further allocation
- **Throws**: `std::bad_alloc` if allocation fails
- **Note**: Useful for avoiding allocations during hot paths

### Iteration

#### for_each()

```cpp
template <typename F>
size_type for_each(F&& func);

template <typename F>
size_type for_each(F&& func) const;
```

- **Effect**: Invokes `func(key, value, break_tag)` for each alive element
- **Returns**: Number of elements visited (may be less than `size()` if early exit)
- **Callable Signature**: `void(key_type, T&, break_t&)` or `void(key_type, T const&, break_t&)`
- **Early Exit**: Set `break_tag.stop = true` to stop iteration
- **Iteration Order**: Unspecified, but iterates over slabs in order and slots within slab in order
- **Undefined Behavior**: Modifying the SlotMap (emplace/erase) during iteration

#### break_t

```cpp
struct break_t {
    bool stop = false;
};
```

---

## Implementation Details

### File Structure

```
src/wjh/slotmap/
├── SlotMap.hpp          # Main SlotMap template
├── detail/
│   └── Slab.hpp         # Slab and Slot implementation
```

### Slab Implementation

#### Slab Class

```cpp
namespace wjh::slotmap::detail {

template <typename T, typename IndexType, typename VersionType, typename SizeType>
class Slab {
public:
    explicit Slab(SizeType slots_per_slab);
    ~Slab();

    // Non-copyable, non-movable (managed by SlotMap)
    Slab(Slab const&) = delete;
    Slab& operator=(Slab const&) = delete;

    Slot<T, IndexType, VersionType>& slot(SizeType index);
    Slot<T, IndexType, VersionType> const& slot(SizeType index) const;

    SizeType dead_count() const;
    void increment_dead_count();
    void reset_dead_count();

private:
    SizeType dead_count_;
    // Flexible array member or separate allocation for slots
    // Implementation may use placement new
};

} // namespace wjh::slotmap::detail
```

#### Slot Class

```cpp
namespace wjh::slotmap::detail {

template <typename T, typename IndexType, typename VersionType>
class Slot {
public:
    // Version access (always valid)
    VersionType version() const noexcept;
    void set_version(VersionType v) noexcept;

    // Free-list access (only when FREE)
    IndexType next() const noexcept;
    void set_next(IndexType i) noexcept;

    // Value access (only when ALIVE)
    template <typename... Args>
    T& emplace(Args&&... args);
    void destroy() noexcept(std::is_nothrow_destructible_v<T>);
    T& value() noexcept;
    T const& value() const noexcept;

private:
    union Storage {
        IndexType next;
        T value;

        Storage() noexcept : next{} {}
        ~Storage() {}  // Manual destruction
    } storage_;

    std::array<std::byte, sizeof(VersionType)> version_bytes_;
};

} // namespace wjh::slotmap::detail
```

### Key Validation

To validate a key:

```cpp
bool is_valid(key_type key) const {
    if (key.is_null()) return false;

    index_type idx = key.index();
    if (idx >= next_slab_base_index_) return false;

    size_type slab_idx = idx >> log2_slots_per_slab_;
    if (slab_idx >= slabs_.size() || slabs_[slab_idx] == nullptr) return false;

    Slot const& slot = get_slot(idx);
    return slot.version() == key.version();
}
```

### Helper Methods

```cpp
private:
    // Get slot by absolute index
    Slot& get_slot(index_type idx) {
        size_type slab_idx = idx >> log2_slots_per_slab_;
        size_type slot_idx = idx & (slots_per_slab_ - 1);
        return slabs_[slab_idx]->slot(slot_idx);
    }

    Slot const& get_slot(index_type idx) const {
        size_type slab_idx = idx >> log2_slots_per_slab_;
        size_type slot_idx = idx & (slots_per_slab_ - 1);
        return slabs_[slab_idx]->slot(slot_idx);
    }

    // Allocate a new slab if possible
    bool allocate_new_slab() {
        if (next_slab_base_index_ >= null_index) return false;

        size_type new_slab_idx = next_slab_base_index_ >> log2_slots_per_slab_;
        if (new_slab_idx >= slabs_.size()) {
            slabs_.resize(new_slab_idx + 1, nullptr);
        }

        auto slab = std::make_unique<Slab>(slots_per_slab_);
        initialize_slab_free_list(slab.get(), next_slab_base_index_);

        slabs_[new_slab_idx] = slab.release();
        next_slab_base_index_ += slots_per_slab_;
        return true;
    }

    void initialize_slab_free_list(Slab* slab, index_type base) {
        for (size_type i = 0; i < slots_per_slab_ - 1; ++i) {
            slab->slot(i).set_next(base + i + 1);
            slab->slot(i).set_version(0);
        }
        slab->slot(slots_per_slab_ - 1).set_next(free_list_head_);
        slab->slot(slots_per_slab_ - 1).set_version(0);
        free_list_head_ = base;
    }
```

### Constants

```cpp
static constexpr index_type null_index =
    static_cast<index_type>((index_type{1} << IndexBits) - 1);

static constexpr version_type max_version =
    static_cast<version_type>((version_type{1} << VersionBits) - 1);
```

---

## Error Handling

### Exception Specifications

| Method | Can Throw | Exception Safety |
|--------|-----------|------------------|
| Default ctor | Yes (`bad_alloc`) | Strong |
| Size ctor | Yes (`bad_alloc`, `invalid_argument`) | Strong |
| Copy ctor | Yes (from `T` copy) | Strong |
| Copy assign | Yes (from `T` copy) | Strong |
| Move ctor | No | - |
| Move assign | No | - |
| Destructor | No | - |
| emplace() | Yes (from `T` ctor) | Strong |
| erase() | No | - |
| pop() | Conditional (if T's move throws) | Strong if noexcept |
| clear() | No | - |
| reset() | No | - |
| use() | Yes (from callable) | Basic (no state change) |
| for_each() | Yes (from callable) | Basic (no state change) |
| reserve() | Yes (`bad_alloc`) | Strong |

### Precondition Violations

- Passing a key from a different SlotMap: Undefined behavior
- Using a stale key (version mismatch): Returns `false` / no-op
- Null key: Returns `false` / no-op (not UB)
- Modifying SlotMap during `for_each`: Undefined behavior

---

## Test Cases

### Unit Tests (doctest)

#### Construction Tests

```cpp
TEST_CASE("SlotMap default construction") {
    SUBCASE("is empty after construction") {
        SlotMap<Key<16, 16, 0, int>> map;
        CHECK(map.is_empty());
        CHECK(map.size() == 0);
    }

    SUBCASE("explicit slab size must be power of 2") {
        CHECK_THROWS_AS(
            SlotMap<Key<16, 16, 0, int>>(1000),  // Not power of 2
            std::invalid_argument
        );
        CHECK_NOTHROW(SlotMap<Key<16, 16, 0, int>>(1024));
    }

    SUBCASE("slab size cannot exceed max index") {
        // With 4 index bits, max usable index is 14 (15 is null_index)
        CHECK_THROWS_AS(
            SlotMap<Key<4, 4, 0, int>>(32),  // Exceeds 15
            std::invalid_argument
        );
    }
}
```

#### Emplace/Erase Tests

```cpp
TEST_CASE("SlotMap emplace and erase") {
    SlotMap<Key<16, 16, 0, int>> map;

    SUBCASE("emplace returns valid key") {
        auto key = map.emplace(42);
        CHECK(!key.is_null());
        CHECK(map.contains(key));
        CHECK(map.size() == 1);
    }

    SUBCASE("emplace constructs value correctly") {
        auto key = map.emplace(42);
        bool found = false;
        map.use(key, [&](int const& v) {
            found = true;
            CHECK(v == 42);
        });
        CHECK(found);
    }

    SUBCASE("erase removes element") {
        auto key = map.emplace(42);
        CHECK(map.erase(key));
        CHECK(!map.contains(key));
        CHECK(map.size() == 0);
    }

    SUBCASE("erase returns false for invalid key") {
        auto key = map.emplace(42);
        map.erase(key);
        CHECK(!map.erase(key));  // Already erased
    }

    SUBCASE("erase returns false for null key") {
        CHECK(!map.erase(Key<16, 16, 0, int>::null()));
    }

    SUBCASE("key version increments after erase and re-emplace") {
        auto key1 = map.emplace(1);
        auto v1 = key1.version();
        map.erase(key1);

        auto key2 = map.emplace(2);
        // Same index (only slot), but version incremented
        CHECK(key2.index() == key1.index());
        CHECK(key2.version() == v1 + 1);
    }
}
```

#### Null Key Tests

```cpp
TEST_CASE("SlotMap null key handling") {
    SlotMap<Key<16, 16, 0, int>> map;
    auto null_key = Key<16, 16, 0, int>::null();

    SUBCASE("contains returns false for null key") {
        CHECK(!map.contains(null_key));
    }

    SUBCASE("use returns false for null key") {
        bool called = false;
        CHECK(!map.use(null_key, [&](int&) { called = true; }));
        CHECK(!called);
    }

    SUBCASE("erase returns false for null key") {
        CHECK(!map.erase(null_key));
    }

    SUBCASE("pop returns nullopt for null key") {
        CHECK(!map.pop(null_key).has_value());
    }

    SUBCASE("emplace never returns null key for version") {
        auto key = map.emplace(42);
        CHECK(key.version() >= 1);
    }
}
```

#### Capacity Tests

```cpp
TEST_CASE("SlotMap capacity exhaustion") {
    // Small index space: 4 bits = 15 usable indices (16 - 1 for null_index)
    SlotMap<Key<4, 4, 0, int>> map(4);  // 4 slots per slab

    SUBCASE("returns null when capacity exhausted") {
        std::vector<Key<4, 4, 0, int>> keys;
        for (int i = 0; i < 15; ++i) {
            auto key = map.emplace(i);
            CHECK(!key.is_null());
            keys.push_back(key);
        }

        // 16th emplace should fail (index 15 is null_index)
        auto overflow_key = map.emplace(999);
        CHECK(overflow_key.is_null());
    }
}
```

#### Version Exhaustion Tests

```cpp
TEST_CASE("SlotMap version exhaustion") {
    // 2-bit version: versions 0, 1, 2, 3; max_version = 3
    // After 3 uses, slot is dead
    SlotMap<Key<8, 2, 0, int>> map(4);

    SUBCASE("slot becomes dead after max version") {
        auto key1 = map.emplace(1);  // version 1
        auto idx1 = key1.index();
        map.erase(key1);

        auto key2 = map.emplace(2);  // version 2, same index
        CHECK(key2.index() == idx1);
        CHECK(key2.version() == 2);
        map.erase(key2);

        auto key3 = map.emplace(3);  // version 3 (max), same index
        CHECK(key3.index() == idx1);
        CHECK(key3.version() == 3);
        map.erase(key3);  // Slot is now dead

        // Next emplace should get different index
        auto key4 = map.emplace(4);
        CHECK(key4.index() != idx1);
    }
}
```

#### Copy/Move Tests

```cpp
TEST_CASE("SlotMap copy operations") {
    SlotMap<Key<16, 16, 0, std::string>> map;
    auto key1 = map.emplace("hello");
    auto key2 = map.emplace("world");

    SUBCASE("copy constructor creates independent copy") {
        auto copy = map;
        CHECK(copy.size() == 2);
        CHECK(copy.contains(key1));
        CHECK(copy.contains(key2));

        // Modifying copy doesn't affect original
        copy.erase(key1);
        CHECK(!copy.contains(key1));
        CHECK(map.contains(key1));
    }

    SUBCASE("copy assignment") {
        SlotMap<Key<16, 16, 0, std::string>> other;
        other.emplace("other");

        other = map;
        CHECK(other.size() == 2);
        CHECK(other.contains(key1));
    }
}

TEST_CASE("SlotMap move operations") {
    SlotMap<Key<16, 16, 0, std::string>> map;
    auto key = map.emplace("hello");

    SUBCASE("move constructor transfers ownership") {
        auto moved = std::move(map);
        CHECK(moved.contains(key));
        CHECK(map.is_empty());  // NOLINT: testing moved-from state
    }

    SUBCASE("move assignment") {
        SlotMap<Key<16, 16, 0, std::string>> other;
        other = std::move(map);
        CHECK(other.contains(key));
    }
}
```

#### for_each Tests

```cpp
TEST_CASE("SlotMap for_each") {
    SlotMap<Key<16, 16, 0, int>> map;
    auto k1 = map.emplace(1);
    auto k2 = map.emplace(2);
    auto k3 = map.emplace(3);

    SUBCASE("visits all elements") {
        int sum = 0;
        auto count = map.for_each([&](auto, int& v, break_t&) {
            sum += v;
        });
        CHECK(count == 3);
        CHECK(sum == 6);
    }

    SUBCASE("early exit with break_t") {
        int sum = 0;
        auto count = map.for_each([&](auto, int& v, break_t& brk) {
            sum += v;
            if (sum >= 3) brk.stop = true;
        });
        CHECK(count < 3);
        CHECK(sum >= 3);
    }

    SUBCASE("const for_each") {
        SlotMap<Key<16, 16, 0, int>> const& cmap = map;
        int sum = 0;
        cmap.for_each([&](auto, int const& v, break_t&) {
            sum += v;
        });
        CHECK(sum == 6);
    }
}
```

#### pop Tests

```cpp
TEST_CASE("SlotMap pop") {
    SlotMap<Key<16, 16, 0, std::string>> map;
    auto key = map.emplace("hello");

    SUBCASE("pop returns value and removes") {
        auto result = map.pop(key);
        CHECK(result.has_value());
        CHECK(*result == "hello");
        CHECK(!map.contains(key));
    }

    SUBCASE("pop returns nullopt for invalid key") {
        map.erase(key);
        auto result = map.pop(key);
        CHECK(!result.has_value());
    }
}
```

#### clear/reset Tests

```cpp
TEST_CASE("SlotMap clear and reset") {
    SlotMap<Key<16, 16, 0, int>> map;
    auto key1 = map.emplace(1);
    auto key2 = map.emplace(2);

    SUBCASE("clear removes all elements") {
        map.clear();
        CHECK(map.is_empty());
        CHECK(!map.contains(key1));
        CHECK(!map.contains(key2));
    }

    SUBCASE("clear invalidates old keys via version bump") {
        auto v_before = key1.version();
        map.clear();
        auto key3 = map.emplace(3);
        // Same index might be reused, but version is different
        CHECK(key3.version() > v_before);
    }

    SUBCASE("reset deallocates memory") {
        map.reset();
        CHECK(map.is_empty());
        // Can still use after reset
        auto key3 = map.emplace(3);
        CHECK(map.contains(key3));
    }
}
```

#### Exception Safety Tests

```cpp
struct ThrowingType {
    static int throw_after;
    static int constructions;

    int value;

    ThrowingType(int v) : value(v) {
        if (++constructions >= throw_after) {
            throw std::runtime_error("construction failed");
        }
    }
};

TEST_CASE("SlotMap exception safety") {
    SUBCASE("emplace strong guarantee") {
        SlotMap<Key<16, 16, 0, ThrowingType>> map;
        ThrowingType::throw_after = 2;
        ThrowingType::constructions = 0;

        auto key1 = map.emplace(1);  // Succeeds
        CHECK(map.size() == 1);

        CHECK_THROWS(map.emplace(2));  // Throws
        CHECK(map.size() == 1);  // Size unchanged
        CHECK(map.contains(key1));  // Original still valid
    }
}
```

#### Slab Recycling Tests

```cpp
TEST_CASE("SlotMap slab recycling") {
    // 2-bit version (max 3), 8-bit index, 2 slots per slab
    SlotMap<Key<8, 2, 0, int>> map(2);

    SUBCASE("exhausted slab is recycled") {
        // Use up all versions for first 2 slots (slab 0)
        for (int round = 0; round < 3; ++round) {
            auto k1 = map.emplace(1);
            auto k2 = map.emplace(2);
            CHECK(k1.index() == 0);
            CHECK(k2.index() == 1);
            map.erase(k1);
            map.erase(k2);
        }

        // Now slab 0 is exhausted. Next emplace should use slab 1 indices
        auto k = map.emplace(99);
        CHECK(k.index() >= 2);  // From a new/recycled slab
    }
}
```

### Property-Based Tests (rapidcheck)

Look at `AtFork_ut.cpp`, `SymbolTable_ut.cpp`, and `MmapAlloc_ut.cpp` in `wjh_ipc/src/wjh/ipc/tests/` for rapidcheck example usage.

```cpp
RC_GTEST_PROP(SlotMap, insert_find_roundtrip, ()) {
    SlotMap<Key<16, 16, 0, int>> map;
    auto values = *rc::gen::container<std::vector<int>>(rc::gen::arbitrary<int>());

    std::vector<Key<16, 16, 0, int>> keys;
    for (auto v : values) {
        keys.push_back(map.emplace(v));
    }

    RC_ASSERT(map.size() == values.size());

    for (size_t i = 0; i < values.size(); ++i) {
        int found = -1;
        map.use(keys[i], [&](int const& v) { found = v; });
        RC_ASSERT(found == values[i]);
    }
}

RC_GTEST_PROP(SlotMap, erase_invalidates_key, ()) {
    SlotMap<Key<16, 16, 0, int>> map;
    auto value = *rc::gen::arbitrary<int>();

    auto key = map.emplace(value);
    RC_ASSERT(map.contains(key));

    map.erase(key);
    RC_ASSERT(!map.contains(key));
}

RC_GTEST_PROP(SlotMap, for_each_visits_all_alive, ()) {
    SlotMap<Key<16, 16, 0, int>> map;
    auto values = *rc::gen::container<std::vector<int>>(rc::gen::arbitrary<int>());

    for (auto v : values) {
        map.emplace(v);
    }

    size_t count = 0;
    map.for_each([&](auto, int&, break_t&) { ++count; });

    RC_ASSERT(count == values.size());
}

RC_GTEST_PROP(SlotMap, size_tracks_alive_elements, ()) {
    SlotMap<Key<16, 16, 0, int>> map;
    auto ops = *rc::gen::container<std::vector<bool>>(rc::gen::arbitrary<bool>());

    std::vector<Key<16, 16, 0, int>> keys;
    size_t expected_size = 0;

    for (bool should_insert : ops) {
        if (should_insert || keys.empty()) {
            keys.push_back(map.emplace(*rc::gen::arbitrary<int>()));
            ++expected_size;
        } else {
            auto idx = *rc::gen::inRange<size_t>(0, keys.size());
            if (map.erase(keys[idx])) {
                --expected_size;
            }
        }
    }

    RC_ASSERT(map.size() == expected_size);
}

RC_GTEST_PROP(SlotMap, copy_is_independent, ()) {
    SlotMap<Key<16, 16, 0, int>> map;
    auto values = *rc::gen::container<std::vector<int>>(rc::gen::arbitrary<int>());

    std::vector<Key<16, 16, 0, int>> keys;
    for (auto v : values) {
        keys.push_back(map.emplace(v));
    }

    auto copy = map;

    // Erase from original
    for (auto k : keys) {
        map.erase(k);
    }

    // Copy should still have all elements
    RC_ASSERT(map.is_empty());
    RC_ASSERT(copy.size() == values.size());
    for (auto k : keys) {
        RC_ASSERT(copy.contains(k));
    }
}
```

### Static Assertions

```cpp
// In SlotMap.hpp or a test file
static_assert(std::is_default_constructible_v<SlotMap<Key<16, 16, 0, int>>>);
static_assert(std::is_copy_constructible_v<SlotMap<Key<16, 16, 0, int>>>);
static_assert(std::is_copy_assignable_v<SlotMap<Key<16, 16, 0, int>>>);
static_assert(std::is_move_constructible_v<SlotMap<Key<16, 16, 0, int>>>);
static_assert(std::is_move_assignable_v<SlotMap<Key<16, 16, 0, int>>>);
static_assert(std::is_nothrow_move_constructible_v<SlotMap<Key<16, 16, 0, int>>>);
static_assert(std::is_nothrow_move_assignable_v<SlotMap<Key<16, 16, 0, int>>>);

// Non-copyable value type
struct NonCopyable {
    NonCopyable() = default;
    NonCopyable(NonCopyable const&) = delete;
    NonCopyable(NonCopyable&&) = default;
};

static_assert(!std::is_copy_constructible_v<SlotMap<Key<16, 16, 0, NonCopyable>>>);
static_assert(std::is_move_constructible_v<SlotMap<Key<16, 16, 0, NonCopyable>>>);

// Non-moveable value type
struct NonMoveable {
    NonMoveable() = default;
    NonMoveable(NonMoveable const&) = default;
    NonMoveable(NonMoveable&&) = delete;
};

// pop() should not be available for non-moveable types
// (Implementation would use requires clause or SFINAE)
```

### Bit Configuration Tests

```cpp
TEST_CASE("SlotMap with various bit configurations") {
    SUBCASE("32-bit key (16/16/0)") {
        SlotMap<Key<16, 16, 0, int>> map;
        auto key = map.emplace(42);
        CHECK(sizeof(key) == 4);
        CHECK(map.contains(key));
    }

    SUBCASE("64-bit key (32/32/0)") {
        SlotMap<Key<32, 32, 0, int>> map;
        auto key = map.emplace(42);
        CHECK(sizeof(key) == 8);
        CHECK(map.contains(key));
    }

    SUBCASE("64-bit key with user bits (20/20/24)") {
        SlotMap<Key<20, 20, 24, int>> map;
        auto key = map.emplace(42);
        CHECK(sizeof(key) == 8);
        CHECK(map.contains(key));
        CHECK(key.user() == 0);  // User bits default to 0
    }

    SUBCASE("minimal key (1/1/0 in 32 bits with padding)") {
        // 1-bit index = 1 usable slot (0), null_index = 1
        // 1-bit version = versions 0,1; max_version = 1
        // This is pathological but should work
        SlotMap<Key<1, 1, 30, int>> map(1);
        auto key = map.emplace(42);
        CHECK(key.index() == 0);
        CHECK(key.version() == 1);

        map.erase(key);  // Version becomes max (1), slot is dead

        auto key2 = map.emplace(99);
        CHECK(key2.is_null());  // No more slots
    }
}
```

---

## Implementation Checklist

### Phase 1: Core Infrastructure ✅ COMPLETED

- [x] Create `src/wjh/slotmap/detail/Slot.hpp`
  - [x] Slot class with byte array storage (not union - avoids UB)
  - [x] Version byte array storage
  - [x] `emplace()`, `destroy()`, `value()` methods
  - [x] `next()`, `set_next()` for free list
  - [x] `version()`, `set_version()` methods
  - [x] Trivially default constructible (value-initialization zeros)
  - [x] Explicit `Slot(index_type next)` constructor for FREE state init
  - [x] Non-copyable, non-movable
  - [x] Uses `std::launder` for proper pointer provenance
  - [x] Tests in `src/wjh/slotmap/tests/Slot_ut.cpp`

- [x] Create `src/wjh/slotmap/detail/Slab.hpp`
  - [x] Slab class with flexible array pattern (slots after header)
  - [x] Factory method `Slab::create(slots_per_slab)` returns `std::unique_ptr<Slab>`
  - [x] Dead count tracking (`dead_count()`, `increment_dead_count()`, `reset_dead_count()`)
  - [x] Proper alignment via `alignas()` on class
  - [x] Uses `std::launder` for slot array access
  - [x] All forms of `operator new` deleted (except placement used internally)
  - [x] Private constructor enforces use of `create()`
  - [x] Non-copyable, non-movable
  - [x] Tests in `src/wjh/slotmap/tests/Slab_ut.cpp`

**Implementation Notes from Phase 1:**
- Slot uses `std::array<std::byte, ...>` for storage instead of union to avoid union-related UB
- Slab uses flexible array member pattern: header followed by contiguous slot array
- `std::launder` is used in both classes for correct pointer provenance after placement new
- Property-based tests use `rc::check("description", []() { ... })` pattern (see wjh_ipc for examples)

---

## 🚀 RESUME HERE - Next Agent Instructions

**Status:** Phase 1 is complete. Begin Phase 2.

**What's done:**
- `src/wjh/slotmap/detail/Slot.hpp` - Complete with tests
- `src/wjh/slotmap/detail/Slab.hpp` - Complete with tests
- Tests pass: `ctest --output-on-failure --test-dir build`

**Next steps:**
1. Read this DESIGN.md thoroughly for the SlotMap API specification
2. Begin Phase 2: Create `src/wjh/slotmap/SlotMap.hpp`
3. Follow the coding standards in CLAUDE.md
4. Build: `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DWJH_SLOTMAP_BUILD_TESTS=ON -DWJH_SLOTMAP_SANITIZE=ON && cmake --build build`
5. Test: `ctest --output-on-failure --test-dir build`

**Key implementation details from Phase 1 to be aware of:**
- `Slab::create(n)` is the only way to construct a Slab (returns `std::unique_ptr<Slab>`)
- Slot is trivially default constructible; use `Slot{}` for zero-init or `Slot(next_index)` for FREE state
- Both use `std::launder` for pointer access - follow the same pattern in SlotMap

---

### Phase 2: Basic SlotMap

- [ ] Create `src/wjh/slotmap/SlotMap.hpp`
  - [ ] Template specialization for `Key<...>`
  - [ ] Type aliases
  - [ ] Member variables (slabs vector, free list, size, etc.)
  - [ ] Default constructor with slab size heuristic
  - [ ] Explicit slab size constructor with power-of-2 validation

### Phase 3: Core Operations

- [ ] Implement `emplace()`
  - [ ] Free list allocation
  - [ ] New slab allocation when needed
  - [ ] Version management (start at 1)
  - [ ] Exception safety

- [ ] Implement `erase()`
  - [ ] Key validation
  - [ ] Value destruction
  - [ ] Version increment
  - [ ] Free list return OR dead count increment
  - [ ] Slab recycling trigger

- [ ] Implement `use()` and `contains()`
  - [ ] Key validation logic
  - [ ] Null key fast path

- [ ] Tests for all public interfaces

### Phase 4: Iteration and Bulk Operations

- [ ] Implement `for_each()`
  - [ ] Iterate over slabs
  - [ ] Skip nullptr slabs
  - [ ] Skip dead/free slots (check version > 0 and slot is alive)
  - [ ] Early exit support with `break_t`

- [ ] Implement `clear()`
  - [ ] Destroy all alive values
  - [ ] Increment all versions
  - [ ] Rebuild free list

- [ ] Implement `reset()`
  - [ ] Destroy all values
  - [ ] Deallocate all slabs
  - [ ] Re-initialize to default state

- [ ] Tests for all public interfaces

### Phase 5: Copy/Move Operations

- [ ] Implement copy constructor
  - [ ] Deep copy all slabs
  - [ ] Preserve structure exactly

- [ ] Implement copy assignment (copy-and-swap)

- [ ] Implement move constructor

- [ ] Implement move assignment

- [ ] Implement `swap()`

- [ ] Tests for all public interfaces

### Phase 6: Additional Features

- [ ] Implement `pop()`
  - [ ] SFINAE for move-constructible types
  - [ ] Move value out before destroying

- [ ] Implement `reserve()`
  - [ ] Pre-allocate slabs

- [ ] Implement slab recycling
  - [ ] Detect exhausted slab
  - [ ] Move to next index range
  - [ ] Handle "no more room" case

- [ ] Tests for all public interfaces

### Phase 7: Testing

- [ ] Unit tests for all public API
- [ ] Property-based tests with rapidcheck
- [ ] Exception safety tests
- [ ] Edge case tests (1-bit fields, capacity limits)
- [ ] Static assertions for type traits

---

## Open Questions / Future Work

1. **Thread Safety**: Could add atomic operations for free list, or sharding by slab. Deferred.

2. **Custom Allocators**: PMR support can be added by storing `memory_resource*` and using it for slab allocation.

3. **Iteration Performance**: Consider adding a "alive bitmap" per slab for faster iteration over sparse slabs.

4. **Bulk Erase**: Could add `erase_if(predicate)` that's more efficient than individual erases.

5. **Key Introspection**: Could expose `max_index()`, `max_version()` as static members.
