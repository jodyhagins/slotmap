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
| **Slab** | Fixed-size memory block containing an array of slots plus metadata and alive bitmap. |
| **Null Key** | A key with all bits zero (version=0, index=0, user=0). Never returned by `emplace()`. |

### Invariants

1. **Null Key Safety**: The null key (all bits zero: version=0, index=0, user=0) is never returned by `emplace()`. This is achieved by initializing slot 0 of the first slab to version 1. All other slots start at version 0.
2. **Key Uniqueness**: A key uniquely identifies a specific object. Once erased, that exact key is never valid again.
3. **ABA Protection**: Version numbers prevent returning stale data when a slot is reused.
4. **Contiguous Storage**: Values within a slab are stored contiguously for cache efficiency.
5. **All Indices Usable**: Every index from 0 to `2^IndexBits - 1` can store values; no index is wasted as a sentinel.

---

## Class Template Specification

### Template Declaration

```cpp
namespace wjh::slotmap {

template <typename KeyT>
class SlotMap;

} // namespace wjh::slotmap
```

The template takes any `KeyT` that satisfies `is_key_v<KeyT>` (i.e., an instantiation of `Key<IndexBits, VersionBits, UserBits, T>`).

### Type Aliases

```cpp
// Within the class:
using key_type = KeyT;
using mapped_type = typename key_type::tag_type;    // The stored type T
using index_type = typename key_type::index_type;   // Strong type with IndexBits bits
using version_type = typename key_type::version_type; // Strong type with VersionBits bits
using user_type = typename key_type::user_type;     // Strong type with UserBits bits
using size_type = typename key_type::size_type;     // Strong type with IndexBits+1 bits
```

**Important**: These are all strong types (derived from `detail::TypeBase`), not raw integral types. They provide type safety and have a `.value` member for accessing the underlying value. Use string types in the interfaces - can use naked types for internal implementation details.

The `size_type` has `IndexBits + 1` bits, allowing it to hold values from 0 to `2^IndexBits` (inclusive). This is critical for:
- Representing the count of all possible indices
- Storing the free list sentinel (`end_of_free_list = 2^IndexBits`)

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

1. **Slab Metadata**: Dead slot count, slots per slab count
2. **Slot Array**: `slots_per_slab` slots, each holding either a value or free-list link
3. **Alive Bitmap**: `ceil(slots_per_slab / 8)` bytes tracking which slots have values

#### Slab Layout (Conceptual)

```
┌─────────────────────────────────────────────────────────────┐
│                        Slab                                  │
├─────────────────────────────────────────────────────────────┤
│  Metadata: { dead_count, slots_per_slab }                   │
├─────────────────────────────────────────────────────────────┤
│  Slot[0] │ Slot[1] │ Slot[2] │ ... │ Slot[slots_per_slab-1] │
├─────────────────────────────────────────────────────────────┤
│  Alive Bitmap (ceil(slots_per_slab / 8) bytes)              │
└─────────────────────────────────────────────────────────────┘
```

### Slot Structure

Each slot contains byte-array storage for value/next-link and version:

```cpp
template <typename T, typename SizeT, typename VersionT>
class Slot {
    // Storage for either size_type (next link) or T (value)
    alignas(std::max(alignof(SizeT), alignof(T)))
        std::array<std::byte, std::max(sizeof(SizeT), sizeof(T))> storage_;

    // Version stored as byte array
    std::array<std::byte, sizeof(VersionT)> version_bytes_;
};
```

**Note**: The slot uses `size_type` (not `index_type`) for the next-link field. This allows storing the `end_of_free_list` sentinel value (`2^IndexBits`), which doesn't fit in `index_type`.

### Slot Operations

```cpp
// Version access - always valid
version_type version() const;
void set_version(version_type v);

// Free-list access - only valid when FREE
size_type next() const;      // Returns size_type to hold sentinel
void set_next(size_type i);  // Takes size_type for same reason

// Value access - only valid when ALIVE
template <typename... Args>
value_type& emplace(Args&&... args);
void destroy();
value_type& value();
value_type const& value() const;
```

### SlotMap Container Structure

