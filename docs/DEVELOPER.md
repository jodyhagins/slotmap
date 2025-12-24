# wjh::slotmap Developer Guide

Internal architecture and implementation details for developers working on this library. For usage, see [API.md](API.md).

**Prerequisites:** Advanced C++ including templates, memory layout, placement new, `std::launder`, and the C++ object model.

---

## Architecture

```
SlotMap<KeyT>
    └── std::vector<std::unique_ptr<Slab>>
            └── Slab
                ├── Slot array (contiguous)
                ├── Alive bitmap
                └── Metadata (dead_count, slots_per_slab)
                        └── Slot
                            ├── storage_: byte array (T or size_type)
                            └── version_bytes_: byte array
```

Slabs are indexed via bit-shifting; slots within slabs via masking. This provides O(1) access with cache-friendly contiguous storage.

---

## File Structure

```
src/wjh/slotmap/
├── SlotMap.hpp/ipp   # SlotMap class
├── Key.hpp/ipp       # Key template
├── Traits.hpp        # Configuration traits
├── types.hpp         # IndexBits, VersionBits, Options, Statistics
├── detail.hpp        # TypeBase, storage_type, hash
└── detail/
    ├── Slot.hpp/ipp  # Slot class
    ├── Slab.hpp/ipp  # Slab class
    └── SlotMap.hpp   # Helper templates for SlotMap
```

**Pattern:** `.hpp` = declarations, `.ipp` = template implementations. Each `.hpp` includes its `.ipp` at the end. Never include `.ipp` directly.

---

## Key Implementation

### Bit Packing

Keys are packed into 16/32/64/128-bit unsigned integers: `[user][version][index]` from MSB to LSB.

`storage_type<TotalBits>` in `detail.hpp` selects the underlying type. Invalid totals (e.g., 48 bits) fail to compile.

### Strong Types (TypeBase)

All fields use `detail::TypeBase<N, Derived>`:

```cpp
template <unsigned N, typename DerivedT>
struct TypeBase {
    static constexpr unsigned num_bits = N;
    using value_type = type_with_at_least_t<num_bits>;
    static constexpr value_type mask = /* N bits set */;
    value_type value;
    // comparison, increment operators
};
```

Key types: `Index`, `Version`, `User`, `Size` all derive from TypeBase with appropriate bit counts.

### size_type Has IndexBits + 1 Bits

**Critical:** `size_type` has one extra bit to hold the free list sentinel (`end_of_free_list = 2^IndexBits`), which exceeds `index_type::mask`. This allows all indices (0 to 2^IndexBits - 1) to store values—no index wasted as sentinel.

Implications:
- Slot's next-pointer uses `size_type`
- `next_slab_base_index_` uses `naked_size_type` to detect overflow

### KeyBase and Triviality

`Key<T, ...>` inherits from `KeyBase<ValueType, T>` with zero-initialized bits.

`TrivialKey<T, ...>` (alias for `Key<Trivial<T>, ...>`) uses a specialization with `= default` constructor (uninitialized bits). The default constructor is private to prevent accidental use while preserving implicit lifetime type status.

### Bit Manipulation

Safe shift helpers handle edge cases where `UserBits == 0` (shifting by type width is UB):

```cpp
static constexpr value_type safe_shift_left(value_type val, unsigned shift) noexcept {
    return (shift >= num_bits) ? value_type{0} : val << shift;
}
```

---

## Traits

```cpp
template <KeyC KeyT, SlotsPerSlab nslots, UseAliveBitForLookup alive_bit,
          DefaultUserBits default_user = DefaultUserBits{0}>
struct Traits;
```

**Note:** `nslots` and `alive_bit` have no defaults in Traits itself. Defaults come through the `SlotMap` alias template's helper machinery.

### Single-Slab Optimization

When `SlotsPerSlab::All` or any value >= max slots:
- No slab vector (single pointer)
- Direct slot access without slab lookup
- Detected at compile time via `if constexpr`

---

## Slot Implementation

### Memory Layout

```cpp
template <typename T, typename SizeT, typename VersionT>
class Slot {
    alignas(max(alignof(SizeT), alignof(T)))
        std::array<std::byte, max(sizeof(SizeT), sizeof(T))> storage_;
    std::array<std::byte, sizeof(VersionT)> version_bytes_;
};
```

Storage holds EITHER a `T` (when alive) OR a `size_type` next-pointer (when free). We use byte arrays with placement new/`std::launder`—not unions—to comply with the C++ object model.

### Alive Bit in Version (Debug Mode)

When version bits < storage bits, the high bit of `version_bytes_` tracks alive/free state for debug assertions:

```cpp
static constexpr naked_version_type alive_bit =
    naked_version_type(1) << (version_digits - 1);
```

