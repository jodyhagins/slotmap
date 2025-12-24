# Design Document: wjh::slotmap

This document explains the design decisions behind the slot map implementation.

## Core Design Principles

### 1. Safety Over Convenience

The library prioritizes safety over convenience:

- **No raw pointers or references** to elements are returned. All access goes through callbacks.
- **No iterators** are provided, preventing dangling iterator bugs after mutations.
- **Type-safe keys** with phantom types prevent mixing keys from different maps.
- **Fixed lifetime** ensures old keys never accidentally access new data.

### 2. Zero Overhead Abstractions

All abstractions compile away:

- Keys are just bit-packed integers (16, 32, 64, or 128 bits)
- Strong types (`index_type`, `version_type`) compile to raw operations
- All key operations are `constexpr`
- No virtual functions or runtime type information

### 3. Configurable Bit Layout

Users control the trade-off between:
- **IndexBits**: Maximum simultaneous elements (2^IndexBits)
- **VersionBits**: Slot reuse count before permanent death (2^VersionBits per slot)
- **UserBits**: User-controlled metadata embedded in the key itself

## Design Decisions

### Why No Iterators?

I've interviewed C++ programmer job applicants for almost 40 years.
Whenever someone brings up anything related to the standard library containers,
I usually ask something about iterator invalidation.
I always get wrong answers.
Always.
Even about `std::vector` which is one of the most widely used container.

The iterator invalidation rules are simple for SlotMap.
There are no iterators, so there are no stinkin' rules.

IMO, iterators are even more dangerous than raw pointers.
At least people have been crying about raw pointers for years, and have
developed many solutions to keep them out of your hands.

Not so with iterators.
For some reason, the C++ community just accepts this danger like teenagers
at summer camp in a B-rated horror movie.

```cpp
// Dangerous with iterators:
auto it = map.find(key);
map.erase(other_key);  // May invalidate 'it'!
*it;                    // Undefined behavior
```

The callback pattern forces explicit lifetime scoping:

```cpp
// Safe with callbacks:
map.use(key, [](auto & value) {
    // Value is guaranteed valid within callback
});
```

No, you can't get an iterator to pass to the generic algorithms.
This is a special purpose data structure, and it doesn't play nicely with all the
container and range algorithms.
We could add a range adapter or something like that if absolutely necessary,
but until the standard library takes iterator access seriously, we are just swimming
against the current anyway.


### Why Fixed Lifetime (No Version Wrapping)?

Alternative designs allow versions to wrap around.
Some have really effective strategies to minimize the chance of wrapping.
Our strategy is simple.
We just don't allow wrapping.

Consider this case, if we were to allow version wrapping.


```cpp
// If versions could wrap:
auto key = map.emplace(x);     // slot 5, version 255
map.erase(key);
// ... many operations later ...
auto key2 = map.emplace(y);    // slot 5, version 255 (wrapped!)
map.use(key, ...);             // OOPS: accesses wrong object!
```

In our design, once a slot exhausts all versions, it's permanently dead.
Old keys can never accidentally match new data.

**Trade-off**: Limited total insertions over the container's lifetime.
- Calculate: `2^IndexBits * 2^VersionBits - 1` total insertions
- Example: 20 index + 12 version = ~4 billion total insertions

### Why Callback-Based Access?

Three problems with returning pointers/references:

1. **Dangling on erase**: Reference becomes invalid if element is erased
2. **Dangling on insertion**: Some containers invalidate references on insert
3. **Thread-safety**: Hard to protect concurrent access with raw pointers

Callbacks solve all three:
- Reference is scoped to callback lifetime
- Container state is consistent during callback
- Future: Callbacks enable per-element locking

The `Options` struct provides escape hatches within callbacks:
```cpp
map.use(key, [](auto & val, Options & opts) {
    if (should_delete(val)) {
        opts.erase = true;  // Erase after callback returns
    }
});
```

Yes, you can still get access to the actual pointer, so you can grab it if you must, but it will look funny, and hopefully draw at least a cold stare during code review.

### Why User Bits in Keys?

User bits allow embedding metadata directly in the key:

```cpp
// Matching engine example: same order, different strategies
Key<Order, 20_ib, 10_vb, 2_ub> base_key = orders.emplace(order);
auto aggressive_key = base_key.with_user({0b01});
auto passive_key = base_key.with_user({0b10});

// Both keys access the same order, but carry different context
process_aggressive(aggressive_key);
process_passive(passive_key);

// Equality semantics:
assert(aggressive_key != passive_key);                      // Different keys
assert(aggressive_key.identifies_same_object(passive_key)); // Same slot
```