```cpp
template <typename KeyT>
class SlotMap {
private:
    std::vector<std::unique_ptr<slab_type>> slabs_;
    size_type free_list_head_;           // size_type to hold end_of_free_list
    naked_size_type size_;               // Number of alive elements
    naked_size_type slots_per_slab_;     // Power of 2, set at construction
    unsigned log2_slots_per_slab_;       // For fast division
    naked_size_type next_slab_base_index_;  // Must be size_type to avoid overflow
};
```

**Important**: `next_slab_base_index_` must be `naked_size_type` (not `naked_index_type`) because it needs to hold values up to `2^IndexBits` (one past the max valid index) to detect when the index space is exhausted.

### Index to Slab Mapping

Given a key's index:
```cpp
slab_index = index >> log2_slots_per_slab_;
slot_index_within_slab = index & (slots_per_slab_ - 1);
```

### Slab Pointer Vector

- `slabs_[slab_index]` may be `nullptr` if that slab was exhausted and recycled
- Maximum vector size: `2^IndexBits / slots_per_slab_`
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
                           │ emplace() → version remains 0
                           ↓
                    ┌─────────────┐
                    │    ALIVE    │  Value is constructed
                    │  (in use)   │  Alive bitmap bit is set
                    └──────┬──────┘
                           │
                           │ erase() → alive bit cleared
                           ↓
           ┌───────────────┴───────────────┐
           │                               │
           │ version < max_version         │ version == max_version
           ↓                               ↓
    ┌─────────────┐                 ┌─────────────┐
    │   UNUSED    │ ++version       │    DEAD     │
    │  (back to   │                 │ (permanent, │
    │  free list) │                 │  never      │
    └─────────────┘                 │  reused)    │
                                    └─────────────┘
```

### Version Semantics

The version stored in the slot represents the **current version to use when emplacing**. When `emplace()` is called, the slot's current version is captured and used in the returned key. Here's the lifecycle:

- **Initial state**: All slots start at version 0, except slot 0 of the first slab which starts at version 1 (to avoid returning the null key).
- **On emplace**: The current slot version is used for the key (versions 0 through max_version are all valid).
- **On erase**: If version < max_version, increment version and return slot to free list. If version == max_version, slot becomes DEAD.
- **DEAD state**: Slot cannot be reused. When all slots in a slab are dead, the slab may be recycled.

Where `max_version = version_type::mask = (1 << VersionBits) - 1`.

**Example with 2-bit version (max_version = 3):**

| State | Version | Action | Result |
|-------|---------|--------|--------|
| FREE | 0 | emplace() | Key gets version 0, slot becomes ALIVE |
| ALIVE | 0 | erase() | Version increments to 1, slot becomes FREE |
| FREE | 1 | emplace() | Key gets version 1, slot becomes ALIVE |
| ALIVE | 1 | erase() | Version increments to 2, slot becomes FREE |
| FREE | 2 | emplace() | Key gets version 2, slot becomes ALIVE |
| ALIVE | 2 | erase() | Version increments to 3, slot becomes FREE |
| FREE | 3 | emplace() | Key gets version 3, slot becomes ALIVE |
| ALIVE | 3 | erase() | Version is max, slot becomes DEAD |

A slot with N-bit version can be used N times before becoming dead (versions 0 through 2^N - 1).

### State Transitions

| From | Event | To | Actions |
|------|-------|-----|---------|
| UNUSED | emplace() | ALIVE  | Construct value, set alive bit, remove from free list |
| ALIVE | erase() where v < max | UNUSED | Destroy value, clear alive bit, increment version, add to free list |
| ALIVE | erase() where v == max | DEAD | Destroy value, clear alive bit, increment slab's dead_count |

### Alive Bitmap

Each slab maintains a bitmap where bit N indicates whether slot N is alive (has a constructed value). This enables:
- O(1) alive status checking without examining version
- Efficient iteration over alive slots
- Proper destruction in Slab destructor

---

## Free List Management

### Structure

The free list is a singly-linked list using the `next` field of the slot's storage.

```
free_list_head_ ──→ Slot[i].next ──→ Slot[j].next ──→ ... ──→ end_of_free_list
```

### Sentinel Value

```cpp
static constexpr size_type end_of_free_list = ++size_type(index_type::mask);
// This equals 2^IndexBits, e.g., 0x10000 for 16-bit indices
```

**Key insight**: Because `size_type` has `IndexBits + 1` bits, it can hold the value `2^IndexBits` which is one past the maximum valid index. This sentinel value:
- Cannot be confused with any valid index
- Allows **all** indices (0 to `2^IndexBits - 1`) to be used for storing values
- Fits in `size_type` but not `index_type`

This is why the Slot's next-link uses `size_type`, not `index_type`.

### Operations

#### Allocate from Free List

```cpp
index_type allocate_slot() {
    if (free_list_head_ == end_of_free_list) {
        if (not allocate_new_slab()) {
            return index_type{};  // No more room - return null index
        }
    }
    index_type idx = index_type(free_list_head_);  // Safe: not sentinel
    Slot& slot = get_slot(idx);
    free_list_head_ = slot.next();  // May be end_of_free_list
    return idx;
}
```

#### Return to Free List

```cpp
void free_slot(index_type idx) {
    Slot& slot = get_slot(idx);
    slot.set_next(free_list_head_);  // size_type implicit conversion
    free_list_head_ = size_type(idx);
}
```

### Slab Initialization

When a new slab is created with base index `base`:

```cpp
// Link all slots in the new slab
for (size_type i = 0; i < slots_per_slab_ - 1; ++i) {
    slab->slot(i).set_next(size_type(base + i + 1));
    slab->slot(i).set_version(version_type{0});
}
// Last slot in slab points to old free list head
slab->slot(slots_per_slab_ - 1).set_next(free_list_head_);
slab->slot(slots_per_slab_ - 1).set_version(version_type{0});