This is controlled by `WJH_SLOTMAP_DEBUG_MODE` (auto-defined for Debug builds). When enabled:
- `next()`, `set_next()` assert slot is free
- `emplace()` asserts slot is free
- `destroy()`, `value()` assert slot is alive

In Release, these compile away to nothing.

### std::launder Requirement

After placement new, the original byte pointer doesn't point to the new object per the C++ object model. Use `std::launder` or `std::construct_at` (which returns a laundered pointer):

```cpp
// Wrong: UB
T* ptr = reinterpret_cast<T*>(storage_.data());
new (ptr) T(args...);
return *ptr;

// Right
T* ptr = std::construct_at(reinterpret_cast<T*>(storage_.data()), args...);
return *ptr;
// Or: return *std::launder(reinterpret_cast<T*>(storage_.data()));
```

---

## Slab Implementation

### Flexible Array Pattern

Slab uses the "struct hack"—the slot array and bitmap follow the header in a single allocation:

```
┌────────────────────────────────────────────┐
│ Slab header (dead_count_, slots_per_slab_) │
├────────────────────────────────────────────┤
│ Slot[0] │ Slot[1] │ ... │ Slot[N-1]        │
├────────────────────────────────────────────┤
│ Alive bitmap (ceil(N/8) bytes)             │
└────────────────────────────────────────────┘
```

Access via pointer arithmetic: `slots() = this + 1`, `bitmap() = slots() + slots_per_slab_`.

### Factory Pattern

```cpp
static std::unique_ptr<Slab> create(size_type slots_per_slab) {
    void* raw = ::operator new(total_bytes_needed(slots_per_slab),
                               std::align_val_t{alignof(Slab)});
    auto* slab = ::new (raw) Slab(slots_per_slab);
    // Placement-new each slot, zero the bitmap
    return std::unique_ptr<Slab>(slab);
}
```

Custom `operator delete` passes alignment to `::operator delete`.

### EmplaceResult

```cpp
struct EmplaceResult {
    version_type version;
    size_type next;  // Captured BEFORE emplace overwrites it
};
```

The next-pointer must be captured before emplacing because placement new overwrites the storage.

### Dead Count and Recycling

When a slot at max version is destroyed:
1. `destroy()` returns `false` (slot dead, not added to free list)
2. `dead_count_` increments
3. When all slots dead, slab can be recycled to a new index range with versions reset to 0

---

## SlotMap Implementation

### Key Members

```cpp
std::vector<std::unique_ptr<slab_type>> slabs_;
size_type free_list_head_ = end_of_free_list;  // Sentinel = 2^IndexBits
naked_size_type size_ = 0;
naked_size_type slots_per_slab_;  // Must be power of 2
unsigned log2_slots_per_slab_;    // Cached for bit operations
naked_size_type next_slab_base_index_ = 0;  // Uses size_type width!
```

`slabs_` may contain `nullptr` entries from recycled slabs.

### Index Mapping

```cpp
auto slab_idx = idx >> log2_slots_per_slab_;
auto slot_idx = idx & (slots_per_slab_ - 1);
```

Power-of-2 slab sizes enable bit operations instead of division.

### Null Key Avoidance

Slot 0 of the first slab starts at version 1, ensuring `emplace()` never returns key with index=0, version=0 (the null key).

### Free List

Singly-linked through free slots: `free_list_head_ → slot.next → ... → end_of_free_list`

**Allocation:** Pop from head, capture next before emplacing, update head only after success.

**Deallocation:** Push to head (if not dead), or increment dead_count and check for recyclability.

---

## Version Semantics

| State | Version | Action | Result |
|-------|---------|--------|--------|
| FREE | 0 | emplace() | Key gets v=0, slot ALIVE |
| ALIVE | 0 | erase() | Version → 1, slot FREE |
| FREE | 1 | emplace() | Key gets v=1, slot ALIVE |
| ... | ... | ... | ... |
| FREE | max | emplace() | Key gets v=max, slot ALIVE |
| ALIVE | max | erase() | Slot DEAD (not added to free list) |

With N version bits: slot used 2^N times, then dead.

**Key validity:** Index exists AND slot alive (bitmap) AND version matches.

---

## Exception Safety

### emplace()/try_emplace()

Strong guarantee via ordering:
1. Check/allocate capacity (may throw)
2. Capture `next` from slot
3. Construct T (may throw) — if fails, slot unchanged
4. Update `free_list_head_` and `size_` (only after success)

### Copy Constructor

Strong guarantee via RAII:
```cpp
slabs_.reserve(other.slabs_.size());
for (auto const & slab_ptr : other.slabs_) {
    slabs_.push_back(slab_ptr ? slab_ptr->clone() : nullptr);
}
// If clone() throws, already-cloned slabs destroyed by unique_ptr
```

### Copy Assignment

Copy-and-swap idiom:
```cpp
SlotMap copy(other);  // May throw
swap(copy);           // noexcept
```

---

## Callback Dispatch

