# wjh::slotmap Developer Guide

## Introduction

This document explains the internal architecture, design decisions, and implementation details of `wjh::slotmap`. Read this before making changes to the library.

This is not a user guide. If you're looking to use the library, consult the API documentation. This guide is for developers who need to understand the internals to:

- Fix bugs
- Add new features
- Optimize performance
- Port to new platforms
- Understand design tradeoffs

We assume advanced C++ knowledge including templates, memory layout, alignment, placement new, `std::launder`, and the C++ object model.

---

## Architecture Overview

The slot map consists of three main components arranged in a hierarchical structure:

```
wjh::slotmap::SlotMap<KeyT>
    |
    +-- owns: std::vector<std::unique_ptr<Slab>>
    |
    +-- Slab (detail::Slab<T, IndexT, VersionT, SizeT>)
            |
            +-- contains: array of Slot objects
            +-- contains: alive bitmap
            +-- metadata: dead_count_, slots_per_slab_
                    |
                    +-- Slot (detail::Slot<T, SizeT, VersionT>)
                            |
                            +-- storage_: byte array for T or size_type
                            +-- version_bytes_: byte array for version
```

**Key insight**: The architecture uses a slab-based allocation strategy where each slab contains a fixed number of slots. Slabs are indexed via bit-shifting operations, and within each slab, slots are accessed by masking operations. This provides O(1) access while maintaining cache-friendly contiguous storage.

---

## File Structure

```
src/wjh/slotmap/
├── SlotMap.hpp      # SlotMap class declaration, includes Key.hpp
├── SlotMap.ipp      # SlotMap implementation
├── Key.hpp          # Key template declaration, includes detail.hpp
├── Key.ipp          # Key implementation
├── detail.hpp       # TypeBase, storage_type, hash functions
└── detail/
    ├── Slot.hpp     # Slot class declaration
    ├── Slot.ipp     # Slot implementation
    ├── Slab.hpp     # Slab class declaration
    └── Slab.ipp     # Slab implementation
```

### The .hpp/.ipp Pattern

We use a split declaration/implementation pattern:

- `.hpp` files contain declarations and must be includable multiple times
- `.ipp` files contain definitions (template implementations)
- Each `.hpp` includes its corresponding `.ipp` at the end

This pattern provides:
- Clear separation of interface and implementation
- Easier navigation for users

**Important**: Never include `.ipp` files directly. Always include the corresponding `.hpp` file.

---

## The Key System

### Bit Packing

Keys are bit-packed into 32, 64, or 128-bit unsigned integers according to this layout:

```
MSB                                   LSB
┌──────────┬─────────────┬──────────────┐
│ UserBits │ VersionBits │  IndexBits   │
└──────────┴─────────────┴──────────────┘
```

The total bit count must be exactly 32, 64, or 128. No other sizes are supported.

The `storage_type` template in `detail.hpp` selects the underlying type:

```cpp
template <unsigned TotalBits>
struct storage_type;

template <> struct storage_type<32> { using type = std::uint32_t; };
template <> struct storage_type<64> { using type = std::uint64_t; };
#ifdef __SIZEOF_INT128__
template <> struct storage_type<128> { using type = unsigned __int128; };
#endif
```

If you attempt to use an invalid total (e.g., 48 bits), compilation will fail due to no matching `storage_type` specialization.

### Strong Types (TypeBase)

All bit fields are wrapped in strong types derived from `detail::TypeBase<N, Derived>`:

```cpp
template <unsigned N, typename DerivedT>
struct TypeBase {
    static constexpr unsigned num_bits = N;
    using value_type = type_with_at_least_t<num_bits>;  // uint8_t, uint16_t, etc.
    static constexpr value_type mask = /* all N bits set */;
    value_type value;

    // Comparison operators, increment, etc.
};
```

This provides:

1. **Compile-time size determination**: The `num_bits` constant is available for `static_assert` and template logic
2. **Type safety**: Cannot mix `index_type` and `version_type` in function calls
3. **The mask constant**: Used for bit operations without runtime computation
4. **Implicit conversion to value_type**: Allows use in arithmetic contexts

Example strong types in `Key`:

```cpp
struct Index : TypeBase<IndexBits, Index> { /* ... */ };
struct Version : TypeBase<VersionBits, Version> { /* ... */ };
struct User : TypeBase<UserBits, User> { /* ... */ };
struct Size : TypeBase<IndexBits + 1, Size> { /* ... */ };  // Note: +1
```

### size_type Has IndexBits + 1 Bits

**Critical design decision**: `size_type` has one more bit than `index_type`.

Why? The free list sentinel (`end_of_free_list`) needs to be distinguishable from all valid indices. With IndexBits=16, valid indices are 0-65535. The sentinel is 65536, which requires 17 bits.

```cpp
// In Key:
struct Size : TypeBase<IndexBits + 1, Size> { /* ... */ };

// In SlotMap:
static constexpr size_type end_of_free_list = ++size_type(index_type::mask);
// e.g., for 16-bit index: end_of_free_list = 0x10000 (65536)
```

This design allows **ALL** indices (0 to 2^IndexBits - 1) to store values. No index is wasted as a sentinel. The sentinel value lives in the extra bit of `size_type`.

**Implications**:

1. Slot's next-pointer uses `size_type` (not `index_type`) to store the sentinel
2. `Slot<T, IndexT, VersionT>` is actually instantiated as `Slot<T, SizeT, VersionT>` where `SizeT = Key::size_type`
3. Free list operations must handle the sentinel value correctly
4. If IndexBits is a power of two, the stored next field will be bigger, but this only really matters when the mapped type is smaller.

### KeyBase and Triviality

`Key` inherits from `KeyBase<ValueType, TagType>`:

```cpp
template <typename ValueTypeT, typename TagTypeT>
struct KeyBase {
    using value_type = ValueTypeT;
    using tag_type = TagTypeT;
    value_type bits_;

    constexpr KeyBase() : bits_(0) { }  // Zero-initialized
protected:
    explicit constexpr KeyBase(value_type b) : bits_(b) { }
};
```

But there's a specialization for `Trivial<T>`:

```cpp
template <typename ValueTypeT, typename TagTypeT>
struct KeyBase<ValueTypeT, Trivial<TagTypeT>> {
    using value_type = ValueTypeT;
    using tag_type = TagTypeT;
    value_type bits_;

    constexpr KeyBase() = default;  // Trivially default constructible (uninitialized)
protected:
    explicit constexpr KeyBase(value_type b) : bits_(b) { }
};
```