// New free list head is first slot in new slab
free_list_head_ = size_type(base);
```

---

## Slab Recycling

### When Recycling Occurs

A slab becomes exhausted when all its slots are DEAD:
```cpp
slab->dead_count() == slots_per_slab_
// or equivalently: slab->can_be_recycled()
```

### Recycling Process

When a slab at `slab_index` becomes exhausted:

1. **Check if there's room for more slabs**:
   - If `next_slab_base_index_ + slots_per_slab_ <= end_of_free_list`: Can recycle
   - Otherwise: Delete the slab and set `slabs_[slab_index] = nullptr`

2. **Recycle the slab** using `Slab::recycle(first_index, last_next)`:
   ```cpp
   // Move slab to next available position
   size_type new_slab_index = next_slab_base_index_ >> log2_slots_per_slab_;

   // Ensure vector is large enough
   if (new_slab_index >= slabs_.size()) {
       slabs_.resize(new_slab_index + 1, nullptr);
   }

   // Recycle resets dead_count, versions to 0, and links slots
   slab->recycle(index_type(next_slab_base_index_), free_list_head_);

   // Update bookkeeping
   slabs_[slab_index] = nullptr;
   slabs_[new_slab_index] = std::move(slab);
   free_list_head_ = size_type(next_slab_base_index_);
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
  - If `2^IndexBits * sizeof(Slot) <= 2MB`: Use single slab covering all indices
  - Otherwise: Use 4096 slots per slab (or largest power of 2 that fits)
- **Postconditions**: `is_empty() == true`, `size() == 0`
- **Throws**: `std::bad_alloc` if initial slab allocation fails

#### Explicit Size Constructor

```cpp
explicit SlotMap(size_type slots_per_slab);
```

- **Preconditions**:
  - `slots_per_slab > 0`
  - `slots_per_slab` is a power of 2
  - `slots_per_slab <= 2^IndexBits`
- **Effect**: Constructs an empty SlotMap with specified slab size
- **Throws**:
  - `std::invalid_argument` if `slots_per_slab` is not a power of 2 or exceeds max
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
- **Note**: Slab destructor uses alive bitmap to find and destroy alive values

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
- **Note**: Null keys (index=0, version=0) return `false` via normal validation because slot 0 always has version ≥1

#### contains()

```cpp
bool contains(key_type key) const;
```

- **Returns**: `true` if `key` refers to an alive element, `false` otherwise
- **Implementation**: Delegates to `use()` with a no-op callable

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
- **Note**: The returned key is never the null key. Slot 0 of the first slab starts at version 1 to ensure this; all other slots may return version 0 on their first use.

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