User bits are ignored by the container for lookup - only index and version matter.

### Why Slab-Based Allocation?

Instead of individual allocations per element:

**Benefits:**
- Predictable memory layout (cache-friendly)
- Bulk allocation reduces allocator pressure
- Bitmap enables fast iteration over alive elements
- Dead slabs can be recycled (memory reuse optimization)

**Slab Recycling:**
When all slots in a slab become dead, the slab's memory is recycled:
- The physical memory is reused for a new slab at a different index range
- This is purely an optimization - it does NOT extend capacity or provide more slots
- Total insertions are still bounded by `2^IndexBits * 2^VersionBits - 1`

### Why the Alive Bit Optimization?

When `version_bits` doesn't fill its storage type (e.g., 6-bit version in an 8-bit byte), the MSB can serve as an "alive bit":

```
Version storage (8 bits): [alive | 7-bit version]
```

**Benefit:** Single memory fetch checks both version AND alive status:
```cpp
if (slot.version_with_alive_bit() == expected_version_with_alive) {
    // Fast path: version matches AND slot is alive
}
```

When `version_bits` fills the type completely, the slab's bitmap is the authority.

## Memory Layout

### Key Layout

```
Key<T, IndexBits(20), VersionBits(10), UserBits(2)>:
  [user (2) | version (10) | index (20)] = 32 bits total
```

Bit layout from MSB to LSB: `[user][version][index]`

### Slot Layout

```
Slot:
  storage_[max(sizeof(T), sizeof(index_type))]: Either value or free-list link
  version_bytes_[sizeof(version_type)]: Version counter (+ alive bit if available)
```

### Slab Layout

```
Slab header:
  dead_count_: Number of permanently dead slots
  slots_per_slab_: Slot count for this slab

Slot array:
  slots_per_slab_ contiguous Slot objects

Alive bitmap:
  ceil(slots_per_slab_ / 8) bytes, one bit per slot
```

## Traits System

The `SlotMap<T, auto... vs>` template accepts configuration in any order:

```cpp
// All equivalent:
SlotMap<int, IndexBits(20), VersionBits(12)>
SlotMap<int, VersionBits(12), IndexBits(20)>
SlotMap<Key<int, 20_ib, 12_vb>>
```

The variadic template unpacks and sorts parameters using the `Traits` template, enabling this flexible syntax.

### Default User Bits

When keys are created via `emplace()` or `try_emplace()`, the user bits field defaults to 0. This can be customized via the `DefaultUserBits` configuration:

```cpp
// Keys from emplace will have user bits set to 0x55
using MySlotMap = SlotMap<int, IndexBits(16), VersionBits(8), UserBits(8),
                          DefaultUserBits(0x55)>;

MySlotMap map;
auto key = map.emplace(42);
assert(key.user().value == 0x55);

// Can still override per-key:
auto custom_key = key.with_user(MySlotMap::user_type{0xAA});
```

The default value is masked to fit within the configured user bits count. Keys provided to `for_each()` callbacks also use the default user bits value.

### Storage Policies

Two storage strategies selected at compile time:

**Single-Slab (when SlotsPerSlab >= 2^IndexBits):**
- All slots fit in one slab
- Direct index-to-slot mapping
- Minimal indirection

**Multi-Slab (default):**
- Index decomposed into slab index + slot index
- Uses bit manipulation (power-of-2 slots per slab)
- Supports slab recycling

## Thread Safety

The library provides **no thread safety guarantees**, consistent with standard library containers. External synchronization is required for concurrent access.

Future versions may add thread-safe variants or per-element locking via callbacks.

## Exception Safety

| Operation | Guarantee |
|-----------|-----------|
| emplace/try_emplace | Strong (element not inserted if constructor throws) |
| erase | No-throw (assumes destructor doesn't throw) |
| pop | Strong (element not erased if move throws) |
| use/for_each | Basic (container unchanged if callback throws) |
| clear/reset | No-throw |
| Copy constructor | Strong |

## Limitations

1. **Fixed lifetime**: Total insertions bounded by `2^(IndexBits+VersionBits) - 1`
2. **No stable pointers**: Elements cannot be referenced outside callbacks
3. **No iterators**: Must use callback-based iteration
4. **Single-threaded**: No built-in thread safety
5. **128-bit keys**: Require `__uint128_t` (GCC/Clang only)