This enables two key types:

- `Key<T, I, V, U>`: Default-constructed keys have zero-initialized bits
- `TrivialKey<T, I, V, U>` (alias for `Key<Trivial<T>, I, V, U>`): Default-constructed keys have uninitialized bits (faster in performance-critical code)

The `tag_type` is extracted from the base, and since the specialization wraps `Trivial<TagTypeT>`, it unwraps to just `TagTypeT` for API purposes.

### Bit Manipulation Helpers

Keys use safe shift operations to handle edge cases:

```cpp
static constexpr value_type safe_shift_left(value_type val, unsigned shift) noexcept {
    if (shift >= num_bits) {
        return value_type{0};
    }
    return val << shift;
}
```

Why? If `UserBits == 0`, then `user_shift == num_bits`, and shifting by the width of the type is undefined behavior in C++. These helpers ensure well-defined behavior for all bit configurations.

---

## The Slot Class

### Memory Layout

```cpp
template <typename T, typename IndexT, typename VersionT>
class Slot {
    alignas(std::max(alignof(IndexT), alignof(T)))
        std::array<std::byte, std::max(sizeof(IndexT), sizeof(T))> storage_;

    std::array<std::byte, sizeof(VersionT)> version_bytes_;
};
```

The slot stores EITHER:
- A `T` value (when alive)
- An `IndexT` next-pointer (when free)

Both share the same `storage_` byte array. This is NOT a union - we use byte arrays with placement new/`std::launder` to comply with the C++ object model and avoid union-related undefined behavior.

**Important**: The slot actually stores `size_type` (not `index_type`) for the next-pointer to accommodate the sentinel value. Note the template parameter is `IndexT` but this is actually `size_type` from the Slab's perspective:

```cpp
// In Slab.hpp:
using slot_type = Slot<T, size_type, VersionT>;  // Note: size_type for next-link
```

### State Tracking (Alive Bit in Version)

The Slot internally uses the high bit of the version to track alive/free state:

```cpp
using naked_version_type = typename version_type::value_type;
static constexpr auto version_digits = std::numeric_limits<naked_version_type>::digits;
static constexpr naked_version_type alive_bit = naked_version_type(1) << (version_digits - 1);
```

Operations:
- `set_alive()` sets this bit
- `set_free()` clears this bit
- The alive bit is NOT exposed in the public API - it's internal bookkeeping
- The version returned to users has this bit masked off

**Why use the high bit of version_type's underlying storage?**

The `version_type` may have fewer bits than its underlying `value_type`. For example, a 7-bit version uses `uint8_t` as storage. We have one unused bit (bit 7) that we can repurpose for the alive flag.

However, if `version_type::num_bits == version_digits` (all bits are used), we cannot use the alive bit validation.

```cpp
constexpr version_type version() const noexcept {
    if constexpr (version_type::num_bits < version_digits) {
        return version_type(naked_version_type(
            std::bit_cast<naked_version_type>(version_bytes_) & ~alive_bit));
    } else {
        return std::bit_cast<version_type>(version_bytes_);
    }
}
```

I wanted to have some ability to inject debugging checks, without increasing the size of the slot. We only get this information and checking when there is room, but that is fine since we can devise tests to leave at least one bit free in the version field.

### Debug Mode Alive Bit Tracking

The alive bit tracking is controlled by the `WJH_SLOTMAP_DEBUG_MODE` preprocessor macro. CMake automatically defines this macro for Debug builds using a generator expression in `src/wjh/slotmap/CMakeLists.txt`:

```cmake
target_compile_definitions(wjh_slotmap
        INTERFACE
        $<$<CONFIG:Debug>:WJH_SLOTMAP_DEBUG_MODE>)
```

This approach works correctly with both single-config generators (Make, Ninja) and multi-config generators (Xcode, Visual Studio).

**Conditional Compilation:**

The alive bit tracking is only enabled when BOTH conditions are true:
1. `WJH_SLOTMAP_DEBUG_MODE` is defined (automatically in Debug builds)
2. There's room in the version bytes: `version_type::num_bits < version_digits`

```cpp
// In Debug mode (when conditions above are met):
constexpr bool is_free() const noexcept {
    return not (std::bit_cast<naked_version_type>(version_bytes_) & alive_bit);
}

// In Release mode (or when no room in version bytes):
constexpr bool is_free() const noexcept {
    return true;  // No checking - assume correct usage
}
```

**Assertions Enabled by Debug Mode:**

When debug mode is active, the following operations include meaningful assertions:

- `next()`: Asserts that the slot is free before accessing the next-pointer
- `set_next()`: Asserts that the slot is free before setting the next-pointer
- `emplace()`: Asserts that the slot is free before constructing a value
- `destroy()`: Asserts that the slot is alive before destroying a value
- `value()`: Asserts that the slot is alive before accessing the value

**Zero-Cost Abstraction:**

In Release builds, all the debug checks compile away:
- `is_free()` and `is_alive()` always return `true`
- The conditionals are resolved at compile time via `if constexpr`
- No runtime overhead - the generated code is identical to having no checks

**Manual Override:**

Users can manually control debug mode by defining or undefining `WJH_SLOTMAP_DEBUG_MODE` before including the library:

```cpp
// Force debug mode even in Release build
#define WJH_SLOTMAP_DEBUG_MODE
#include <wjh/slotmap/SlotMap.hpp>

// Or force it off in Debug build
#undef WJH_SLOTMAP_DEBUG_MODE
#include <wjh/slotmap/SlotMap.hpp>
```

### std::launder Usage

When accessing the stored `T` or `IndexT` through the byte array, we use `std::launder`:

```cpp
template <typename U>
constexpr U* get() {
    return std::launder(reinterpret_cast<U*>(storage_.data()));
}
```

**Why is this required?**

After placement new, the pointer obtained from `storage_.data()` may not have the correct provenance to access the newly created object. The C++ standard requires "laundering" the pointer to obtain a valid pointer to the new object.

From [basic.life] in the standard: A pointer to a byte array does not point to an object created within that storage after placement new, until the pointer is appropriately adjusted or laundered.

Using `std::launder` tells the compiler: "I know I did placement new here, give me a pointer to the actual object."

**Why use std::bit_cast for version_bytes_?**

The version is stored as a byte array to avoid padding issues. We need to convert between `std::array<std::byte, N>` and the version type. `std::bit_cast` is the safe, constexpr way to reinterpret the bytes as a different type, without violating strict aliasing or type-punning rules.

---

## The Slab Class