- **Effect**: Invokes `func` for each alive element
- **Returns**: Number of elements visited (may be less than `size()` if early exit)
- **Callable Signatures** (any of the following):
  - `void(key_type, T&, Break&)` - full access with early exit
  - `void(key_type, T&)` - key and value access
  - `void(T&, Break&)` - value access with early exit
  - `void(T&)` - value access only
  - (const overload uses `T const&` instead of `T&`)
- **Early Exit**: Set `break_tag.stop = true` to stop iteration (only available with `Break&` signatures)
- **Iteration Order**: Unspecified, but iterates over slabs in order and slots within slab in order
- **Undefined Behavior**: Modifying the SlotMap (emplace/erase) during iteration

#### Break

```cpp
struct Break {
    bool stop = false;
};
```

---

## Implementation Details

### File Structure

```
src/wjh/slotmap/
├── Key.hpp              # Key template with strong types
├── Key.ipp              # Key implementation
├── SlotMap.hpp          # Main SlotMap template declaration
├── SlotMap.ipp          # SlotMap implementation
├── detail.hpp           # TypeBase, hash, other utilities
└── detail/
    ├── Slot.hpp         # Slot class declaration
    ├── Slot.ipp         # Slot implementation
    ├── Slab.hpp         # Slab class declaration
    └── Slab.ipp         # Slab implementation
```

### Slab Implementation

#### Slab Class

```cpp
namespace wjh::slotmap::detail {

template <typename T, typename IndexT, typename VersionT, typename SizeT>
class Slab {
public:
    using slot_type = Slot<T, SizeT, VersionT>;  // Note: SizeT for next-link

    struct EmplaceResult {
        VersionT version;  // Version to use in returned key
        SizeT next;        // Next free slot (captured before emplace overwrites storage)
    };

    static std::unique_ptr<Slab> create(SizeT slots_per_slab);

    // Lifecycle management (uses alive bitmap)
    template <typename... Args>
    EmplaceResult emplace(IndexT index, Args&&... args);
    bool destroy(IndexT index);  // Returns false if slot is now dead
    bool is_alive(IndexT index) const noexcept;

    // Slot access
    slot_type& slot(IndexT index) noexcept;
    slot_type const& slot(IndexT index) const noexcept;

    // Dead count tracking
    SizeT dead_count() const noexcept;
    bool can_be_recycled() const noexcept;
    void recycle(IndexT first_index, SizeT last_next);

private:
    naked_size_type dead_count_;
    naked_size_type slots_per_slab_;
    // Followed by: slot array, then alive bitmap
};

} // namespace wjh::slotmap::detail
```

#### Slot Class

```cpp
namespace wjh::slotmap::detail {

template <typename T, typename SizeT, typename VersionT>
class Slot {
public:
    // Version access (always valid)
    VersionT version() const noexcept;
    void set_version(VersionT v) noexcept;

    // Free-list access (only when FREE) - uses SizeT for sentinel
    SizeT next() const noexcept;
    void set_next(SizeT i) noexcept;

    // Value access (only when ALIVE)
    template <typename... Args>
    T& emplace(Args&&... args);
    void destroy() noexcept(std::is_nothrow_destructible_v<T>);
    T& value() noexcept;
    T const& value() const noexcept;

private:
    alignas(...) std::array<std::byte, ...> storage_;
    std::array<std::byte, sizeof(VersionT)> version_bytes_;
};

} // namespace wjh::slotmap::detail
```

### Key Validation

To validate a key:

```cpp
bool is_valid(key_type key) const {
    if (key.is_null()) return false;

    index_type idx = key.index();
    if (size_type(idx) >= size_type(next_slab_base_index_)) return false;

    auto slab_idx = static_cast<std::size_t>(idx >> log2_slots_per_slab_);
    if (slab_idx >= slabs_.size() || slabs_[slab_idx] == nullptr) return false;

    return slabs_[slab_idx]->is_alive(idx) &&
           slabs_[slab_idx]->slot(idx).version() == key.version();
}
```

### Helper Methods