`use()` and `for_each()` use concepts to constrain callbacks:

```cpp
template <typename F, typename K, typename V>
concept UseCallbackC =
    std::invocable<F, K, V &, Options &> ||
    std::invocable<F, K, V &> ||
    std::invocable<F, V &, Options &> ||
    std::invocable<F, V &>;
```

`invoke_use` in detail dispatches to the matching overload. Return type determines result:
- `void` → `bool`
- Other `R` → `std::optional<R>`

`for_each` validates return type is `void` or `bool` via `static_assert`.

---

## Design Decisions

| Decision | Benefit | Cost |
|----------|---------|------|
| Power-of-2 slabs | Bit-shift instead of division (1 vs 10-40 cycles) | Memory waste if ideal size isn't power-of-2 |
| Separate bitmap | Cache-friendly iteration, fast alive checks | 1 bit per slot |
| size_type extra bit | All indices usable, no wasted sentinel index | Slightly larger type |
| Factory for Slab | Encapsulates complex allocation | Can't use make_unique |
| Byte arrays + launder | Compliant with C++ object model | More verbose than union |

---

## Common Pitfalls

**1. Updating free list before exception-safe point:**
```cpp
// WRONG
free_list_head_ = slot.next();
slot.emplace(args...);  // If throws, free list corrupted

// RIGHT
auto [ver, next] = slab->emplace(slot_idx, args...);
free_list_head_ = next;  // Only after success
```

**2. Using index_type for next_slab_base_index_:**
```cpp
// WRONG: Overflows when index space full
naked_index_type next_slab_base_index_;

// RIGHT: Has extra bit
naked_size_type next_slab_base_index_;
```

**3. Not laundering after placement new** — see Slot section above.

**4. Forgetting alignment in operator delete:**
```cpp
// WRONG
::operator delete(ptr);

// RIGHT
::operator delete(ptr, std::align_val_t{alignof(Slab)});
```

**5. Assuming iteration order** — unspecified and may change.

---

## Testing Strategy

### Property-Based Tests (rapidcheck)

```cpp
rc::check("insert-find roundtrip", [](std::vector<int> const & values) {
    SlotMap<...> map;
    std::vector<Key> keys;
    for (auto v : values) keys.push_back(map.emplace(v));
    RC_ASSERT(map.size().value == values.size());
    for (size_t i = 0; i < values.size(); ++i) {
        int found = -1;
        map.use(keys[i], [&](int v) { found = v; });
        RC_ASSERT(found == values[i]);
    }
});
```

### Critical Edge Cases

- **1-bit fields:** `Key<T, IndexBits(1), VersionBits(1)>`
- **Max capacity:** Fill index space, verify null/exception
- **Version exhaustion:** Cycle until dead, verify recycling
- **Single-slot slabs:** `SlotMap(size_type{1})`
- **128-bit keys:** Platforms with `__int128`

### Exception Safety Testing

```cpp
struct ThrowOnCopy {
    static int throw_after;
    ThrowOnCopy(ThrowOnCopy const &) { if (--throw_after == 0) throw ...; }
};

// Verify: if copy throws, original unchanged
```

---

## Debugging Tips

### Inspecting Keys

```
(gdb) p key.index().value
(gdb) p key.version().value
(gdb) p/x key.to_underlying()
```

### Verifying Free List

```
(gdb) set $head = map.free_list_head_.value
(gdb) p $head
(gdb) p map.get_slot(index_type{$head}).next().value
# Repeat until end_of_free_list
```

### Memory Layout

```
(gdb) p slab
(gdb) p slab->slots()      # Should be slab + sizeof(Slab)
(gdb) p slab->bitmap()     # Should be slots() + N * sizeof(Slot)
```

---

## Platform Notes

### 128-bit Keys

`unsigned __int128` is GCC/Clang only. Guarded by `#ifdef __SIZEOF_INT128__`.

### Alignment

Over-aligned types need C++17 aligned new. Check `__cpp_aligned_new >= 201606`.

### Endianness

Bit packing is endian-neutral (bitwise ops). Keys serialize portably; T does not.

---

## Future Work

- **resurrect():** Reset all versions, invalidating all keys but extending lifetime
- **Thread-safe variant:** Per-slab locks or lock-free free list
- **PMR allocator support**
- **erase_if(predicate):** Bulk removal (requires safe during-iteration erase)

---

## Summary

When modifying this code:

1. **Exception safety:** Don't update state before throwable operations
2. **Strong types:** Use `index_type`, `version_type`, `size_type`
3. **Object lifetime:** Respect placement new / launder requirements
4. **Test edge cases:** 1-bit fields, exhaustion, recycling
5. **Measure performance:** Bit ops fast, cache effects matter

The design prioritizes correctness (no UB, strong exception safety), performance (O(1) ops, cache-friendly), and type safety (strong types prevent mixing).