### Memory Layout (Flexible Array Pattern)

Slab uses a "flexible array member" pattern (also called "struct hack"). The allocation looks like:

```
┌────────────────────────────────────────────────────────────┐
│ Slab header (dead_count_, slots_per_slab_)                 │
├────────────────────────────────────────────────────────────┤
│ Slot[0] │ Slot[1] │ Slot[2] │ ... │ Slot[slots_per_slab-1] │
├────────────────────────────────────────────────────────────┤
│ Alive bitmap (ceil(slots_per_slab / 8) bytes)              │
└────────────────────────────────────────────────────────────┘
```

The total allocation size is calculated by `total_bytes_needed(slots_per_slab)`:

```cpp
static constexpr std::size_t total_bytes_needed(size_type slots_per_slab) {
    auto const slots_bytes = slots_per_slab.value * sizeof(slot_type);
    auto const bitmap_bytes = bitmap_size(slots_per_slab);
    auto const bytes_needed = sizeof(Slab) + slots_bytes + bitmap_bytes;
    return bytes_needed;
}
```

**Important**: The slot array is NOT a member of the Slab class. It's allocated in memory immediately following the Slab object. We access it via pointer arithmetic:

```cpp
slot_type* slots() noexcept {
    return std::launder(reinterpret_cast<slot_type*>(this + 1));
}
```

`this + 1` points to the byte immediately after the Slab object, which is where we placement-constructed the slot array.

### Factory Pattern

Slab uses a factory method because:

1. **Constructor is private**: Can't be called directly by users
2. **Allocation size depends on runtime parameter**: `slots_per_slab` is not a template parameter
3. **Custom `operator delete` is needed**: For proper deallocation with alignment

```cpp
static std::unique_ptr<Slab> create(size_type slots_per_slab) {
    // Allocate raw memory with proper alignment
    void* raw = ::operator new(
        total_bytes_needed(slots_per_slab),
        std::align_val_t{alignof(Slab)});

    // Construct the Slab header
    auto* slab = ::new (raw) Slab(slots_per_slab);

    // Construct all the slots (value-initialized to zero)
    auto* slot_mem = reinterpret_cast<std::byte*>(slab + 1);
    for (naked_size_type i = 0; i < slots_per_slab; ++i) {
        ::new (static_cast<void*>(slot_mem)) slot_type{};
        slot_mem += sizeof(slot_type);
    }

    // Zero the bitmap (slot_mem now points to bitmap start)
    std::memset(slot_mem, 0, bitmap_size(slots_per_slab));

    return std::unique_ptr<Slab>(slab);
}
```

**Why use placement new in a loop instead of array new?**

Array new (`new slot_type[N]`) would require `slot_type` to be default-constructible and would store a size prefix. Placement new gives us full control over initialization and avoids hidden size storage.

**Why return unique_ptr?**

RAII ensures proper cleanup. The custom deleter is handled by Slab's `operator delete`, which the unique_ptr will call automatically.

### Alive Bitmap

Each slab maintains a bitmap where bit N indicates slot N is alive:

```cpp
std::byte* bitmap() noexcept {
    auto* slot_end = reinterpret_cast<std::byte*>(slots() + slots_per_slab_);
    return std::launder(slot_end);
}
```

Operations:

```cpp
bool is_alive(index_type index) const noexcept {
    auto const byte_idx = static_cast<std::size_t>(index) / 8;
    auto const bit_idx = static_cast<unsigned>(index % 8);
    return (bitmap()[byte_idx] & (std::byte{1} << bit_idx)) != std::byte{0};
}

void set_alive(index_type index, bool alive) noexcept {
    auto const byte_idx = static_cast<std::size_t>(index) / 8;
    auto const bit_idx = static_cast<unsigned>(index % 8);
    auto const mask = std::byte{1} << bit_idx;

    if (alive) {
        bitmap()[byte_idx] |= mask;
    } else {
        bitmap()[byte_idx] &= ~mask;
    }
}
```

**Why a separate bitmap?**

You may have noticed an alive bit in the version_bytes_ of each slot. Why duplicate?

The short answer is that the slot's alive bit is only there for bug checking, and may not be there at all.

### EmplaceResult

`Slab::emplace()` returns a struct:

```cpp
struct EmplaceResult {
    version_type version;  // Version to use in the key
    size_type next;        // Next free slot (was stored in slot before emplace)
};
```

Why return both values?

**We must capture `next` BEFORE emplacing** because the storage is shared - placing a T overwrites the next-pointer:

```cpp
template <typename... Args>
EmplaceResult emplace(index_type index, Args &&... args) {
    assert(not is_alive(index));
    auto & s = slots()[index];
    auto result = EmplaceResult{.version = s.version(), .next = s.next()};
    s.emplace(std::forward<Args>(args)...);  // Overwrites next-pointer!
    set_alive(index, true);
    return result;
}
```

The SlotMap needs the `next` value to update its free list head. If we didn't capture it before emplacing, it would be lost.

### Dead Count and Recycling

When `destroy()` is called on a slot at max version:

```cpp
bool destroy(index_type index) {
    assert(is_alive(index));
    auto & s = slots()[index];

    // Destroy the value and clear alive bit
    s.destroy();
    set_alive(index, false);

    auto ver = s.version();
    if (ver == max_version) {
        // Slot is dead - cannot be reused
        ++dead_count_;
        return false;
    }

    // Increment version for next use
    ver.value += 1;
    s.set_version(ver);
    return true;
}
```

When a slot becomes dead:
1. The slot's version is exhausted (cannot be incremented)
2. `dead_count_` is incremented
3. `destroy()` returns `false` (slot NOT added to free list)
4. When `dead_count_ == slots_per_slab_`, `can_be_recycled()` returns `true`

**Recycling process**:

```cpp
void recycle(index_type first_index, size_type last_next) {
    assert(can_be_recycled());

    dead_count_ = 0;

    // Re-link all slots into a free list chain
    auto const end = this->slots() + slots_per_slab_ - 1;
    for (auto slot = this->slots(); slot != end; ++slot) {
        first_index.value += 1;
        slot->set_version(version_type{});  // Reset version to 0
        slot->set_next(first_index);
    }
    end->set_version(version_type{});
    end->set_next(last_next);

    // Clear bitmap
    std::memset(bitmap(), 0, bitmap_size(size_type(slots_per_slab_)));
}
```

The slab is now ready to be moved to a new index position with all versions reset. This allows the same physical memory to be reused at a new logical index range.

**Why recycling?**