```cpp
private:
    // Get slot by absolute index
    slot_type& get_slot(index_type idx) noexcept {
        auto slab_idx = static_cast<std::size_t>(idx >> log2_slots_per_slab_);
        auto slot_idx = static_cast<size_type>(idx & (slots_per_slab_ - 1));
        return slabs_[slab_idx]->slot(slot_idx);
    }

    // Allocate a new slab if possible
    bool allocate_new_slab() {
        if (size_type(next_slab_base_index_) >= end_of_free_list) return false;

        auto new_slab_idx = static_cast<std::size_t>(
            next_slab_base_index_ >> log2_slots_per_slab_);
        if (new_slab_idx >= slabs_.size()) {
            slabs_.resize(new_slab_idx + 1, nullptr);
        }

        auto slab = slab_type::create(size_type(slots_per_slab_));
        initialize_slab_free_list(slab.get(), index_type(next_slab_base_index_));

        slabs_[new_slab_idx] = std::move(slab);
        next_slab_base_index_ += slots_per_slab_;
        return true;
    }

    void initialize_slab_free_list(slab_type* slab, index_type base) {
        for (size_type i{0}; i.value < slots_per_slab_ - 1; ++i.value) {
            auto idx = index_type(base.value + i.value);
            slab->slot(idx).set_next(size_type(idx.value + 1));
            slab->slot(idx).set_version(version_type{0});
        }
        auto last_idx = index_type(base.value + slots_per_slab_ - 1);
        slab->slot(last_idx).set_next(free_list_head_);
        slab->slot(last_idx).set_version(version_type{0});
        free_list_head_ = size_type(base);
    }
```

### Constants

```cpp
// Maximum valid index (all IndexBits set to 1)
static constexpr index_type null_index = index_type(index_type::mask);

// Free list sentinel (2^IndexBits, one past max valid index)
static constexpr size_type end_of_free_list = ++size_type(index_type::mask);

// Maximum version before slot becomes dead
static constexpr version_type max_version = version_type(version_type::mask);
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

    SUBCASE("slab size cannot exceed max size") {
        // With 4 index bits, max size is 2^4 = 16
        CHECK_THROWS_AS(
            SlotMap<Key<4, 4, 0, int>>(32),  // Exceeds 16
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
        CHECK(not key.is_null());
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
        CHECK(not map.contains(key));
        CHECK(map.size() == 0);
    }

    SUBCASE("erase returns false for invalid key") {
        auto key = map.emplace(42);
        map.erase(key);
        CHECK(not map.erase(key));  // Already erased
    }

    SUBCASE("erase returns false for null key") {
        CHECK(not map.erase(Key<16, 16, 0, int>::null()));
    }

    SUBCASE("erased key becomes invalid, new key at same index has different version") {
        auto key1 = map.emplace(1);
        auto v1 = key1.version();
        map.erase(key1);
        CHECK(not map.contains(key1));  // Old key is invalid

        auto key2 = map.emplace(2);
        CHECK(map.contains(key2));   // New key is valid
        // If same index reused, version will have incremented
        if (key2.index() == key1.index()) {
            CHECK(key2.version().value == v1.value + 1);
        }
    }
}
```

#### Null Key Tests

```cpp
TEST_CASE("SlotMap null key handling") {
    SlotMap<Key<16, 16, 0, int>> map;
    auto null_key = Key<16, 16, 0, int>::null();

    SUBCASE("contains returns false for null key") {
        CHECK(not map.contains(null_key));
    }

    SUBCASE("use returns false for null key") {
        bool called = false;
        CHECK(not map.use(null_key, [&](int&) { called = true; }));
        CHECK(not called);
    }

    SUBCASE("erase returns false for null key") {
        CHECK(not map.erase(null_key));
    }

    SUBCASE("pop returns nullopt for null key") {
        CHECK(not map.pop(null_key).has_value());
    }

    SUBCASE("emplace never returns null key") {
        auto key = map.emplace(42);
        CHECK(not key.is_null());
        // Note: version may be 0 for non-first-slab slots, but the
        // combination of (index=0, version=0) is prevented
    }
}
```

#### Capacity Tests