With finite versions, each slot has a finite lifetime (2^VersionBits uses). Without recycling, the entire slab would be abandoned when all slots are dead. Recycling allows the physical memory to be reused at a new index range rather than having to allocate another slab when needed.

---

## The SlotMap Class

### Member Variables

```cpp
std::vector<std::unique_ptr<slab_type>> slabs_{};
size_type free_list_head_ = end_of_free_list;
naked_size_type size_ = 0;
naked_size_type slots_per_slab_;
unsigned log2_slots_per_slab_ = 0;
naked_size_type next_slab_base_index_ = 0;
```

**Key observations**:

1. `slabs_` may contain `nullptr` entries (from recycled slabs)
2. `free_list_head_` uses `size_type` to hold the sentinel
3. `size_` is the count of alive elements (not allocated slots)
4. `slots_per_slab_` must be a power of 2 (enforced by constructor)
5. `log2_slots_per_slab_` is cached for fast bit-shift operations
6. `next_slab_base_index_` uses `naked_size_type` (not `naked_index_type`) to detect index space exhaustion

**Why is next_slab_base_index_ a naked_size_type?**

When the index space is nearly full, `next_slab_base_index_` may equal `end_of_free_list.value`, which is `2^IndexBits`. This value does NOT fit in `naked_index_type`, but does fit in `naked_size_type` (which has IndexBits+1 bits). Using the wrong type here would cause overflow bugs.

### Index to Slab Mapping

Given a key's index, we compute:

```cpp
auto const slab_idx = static_cast<std::size_t>(idx >> log2_slots_per_slab_);
auto const slot_idx = index_type(naked_index_type(idx.value & (slots_per_slab_ - 1)));
```

**Example** (slots_per_slab = 1024, log2 = 10):

```
Index 0:    slab_idx = 0 >> 10 = 0,  slot_idx = 0 & 1023 = 0
Index 1023: slab_idx = 1023 >> 10 = 0,  slot_idx = 1023 & 1023 = 1023
Index 1024: slab_idx = 1024 >> 10 = 1,  slot_idx = 1024 & 1023 = 0
Index 2047: slab_idx = 2047 >> 10 = 1,  slot_idx = 2047 & 1023 = 1023
```

### Free List

The free list is a singly-linked list threaded through free slots:

```
free_list_head_ → slot[7].next → slot[2].next → ... → end_of_free_list
```

**Allocation** (pop from head):

```cpp
if (free_list_head_ == end_of_free_list) {
    if (not allocate_new_slab()) {
        return key_type::null();
    }
}

auto const idx = index_type(naked_index_type(free_list_head_.value));
auto* slab = get_slab(idx);
auto const slot_idx = /* compute slot within slab */;

auto [ver, next] = slab->emplace(slot_idx, std::forward<Args>(args)...);

free_list_head_ = next;  // Update head (only after successful emplace)
++size_;

return key_type(idx, ver, user_type{});
```

**Deallocation** (push to head):

```cpp
bool can_reuse = slab->destroy(slot_idx);
--size_;
if (can_reuse) {
    slot.set_next(free_list_head_);
    free_list_head_ = size_type(key_idx);
} else {
    // Slot is dead - check if slab can be recycled
    try_recycle_slab(slab_idx);
}
```

The sentinel `end_of_free_list` (2^IndexBits) marks the end. This requires `size_type` storage, which is why Slot uses `size_type` for the next-link field.

### Null Key Avoidance

Slot 0 of the first slab is initialized to version 1 (not 0):

```cpp
if (new_slab_idx == 0) {
    using naked_version_type = typename version_type::value_type;
    slab->slot(index_type(naked_index_type{0}))
        .set_version(version_type{naked_version_type{1}});
}
```

This ensures `emplace()` never returns a key with index=0, version=0 (the null key).

**Why is this important?**

The null key is a sentinel value (like `nullptr` for pointers). Users expect `Key::null()` to be invalid. If we allowed slot 0 version 0 to be used, then:

1. User constructs a default key: `Key k;` (all zero bits)
2. SlotMap emplaces at slot 0, version 0
3. User's default-constructed key is now accidentally valid!

By ensuring slot 0 starts at version 1, we guarantee the null key is never returned by `emplace()`. The null key will fail validation because slot 0's version (≥1) won't match the key's version (0).

**Why not special-case slot 0 to never be used?**

That would waste an index. With small index spaces (e.g., 4-bit index = 16 slots), wasting one slot is significant. Starting at version 1 allows us to use all indices without special-casing.

### Slab Recycling Flow

When a slot's version is exhausted:

1. `Slab::destroy()` returns `false`
2. Slot is NOT added to free list (it's dead)
3. Slab's `dead_count_` increases
4. If all slots dead: `try_recycle_slab()` is called

In `try_recycle_slab()`:

```cpp
void try_recycle_slab(std::size_t slab_idx) {
    auto* slab = slabs_[slab_idx].get();
    if (not slab || not slab->can_be_recycled()) {
        return;
    }

    // Check if there's room for more slabs in the index space
    auto const new_base = next_slab_base_index_;
    if (size_type(new_base) + size_type(slots_per_slab_) > end_of_free_list) {
        // No room for recycling - just delete the slab
        slabs_[slab_idx].reset();
        return;
    }

    // Calculate where the recycled slab will go
    auto const new_slab_idx = static_cast<std::size_t>(new_base >> log2_slots_per_slab_);

    // Ensure vector is large enough
    if (new_slab_idx >= slabs_.size()) {
        slabs_.resize(new_slab_idx + 1);
    }

    // Recycle the slab to the new position
    auto const first_index = index_type(static_cast<naked_index_type>(new_base));
    slab->recycle(first_index, free_list_head_);

    // Move slab pointer to new position
    if (new_slab_idx != slab_idx) {
        slabs_[new_slab_idx] = std::move(slabs_[slab_idx]);
    }

    // Update bookkeeping
    free_list_head_ = size_type(new_base);
    next_slab_base_index_ += slots_per_slab_;
}
```

This allows the same physical memory to be reused at a new logical index position. Old keys to that slab are invalidated (the slab is no longer at the old index), but new keys can be issued at the new index range.

---

## Version Semantics (Important!)

This is often confusing. Here's the complete lifecycle with concrete examples:

### Initial State

All slots start at version 0, **except** slot 0 of the first slab which starts at version 1 (null key avoidance).

### On emplace()

The **current** slot version is used in the returned key:

```cpp
auto [ver, next] = slab->emplace(slot_idx, std::forward<Args>(args)...);
return key_type(idx, ver, user_type{});
```

Example: Slot at version 2 → key gets version 2

### On erase()

```cpp
bool destroy(index_type index) {
    // Destroy value, clear alive bit
    s.destroy();
    set_alive(index, false);

    auto ver = s.version();
    if (ver == max_version) {
        ++dead_count_;
        return false;  // Slot is DEAD
    }

    ver.value += 1;  // Increment version
    s.set_version(ver);
    return true;  // Slot can be reused
}
```

If version < max_version: increment version, slot can be added to free list
If version == max_version: slot is DEAD (not added to free list)

### Version Values Used

With N-bit version: values 0 through 2^N-1 are all used

A slot can be used 2^N times before becoming dead

**Example** (2-bit version, max_version = 3):

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

Slot is used 4 times (versions 0, 1, 2, 3), then dead.

### Key Validity

A key is valid if:
1. The index exists (slab allocated and slot within slab)
2. The slot is alive (bitmap bit set)
3. The slot's current version matches the key's version

After erase: slot's version is incremented, so old key's version no longer matches.

**Example**:

```cpp
auto k1 = map.emplace(42);  // Returns key with index=5, version=0
map.erase(k1);              // Slot 5 now has version=1
bool valid = map.contains(k1);  // false! Slot version (1) != key version (0)

auto k2 = map.emplace(99);  // Reuses slot 5, returns key with index=5, version=1
bool valid = map.contains(k2);  // true! Versions match
bool valid = map.contains(k1);  // still false! Old key is permanently invalid
```

---

## Exception Safety Implementation

### emplace() and try_emplace()

Goal: Strong exception guarantee (if T's constructor throws, SlotMap is unchanged)

Both `emplace()` and `try_emplace()` share the same core implementation. The difference is in handling capacity exhaustion:
- `emplace()` throws `std::length_error` when capacity is exhausted
- `try_emplace()` returns the null key when capacity is exhausted

```cpp
template <typename... Args>
key_type try_emplace(Args &&... args) {
    // 1. Check capacity - NO THROW
    if (free_list_head_ == end_of_free_list) {
        if (not allocate_new_slab()) {  // MAY THROW (std::bad_alloc)
            return key_type::null();  // try_emplace returns null on exhaustion
        }
    }

    auto const idx = index_type(naked_index_type(free_list_head_.value));
    auto* slab = get_slab(idx);
    auto const slot_idx = /* compute */;

    // 2. Emplace - MAY THROW (T's constructor)
    auto [ver, next] = slab->emplace(slot_idx, std::forward<Args>(args)...);

    // 3. Update state - NO THROW (only reached if emplace succeeded)
    free_list_head_ = next;
    ++size_;

    return key_type(idx, ver, user_type{});
}

template <typename... Args>
key_type emplace(Args &&... args) {
    // 1. Check capacity - MAY THROW (std::length_error)
    if (free_list_head_ == end_of_free_list) {
        if (not allocate_new_slab()) {  // MAY THROW (std::bad_alloc)
            throw std::length_error("SlotMap capacity exhausted");  // emplace throws
        }
    }

    // Rest is identical to try_emplace
    auto const idx = index_type(naked_index_type(free_list_head_.value));
    auto* slab = get_slab(idx);
    auto const slot_idx = /* compute */;

    auto [ver, next] = slab->emplace(slot_idx, std::forward<Args>(args)...);

    free_list_head_ = next;
    ++size_;

    return key_type(idx, ver, user_type{});
}
```

**Key insight**: We don't modify `free_list_head_` or `size_` until AFTER construction succeeds.

If `T`'s constructor throws:
- The slot remains free (alive bit not set)
- `free_list_head_` still points to this slot
- `size_` is unchanged
- The SlotMap is in a valid state

**Why does this work?**

The `EmplaceResult` captures `next` before modifying the slot. If emplace throws:
1. Slot's storage is not modified (construction didn't happen)
2. Slot's next-pointer is still valid
3. Free list is intact

### Copy Constructor

Goal: Strong exception guarantee

```cpp
SlotMap(SlotMap const & other)
    : free_list_head_{other.free_list_head_}
    , size_{other.size_}
    , slots_per_slab_{other.slots_per_slab_}
    , log2_slots_per_slab_{other.log2_slots_per_slab_}
    , next_slab_base_index_{other.next_slab_base_index_}
{
    slabs_.reserve(other.slabs_.size());  // MAY THROW

    for (auto const & slab_ptr : other.slabs_) {
        if (slab_ptr) {
            slabs_.push_back(slab_ptr->clone());  // MAY THROW
        } else {
            slabs_.push_back(nullptr);
        }
    }
}
```

If any `clone()` throws:
- Partially constructed `slabs_` vector is destroyed
- RAII cleans up already-cloned slabs (unique_ptr destructors run)
- No memory leaks

### Copy Assignment

Uses copy-and-swap idiom for strong exception guarantee:

```cpp
SlotMap & operator=(SlotMap const & other) {
    if (this != &other) {
        SlotMap copy(other);  // MAY THROW
        swap(copy);           // NOEXCEPT
    }
    return *this;
}
```

If copy construction throws:
- `copy` is not constructed
- `*this` is unchanged

If copy succeeds:
- `swap()` is noexcept
- Old state is in `copy`, which is destroyed on scope exit

---

## for_each Implementation

The `for_each()` implementation handles multiple callable signatures via overload detection using `if constexpr` and `std::is_invocable_v`:

```cpp
namespace detail {
template <typename F, typename KeyT, typename ValT>
void invoke_for_each(F && func, KeyT key, ValT & val, [[maybe_unused]] Break & brk) {
    if constexpr (std::is_invocable_v<F, KeyT, ValT &, Break &>) {
        std::invoke(std::forward<F>(func), key, val, brk);
    } else if constexpr (std::is_invocable_v<F, KeyT, ValT &>) {
        std::invoke(std::forward<F>(func), key, val);
    } else if constexpr (std::is_invocable_v<F, ValT &, Break &>) {
        std::invoke(std::forward<F>(func), val, brk);
    } else {
        std::invoke(std::forward<F>(func), val);
    }
}
}
```

This allows users to pass lambdas with any of these signatures:
- `[](key_type k, T & v, Break & b) { ... }`
- `[](key_type k, T & v) { ... }`
- `[](T & v, Break & b) { ... }`
- `[](T & v) { ... }`

The compiler selects the appropriate invocation at compile-time based on what the callable accepts.

**Non-const delegates to const**:

```cpp
template <typename F>
size_type for_each(F && func) {
    return const_cast<SlotMap const &>(*this).for_each(
        [&func](key_type key, mapped_type const & v, Break & brk) {
            detail::invoke_for_each(
                std::forward<F>(func),
                key,
                const_cast<mapped_type &>(v),  // Cast away const for non-const overload
                brk);
        });
}
```

This eliminates code duplication. The const version contains the actual iteration logic, and the non-const version wraps it with const_cast.

**Why is this safe?**

The non-const `for_each()` is only called on non-const `SlotMap` objects. We know the underlying values are actually mutable. The const_cast simply restores the original mutability that was temporarily removed for code reuse.

**Key implementation details**:

1. Check `brk.stop` in both loops for early exit
2. Skip `nullptr` slabs (from recycling)
3. Reconstruct full index from `slab_idx << log2_slots_per_slab_ + slot_idx`
4. Build key from reconstructed index + slot's current version
5. Return number of elements actually visited (may be less than `size()` if early exit)

---

## Memory Management Details

### Alignment

The Slab class has explicit alignment:

```cpp
class alignas(std::max(alignof(SizeT), alignof(Slot<T, SizeT, VersionT>))) Slab { /* ... */ };
```

Why? The slab must be aligned to the most restrictive alignment of its contents. This ensures:
1. The slab header is properly aligned
2. The slot array following it is properly aligned (slots have their own alignment requirements)
3. We can safely cast pointers to these types

The alignment is passed to `operator new`:

```cpp
void* raw = ::operator new(total_bytes_needed(slots_per_slab), std::align_val_t{alignof(Slab)});
```

And must also be passed to `operator delete`:

```cpp
static void operator delete(void* ptr) {
    ::operator delete(ptr, std::align_val_t{alignof(Slab)});
}
```

**Why custom operator delete?**

C++17 requires that if you allocate with an alignment, you must deallocate with the same alignment. Forgetting this is undefined behavior (and can cause crashes on over-aligned types).

### Object Lifetime

Object lifetime in the slot map follows strict rules:

**Slot lifetime**:
1. Constructed via placement new in `Slab::create()`
2. Destroyed explicitly in `~Slab()` (must call destructor for each slot)
3. Non-copyable, non-movable (managed by containing Slab)

**Value lifetime** (T):
1. Constructed via `Slot::emplace()` using placement new
2. Destroyed via `Slot::destroy()` using `std::destroy_at()`
3. Pointer must be laundered when accessing after construction

**Slab lifetime**:
1. Allocated via `::operator new` with alignment
2. Constructed via placement new at the allocated memory
3. Destroyed via destructor (which destroys all slots)
4. Deallocated via custom `operator delete` with alignment

**Critical invariants**:

1. A value is only accessed when the alive bit is set
2. The next-pointer is only accessed when the alive bit is clear
3. Laundering is required when switching between these interpretations
4. The bitmap is the source of truth for which interpretation is valid

### Why std::launder is Required

Consider this code flow:

```cpp
// 1. Slot starts free, storage contains next-pointer
index_type* next_ptr = reinterpret_cast<index_type*>(storage_.data());

// 2. Emplace creates T in same storage
T* value_ptr = std::construct_at(reinterpret_cast<T*>(storage_.data()), args...);

// 3. Access the value - need std::launder!
T* safe_ptr = std::launder(reinterpret_cast<T*>(storage_.data()));
```

Why can't we just use `value_ptr` from step 2?

We can! `std::construct_at` returns a valid pointer. But if we want to get a pointer from scratch (e.g., in a later call to `value()`), we need to launder.

Why can't we reuse the original byte pointer?

Because the byte pointer was obtained before the object was created. The C++ object model says that pointer does not point to the new object, even though it points to the same memory address. Laundering obtains a new pointer with correct provenance.

---

## Design Decisions and Tradeoffs

### Why Power-of-2 Slab Sizes?

**Benefit**: Fast index-to-slab mapping using bit operations
**Cost**: Potentially wasted memory if ideal size is not power-of-2

The tradeoff favors performance. Division is slow (10-40 cycles on modern CPUs). Bit-shift is 1 cycle.

### Why Separate Alive Bitmap?

**Benefit**: Faster iteration and destruction, cache-friendly
**Cost**: Extra memory (1 bit per slot)

For 1024 slots: 128 bytes of bitmap vs. potentially scanning 1024 slots to find alive ones. The bitmap is dense and cache-friendly.

### Why size_type Has Extra Bit?

**Benefit**: All indices usable, no wasted index value
**Cost**: Slightly larger type (e.g., uint32_t instead of uint16_t for slots)

For 16-bit index: using uint17_t (actually uint32_t) for size wastes 15 bits per slot. But this is mitigated because slots don't store size_type persistently - they store it only in the next-pointer when free.

The alternative would be reserving one index value (e.g., 0xFFFF) as sentinel. This wastes an index that could store a value. With small index spaces, this is unacceptable.

### Why Factory Pattern for Slab?

**Benefit**: Encapsulates complex allocation logic, prevents misuse
**Cost**: Cannot use make_unique (custom allocator)

The flexible array member pattern requires custom allocation. Exposing this in a public constructor would be error-prone. The factory method ensures correct allocation every time.

### Why Non-Const for_each Delegates to Const?

**Benefit**: Eliminates code duplication, single source of truth
**Cost**: Requires const_cast (looks scary, but is safe)

The iteration logic is complex (nested loops, early exit, key reconstruction). Duplicating this between const and non-const overloads would be error-prone. By delegating, we ensure both versions behave identically.

---

## Common Pitfalls and Gotchas

### Pitfall 1: Forgetting to Update Free List Before Exception

**Wrong**:
```cpp
free_list_head_ = slot.next();  // Update too early!
T & value = slot.emplace(args...);  // MAY THROW
++size_;
```

If `emplace` throws, `free_list_head_` is corrupted (points to a slot that's not actually free).

**Right**:
```cpp
auto [ver, next] = slab->emplace(slot_idx, args...);  // Capture next before modifying
free_list_head_ = next;  // Only update after success
++size_;
```

### Pitfall 2: Using index_type for next_slab_base_index_

**Wrong**:
```cpp
naked_index_type next_slab_base_index_;  // Overflow when index space full!
```

When the last slab is allocated, `next_slab_base_index_` equals `2^IndexBits`, which does NOT fit in `index_type`.

**Right**:
```cpp
naked_size_type next_slab_base_index_;  // Has extra bit for overflow detection
```

### Pitfall 3: Not Using std::launder After Placement New

**Wrong**:
```cpp
T* ptr = reinterpret_cast<T*>(storage_.data());
new (ptr) T(args...);
return *ptr;  // Undefined behavior!
```

The pointer obtained before construction does not point to the newly constructed object.

**Right**:
```cpp
T* ptr = std::construct_at(reinterpret_cast<T*>(storage_.data()), args...);
return *ptr;  // OK, construct_at returns laundered pointer

// Or:
new (storage_.data()) T(args...);
return *std::launder(reinterpret_cast<T*>(storage_.data()));  // OK, laundered
```

### Pitfall 4: Forgetting Alignment in operator delete

**Wrong**:
```cpp
static void operator delete(void* ptr) {
    ::operator delete(ptr);  // Missing alignment!
}
```

Undefined behavior if the type has over-alignment.

**Right**:
```cpp
static void operator delete(void* ptr) {
    ::operator delete(ptr, std::align_val_t{alignof(Slab)});
}
```

### Pitfall 5: Assuming Iteration Order

Iteration order is unspecified. Do not rely on any particular order (e.g., insertion order, index order).

Current implementation iterates slabs in vector order, then slots in array order, but this is an implementation detail that may change.

---

## Testing Strategy

### Unit Tests (doctest)

Located in `src/wjh/slotmap/tests/*.cpp`:

- `SlotMap_ut.cpp`: Core operations (emplace, erase, use, contains)
- `SlotMap_lifecycle_ut.cpp`: Copy, move, swap, pop, reserve
- `SlotMap_iteration_ut.cpp`: for_each, clear, reset
- `SlotMap_edge_cases_ut.cpp`: Exception safety, bit-field extremes

Use `SUBCASE` for scenario variants:

```cpp
TEST_CASE("SlotMap emplace") {
    SlotMap<Key<int, 16, 16>> map;

    SUBCASE("returns valid key") {
        auto key = map.emplace(42);
        CHECK(not key.is_null());
    }

    SUBCASE("increments size") {
        auto before = map.size();
        map.emplace(42);
        CHECK(map.size() == before + 1);
    }
}
```

### Property-Based Tests (rapidcheck)

Use `rc::check()` for invariant verification:

```cpp
rc::check("insert-find roundtrip", [](std::vector<int> const & values) {
    SlotMap<Key<int, 16, 16>> map;
    std::vector<Key<int, 16, 16>> keys;

    for (auto v : values) {
        keys.push_back(map.emplace(v));
    }

    RC_ASSERT(map.size().value == values.size());

    for (size_t i = 0; i < values.size(); ++i) {
        int found = -1;
        map.use(keys[i], [&](int const & v) { found = v; });
        RC_ASSERT(found == values[i]);
    }
});
```

### Edge Case Testing

Critical edge cases to test:

1. **1-bit fields**: `Key<T, 1, 1>` (minimum possible configuration)
2. **Maximum capacity**: Fill entire index space, verify null key returned
3. **Version exhaustion**: Emplace/erase until slot is dead, verify recycling
4. **Single-slot slabs**: `SlotMap(size_type{1})` (edge case for iteration)
5. **Maximum slab size**: `SlotMap(end_of_free_list)` (single giant slab)
6. **128-bit keys**: Test on platforms with `__int128` support

### Exception Safety Testing

Use throwing types to verify strong guarantee:

```cpp
struct ThrowOnCopy {
    int value;
    static int throw_on_count;
    static int copy_count;

    ThrowOnCopy(int v) : value(v) {}
    ThrowOnCopy(ThrowOnCopy const & other) : value(other.value) {
        if (++copy_count == throw_on_count) {
            throw std::runtime_error("copy failed");
        }
    }
};

TEST_CASE("SlotMap copy constructor exception safety") {
    SlotMap<Key<ThrowOnCopy, 8, 8>> map;
    map.emplace(1);
    map.emplace(2);

    ThrowOnCopy::copy_count = 0;
    ThrowOnCopy::throw_on_count = 2;  // Throw on second copy

    try {
        SlotMap copy(map);  // Should throw
        FAIL("Expected exception");
    } catch (std::runtime_error const &) {
        // Good, exception thrown
    }

    // Original map should be unchanged
    CHECK(map.size() == 2);
}
```

---

## Adding New Features

### Guidelines for Contributors

1. **Maintain strong exception safety**
   - Don't modify state before operations that can throw
   - Use RAII for resource management
   - Prefer copy-and-swap for assignment operators

2. **Use strong types**
   - Don't use raw integers in interfaces
   - Prefer `index_type`, `version_type`, `size_type` over their naked counterparts
   - Use naked types only in inner loops where performance is critical

3. **Test edge cases**
   - 1-bit fields (minimum configuration)
   - Max capacity (index space exhaustion)
   - Version exhaustion (dead slots, recycling)
   - Empty containers, single-element containers
   - Exception safety (throwing constructors/destructors)

4. **Use property-based tests**
   - Verify invariants across random inputs
   - Test interleaved operations (emplace/erase in random order)
   - Use rapidcheck generators for complex scenarios

5. **Follow .hpp/.ipp pattern**
   - Declarations in `.hpp`
   - Implementations in `.ipp`
   - `.hpp` includes `.ipp` at the end

6. **constexpr where possible**
   - Key operations should be constexpr
   - Use `static_assert` for compile-time validation
   - Prefer constexpr functions over macros

---

## Performance Considerations

### Slab Size Selection

Default slab size heuristic:
- If entire index space fits in ~2MB: use single slab
- Otherwise: use 4096 slots per slab

Rationale:
- Single slab eliminates indirection (no slab lookup)
- Multiple slabs allow growing without reallocating everything
- 4096 slots balances memory overhead vs. lookup cost

For performance-critical applications, measure and tune `slots_per_slab` for your workload.

### Cache Effects

Slab-based design improves cache locality:
- Values in same slab are contiguous in memory
- Iteration over alive values is cache-friendly (bitmap is dense)
- Free list is cache-unfriendly (scattered slots), but this only affects emplace/erase

### Bit Operations

All index-to-slab and slot-within-slab calculations use bit operations (shift/mask) instead of division/modulo. This requires power-of-2 slab sizes but provides significant speedup.

Modern CPUs can execute bit-shifts in 1 cycle. Division can take 10-40 cycles.

### Inlining

Most member functions are defined in `.ipp` files, making them available for inlining. The compiler can inline across translation units in LTO builds.

Critical functions for inlining:
- `get_slot()`: Called on every use/emplace/erase
- `Slot::version()`: Called frequently for validation
- `is_alive()`: Called in tight loops during iteration

---

## Known Limitations and Future Work

### Current Limitations

1. **Not thread-safe**: No synchronization primitives (use external locking)
2. **No custom allocator support**: Always uses global `operator new`
3. **No bulk erase**: Must erase elements one at a time
4. **Finite lifetime**: Each slot has 2^VersionBits uses, then becomes dead

### Planned Features

#### resurrect()

Reset all versions to 0, invalidating all keys but allowing unlimited lifetime:

```cpp
void resurrect() {
    for (auto & slab_ptr : slabs_) {
        if (slab_ptr) {
            // Reset all versions to 0
            // Clear alive bitmap
            // Rebuild free list
        }
    }
}
```

**Tradeoff**: All existing keys become invalid. Users must regenerate keys.

**Use case**: Long-running servers where version exhaustion would eventually occur.

#### Thread-Safe Variant

Potential approaches:
- Per-slab locks (sharding)
- Lock-free free list (atomic compare-and-swap)
- Immutable persistent data structure (copy-on-write)

**Tradeoff**: Synchronization overhead vs. concurrent access.

#### PMR Allocator Support

Add `std::pmr::memory_resource*` parameter:

```cpp
SlotMap(size_type slots_per_slab, std::pmr::memory_resource* resource);
```

Use resource for slab allocation instead of global `operator new`.

**Tradeoff**: Additional member variable, virtual function calls for allocation.

#### erase_if(predicate)

Bulk removal based on predicate:

```cpp
template <typename Pred>
size_type erase_if(Pred && pred) {
    size_type removed{0};
    for_each([&](key_type key, mapped_type & value, Break &) {
        if (std::invoke(pred, value)) {
            erase(key);
            ++removed;
        }
    });
    return removed;
}
```

**Note**: Erasing during iteration is currently undefined behavior. Would require special handling.

---

## Debugging Tips

### Inspecting Keys

Keys are bit-packed. Use extraction methods in debugger:

```
(gdb) p key.index().value
(gdb) p key.version().value
(gdb) p key.user().value
```

Or print as hex to see bit layout:

```
(gdb) p/x key.to_underlying()
```

### Checking Alive Status

In debugger, check slab's alive bitmap:

```
(gdb) p slab->is_alive(index_type{5})
```

### Verifying Free List

Walk the free list manually:

```
(gdb) set $head = map.free_list_head_.value
(gdb) p $head
(gdb) p map.get_slot(index_type{$head}).next().value
# Repeat until end_of_free_list
```

### Memory Layout

Print memory addresses to verify layout:

```
(gdb) p slab
(gdb) p slab->slots()
(gdb) p slab->bitmap()
```

Verify: `slots()` == `slab + sizeof(Slab)`, `bitmap()` == `slots() + slots_per_slab * sizeof(Slot)`

---

## Platform-Specific Concerns

### 128-bit Keys

128-bit keys use `unsigned __int128`, which is a GCC/Clang extension. Not available on MSVC.

Conditional compilation:

```cpp
#ifdef __SIZEOF_INT128__
template <> struct storage_type<128> { using type = unsigned __int128; };
#endif
```

On platforms without `__int128`, attempting to use 128-bit keys will fail to compile.

### Alignment

Over-aligned types (e.g., `alignof(T) > alignof(std::max_align_t)`) require C++17's aligned `operator new`.

Older compilers may not support this. Check `__cpp_aligned_new >= 201606` for feature detection.

### Endianness

Bit packing is endian-neutral (uses bitwise operations, not byte manipulation). Keys can be serialized and deserialized across different-endian platforms, though values in the slot map cannot (T's layout is platform-specific).

---

## Appendix: ASCII Diagrams

### Complete Memory Layout Example

```
SlotMap (slots_per_slab = 4, 2 slabs allocated)

slabs_ vector:
┌─────┬─────┬─────┐
│  0  │  1  │ ... │  (slabs_[0], slabs_[1], ...)
└──┬──┴──┬──┴─────┘
   │     │
   │     └──> Slab 1 (indices 4-7)
   └──────> Slab 0 (indices 0-3)

Slab 0 memory:
┌──────────────────────────────┐
│ dead_count_: 0               │
│ slots_per_slab_: 4           │
├──────────────────────────────┤
│ Slot[0]: storage_, version_  │  (index 0, version 1)
│ Slot[1]: storage_, version_  │  (index 1, version 0)
│ Slot[2]: storage_, version_  │  (index 2, version 0)
│ Slot[3]: storage_, version_  │  (index 3, version 0)
├──────────────────────────────┤
│ Bitmap: [0000 0000]          │  (no slots alive)
└──────────────────────────────┘

Free List (assuming all free):
free_list_head_ = 0
  Slot[0].next = 1
  Slot[1].next = 2
  Slot[2].next = 3
  Slot[3].next = 4  (points to next slab)
  Slot[4].next = 5
  Slot[5].next = 6
  Slot[6].next = 7
  Slot[7].next = end_of_free_list (sentinel)
```

### Key Bit Layout (32-bit example)

```
Key<int, 20, 10, 2> (20 index bits, 10 version bits, 2 user bits)

Bit layout:
31 30│29 ... 20│19 ... 0
─────┼─────────┼─────────
User │ Version │  Index

Example key value: 0b11_0000000101_00000000000000001010

User:    0b11    (3)
Version: 0b0000000101 (5)
Index:   0b00000000000000001010 (10)

Masks:
index_mask   = 0x000FFFFF  (20 bits)
version_mask = 0x000003FF  (10 bits)
user_mask    = 0x00000003  (2 bits)

Shifts:
version_shift = 20
user_shift    = 30
```

---

## Conclusion

This guide covers the essential internals of `wjh::slotmap`. When adding features or fixing bugs:

1. Understand the object lifetime model (byte arrays, placement new, std::launder)
2. Maintain exception safety (don't modify state before operations that can throw)
3. Respect the strong type system (use index_type, version_type, size_type)
4. Test edge cases (1-bit fields, version exhaustion, capacity limits)
5. Measure performance (bit operations are fast, but cache effects matter)

The design prioritizes:
- **Correctness**: No undefined behavior, strong exception safety
- **Performance**: O(1) operations, cache-friendly iteration
- **Type safety**: Strong types prevent mixing indices and versions
- **Generality**: Works with any T, any bit configuration (32/64/128-bit keys)

When in doubt, consult the test suite for examples of correct usage and expected behavior.