```cpp
TEST_CASE("SlotMap capacity exhaustion") {
    // Small index space: 4 bits = 16 usable indices (all of them!)
    SlotMap<Key<4, 4, 0, int>> map(4);  // 4 slots per slab

    SUBCASE("returns null when capacity exhausted") {
        std::vector<Key<4, 4, 0, int>> keys;
        for (int i = 0; i < 16; ++i) {  // All 16 indices usable
            auto key = map.emplace(i);
            CHECK(not key.is_null());
            keys.push_back(key);
        }

        // 17th emplace should fail
        auto overflow_key = map.emplace(999);
        CHECK(overflow_key.is_null());
    }
}
```

#### Version Exhaustion Tests

```cpp
TEST_CASE("SlotMap version exhaustion") {
    // 2-bit version: versions 0, 1, 2, 3; max_version = 3
    // A slot can be used 4 times (versions 0, 1, 2, 3) before becoming dead
    SlotMap<Key<8, 2, 0, int>> map(4);

    SUBCASE("slot becomes dead after max version") {
        // First slot (index 0) starts at version 1 due to null-key avoidance
        auto key1 = map.emplace(1);  // version 1
        auto idx1 = key1.index();
        map.erase(key1);

        auto key2 = map.emplace(2);  // version 2, same index
        CHECK(key2.index() == idx1);
        map.erase(key2);

        auto key3 = map.emplace(3);  // version 3 (max), same index
        CHECK(key3.index() == idx1);
        map.erase(key3);  // Slot is now dead

        // Next emplace should get different index (slot at idx1 is dead)
        auto key4 = map.emplace(4);
        CHECK(key4.index() != idx1);
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
```

---

## Implementation Checklist

### Phase 1: Core Infrastructure ✅ COMPLETED

- [x] Create `src/wjh/slotmap/detail/Slot.hpp` and `Slot.ipp`
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

- [x] Create `src/wjh/slotmap/detail/Slab.hpp` and `Slab.ipp`
  - [x] Slab class with flexible array pattern (slots after header)
  - [x] Factory method `Slab::create(slots_per_slab)` returns `std::unique_ptr<Slab>`
  - [x] **Alive bitmap** for tracking which slots have values
  - [x] `emplace()` method that sets alive bit and returns version
  - [x] `destroy()` method that clears alive bit, increments version, tracks dead count
  - [x] `is_alive()` method for O(1) alive checking
  - [x] `can_be_recycled()` and `recycle()` methods
  - [x] Proper alignment via `alignas()` on class
  - [x] Uses `std::launder` for slot array access
  - [x] Private constructor enforces use of `create()`
  - [x] Non-copyable, non-movable
  - [x] Tests in `src/wjh/slotmap/tests/Slab_ut.cpp`

**Implementation Notes from Phase 1:**
- Slot uses `std::array<std::byte, ...>` for storage instead of union to avoid union-related UB
- Slab uses flexible array member pattern: header followed by contiguous slot array, then alive bitmap
- `std::launder` is used in both classes for correct pointer provenance after placement new
- Property-based tests use `rc::check("description", []() { ... })` pattern
- **Slot uses `size_type` for next-link** to allow storing `end_of_free_list` sentinel

---

### Phase 2: Basic SlotMap ✅ COMPLETED

- [x] Create `src/wjh/slotmap/SlotMap.hpp` and `SlotMap.ipp`
  - [x] Template for `KeyT` with `is_key_v<KeyT>` static_assert
  - [x] Type aliases using strong types from Key
  - [x] Member variables (slabs vector, free list, size, etc.)
  - [x] `end_of_free_list` constant (`2^IndexBits`, one past max index)
  - [x] `free_list_head_` uses `size_type` to hold sentinel
  - [x] Default constructor with slab size heuristic
  - [x] Explicit slab size constructor with power-of-2 validation
  - [x] Move constructor and move assignment (noexcept)
  - [x] `is_empty()` and `size()` capacity methods
  - [x] `get_slot()` helper method
  - [x] `clear_slabs()` helper method
  - [x] Tests in `src/wjh/slotmap/tests/SlotMap_ut.cpp`

**Implementation Notes from Phase 2:**
- **All indices are usable**: No index wasted as sentinel (unlike original design)
- `size_type` has `IndexBits + 1` bits, allowing it to hold `2^IndexBits`
- `end_of_free_list = 2^IndexBits` serves as free list sentinel
- Slab's `slot_type` uses `size_type` for next-link to store sentinel
- `Slab::recycle()` takes `size_type last_next` to handle sentinel
- Strong types (`index_type`, `size_type`, etc.) used throughout for type safety

---

## 🎉 IMPLEMENTATION COMPLETE

**Status:** All phases (1-7) are complete. The SlotMap implementation is fully functional and tested.

**What's done:**
- `src/wjh/slotmap/detail/Slot.hpp` / `Slot.ipp` - Complete with tests
- `src/wjh/slotmap/detail/Slab.hpp` / `Slab.ipp` - Complete with tests (includes alive bitmap, emplace, destroy, clone, recycle)
- `src/wjh/slotmap/SlotMap.hpp` / `SlotMap.ipp` - Full implementation with copy/move/swap, pop, reserve, slab recycling
- `src/wjh/slotmap/tests/SlotMap_ut.cpp` - Comprehensive tests including exception safety, edge cases, and property-based tests
- All tests pass: `ctest --output-on-failure --test-dir build`

**Key design decisions:**

1. **Strong types everywhere**: `index_type`, `version_type`, `size_type` are all strong types with `.value` member.

2. **`size_type` has IndexBits+1 bits**: This allows storing `end_of_free_list = 2^IndexBits` which is the free list sentinel.

3. **Slot uses `size_type` for next-link**: So it can store the sentinel.

4. **Version semantics**: Slots start at version 0 (except slot 0 of first slab which starts at 1 for null-key avoidance). The version in the key is the slot's version at emplace time. A slot with N-bit version can be used 2^N times before becoming dead.

5. **Slab manages lifecycle**: `Slab::emplace(index, args...)` returns `EmplaceResult{version, next}` and `Slab::destroy(index)` handles the alive bitmap and version management.

6. **All indices usable**: Indices 0 through `2^IndexBits - 1` can all store values. None are reserved.

7. **Null key handling**: No explicit null key checks. Null keys (version=0, index=0) fail validation naturally because slot 0 has version ≥1.

8. **`for_each()` flexibility**: Supports multiple callable signatures: `(key, value, Break&)`, `(key, value)`, `(value, Break&)`, or `(value)`.

9. **Slab recycling**: When all slots in a slab become dead (version exhausted), `try_recycle_slab()` moves it to a new index position with reset versions.

10. **Valid key sizes**: Only 32-bit, 64-bit, and 128-bit keys are supported (determined by `storage_type` specializations in detail.hpp).

---

### Phase 3: Core Operations ✅ COMPLETED

- [x] Implement `emplace()`
  - [x] Check if free list empty, allocate new slab if needed
  - [x] Pop from free list (handle `end_of_free_list` sentinel)
  - [x] Use `Slab::emplace()` which returns `EmplaceResult{version, next}`
  - [x] Build and return key with index, version, user=0
  - [x] Increment size
  - [x] Exception safety: if construction fails, slot stays in free list

- [x] Implement `erase()`
  - [x] Key validation via `get_slab()` null check, version match, alive check
  - [x] Use `Slab::destroy()` which handles alive bit, version increment, dead count
  - [x] If `destroy()` returns true: add to free list
  - [x] If `destroy()` returns false: slot is dead (TODO: Phase 6 slab recycling)
  - [x] Decrement size
  - [x] Return true on success

- [x] Implement `use()` and `contains()`
  - [x] Const `use()` does validation and invokes callable
  - [x] Non-const `use()` delegates to const version with const_cast
  - [x] `contains()` delegates to `use()` with no-op callable
  - [x] No explicit null key checks (handled by version mismatch)

- [x] Implement `allocate_new_slab()` helper
  - [x] Check if `next_slab_base_index_ >= end_of_free_list`
  - [x] Create slab, initialize free list links
  - [x] Special case: slot 0 of first slab gets version 1 (null key avoidance)
  - [x] Update slabs vector, next_slab_base_index_

- [x] Tests for all public interfaces

**Implementation Notes from Phase 3:**
- `Slab::emplace()` now returns `EmplaceResult` struct with both version and next (captures next before emplace overwrites storage)
- `next_slab_base_index_` changed from `naked_index_type` to `naked_size_type` to avoid overflow when index space is exhausted
- Non-const methods delegate to const versions to eliminate duplication
- Comprehensive property-based tests verify interleaved emplace/erase consistency

### Phase 4: Iteration and Bulk Operations ✅ COMPLETED

- [x] Implement `for_each()`
  - [x] Iterate over slabs
  - [x] Skip nullptr slabs
  - [x] Use `Slab::is_alive()` to find alive slots
  - [x] Build key from index + version for callback
  - [x] Early exit support with `break_t`

- [x] Implement `clear()`
  - [x] Iterate and destroy all alive values (use Slab::destroy)
  - [x] Rebuild free list (skip dead slots)
  - [x] Reset size to 0

- [x] Implement `reset()`
  - [x] Clear all slabs via `clear_slabs()` helper
  - [x] Returns to initial state (empty, ready for new allocations)

- [x] Tests for all public interfaces

**Implementation Notes from Phase 4:**
- `for_each()` non-const delegates to const version with const_cast
- `for_each()` reconstructs keys from index + version during iteration
- `clear()` uses `Slab::destroy()` for alive slots (properly clears alive bit and increments version)
- `clear()` rebuilds free list by iterating all slots, skipping dead ones
- `reset()` simply calls `clear_slabs()` which deallocates all slabs
- Comprehensive property-based tests verify iteration visits all elements and clear/reset behavior

### Phase 5: Copy/Move Operations ✅ COMPLETED

- [x] Move operations already implemented ✅
- [x] Implement copy constructor (if T is copyable)
- [x] Implement copy assignment (copy-and-swap)
- [x] Implement `swap()`
- [x] Tests for all public interfaces

**Implementation Notes from Phase 5:**
- `Slab::clone()` method added to support deep copying of slabs
- Copy constructor clones all non-null slabs and copies metadata fields
- Copy assignment uses copy-and-swap idiom for strong exception safety
- `swap()` exchanges all member fields between two SlotMaps
- Copy operations are only available when `std::is_copy_constructible_v<T>` is true
- Comprehensive property-based tests verify copy independence and data preservation

### Phase 6: Additional Features ✅ COMPLETED

- [x] Implement `pop()`
- [x] Implement `reserve()`
- [x] Implement slab recycling (in erase when slab becomes exhausted)
- [x] Tests for all public interfaces

**Implementation Notes from Phase 6:**
- `pop(key)` returns `std::optional<mapped_type>`, moving the value out before erasing. Only available when `std::is_move_constructible_v<T>` is true.
- `reserve(n)` pre-allocates slabs to hold at least n elements, avoiding allocations during hot paths.
- Slab recycling: when a slot's version is exhausted, `destroy()` returns false and the slot is marked dead. When ALL slots in a slab are dead (`can_be_recycled()`), `try_recycle_slab()` moves the slab to a new index position and resets all versions to 0.
- `Slab::recycle(first_index, last_next)` resets dead_count, re-initializes the free list chain within the slab, and clears the bitmap.
- Recycling only occurs if there's room in the index space for the new slab position.

### Phase 7: Testing ✅ COMPLETED

- [x] Property-based tests with rapidcheck
- [x] Exception safety tests
- [x] Edge case tests (1-bit fields, capacity limits)
- [x] Static assertions for type traits

**Implementation Notes from Phase 7:**
- Exception safety tests verify strong guarantee for emplace, copy constructor, and copy assignment
- Edge case tests cover 1-bit version fields, 1-bit index fields, maximum version exhaustion, single-slot slabs, maximum slab sizes, and 64-bit keys
- Static assertions verify type traits for SlotMap (constructible, movable, copyable) and Key (trivially copyable, is_key_v, sizes)
- Property-based tests added for version exhaustion/recycling, clear/refill, for_each early exit, and reserve/emplace

---

## Open Questions / Future Work

1. **Thread Safety**: Could add atomic operations for free list, or sharding by slab. Deferred.

2. **Custom Allocators**: PMR support can be added by storing `memory_resource*` and using it for slab allocation.

3. **Iteration Performance**: Alive bitmap already added for efficient iteration over sparse slabs.

4. **Bulk Erase**: Could add `erase_if(predicate)` that's more efficient than individual erases.

5. **Key Introspection**: Could expose `max_index()`, `max_version()` as static members.
