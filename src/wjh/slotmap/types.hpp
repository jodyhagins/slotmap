// ----------------------------------------------------------------------
// Copyright 2025 Jody Hagins
// Distributed under the MIT Software License
// See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT
// ----------------------------------------------------------------------
#ifndef WJH_SLOTMAP_153C55E8FEB34AA9AB579FCE39E431F4
#define WJH_SLOTMAP_153C55E8FEB34AA9AB579FCE39E431F4

#include <cstddef>
#include <cstdint>

namespace wjh::slotmap {

/**
 * The number of bits to use for the index part of the key.
 *
 * A SlotMap is defined with a specific IndexBits value. The maximum number of
 * objects that a SlotMap can hold at any given time is 2^IndexBits.
 */
enum class IndexBits : unsigned
{
};

/**
 * The number of bits to use for the version part of the key.
 *
 * An individual slot in a SlotMap can be reused 2^VersionBits times. After
 * that, the slot will not be reused again. Thus, the upper limit on the total
 * number of objects a SlotMap can create in its lifetime is 2^IndexBits *
 * 2^VersionBits - 1. The '-1' is because the very first slot starts at version
 * 1 instead of version 0, because a null-key has all bits as zero.
 */
enum class VersionBits : unsigned
{
};

/**
 * The number of bits to all the user to control.
 *
 * These bits will be masked out when the SlotMap handles a key, so the user can
 * put anything they want in there.
 */
enum class UserBits : unsigned
{
};

/**
 * Options for use() and for_each() operations.
 *
 * @var stop  Set true to stop iteration after this callback (for_each only).
 * @var erase Set true to erase the element after the callback returns.
 *            Ignored on const overloads (asserts in debug mode).
 */
struct Options
{
    bool stop = false;
    bool erase = false;
};

/**
 * Statistics about a SlotMap's current state.
 *
 * Terminology:
 * - Slot: A physical storage location identified by an index. Each slot can
 *     hold one object at a time, but many objects over its lifetime.
 * - Object: A value stored in a slot. A slot creates a new "object" each time
 *     it is reused (emplace after erase). The version field tracks this.
 *
 * All size values use std::size_t since IndexBits is limited to 63, ensuring
 * size_type (IndexBits + 1 bits) always fits in 64 bits.
 *
 * Complexity: O(num_slabs) to compute via statistics().
 */
struct Statistics
{
    static_assert(sizeof(std::size_t) >= sizeof(std::uint64_t));

    /// Type used for max_objects and objects_remaining fields.
    /// Uses __uint128_t when available (GCC/Clang on 64-bit), otherwise
    /// std::uint64_t. Saturates to max value on overflow.
#ifdef __SIZEOF_INT128__
    using max_objects_type = __uint128_t;
#else
    using max_objects_type = std::uint64_t;
#endif

    // ========================================================================
    // Configuration (immutable after construction of the SlotMap)
    // ========================================================================

    /// Configured slots per slab (power of 2)
    std::size_t slots_per_slab;

    /// Max simultaneous slots (2^IndexBits)
    std::size_t max_slots;

    /// Max objects ever creatable (2^IndexBits * 2^VersionBits - 1)
    /// The -1 is because slot 0 starts at version 1 to avoid the null key.
    ///
    /// @note Uses 128-bit type when available (GCC/Clang), otherwise 64-bit.
    ///       Saturates to max representable value on overflow.
    max_objects_type max_objects;

    // ========================================================================
    // Slot accounting
    // ========================================================================

    /// Number of slots currently holding alive objects
    std::size_t active_slots;

    // Number of slots on the free list, available for immediate use
    std::size_t free_slots;

    /// Number of exhausted slots (all versions have been used)
    std::size_t dead_slots;

    /// Total number of allocated slots (active + free + dead)
    std::size_t allocated_slots;

    /// Number of slots that have not yet been allocated
    std::size_t unallocated_slots;

    // ========================================================================
    // Capacity metrics
    // ========================================================================

    /// Slots usable without new slab (= free_slots)
    std::size_t available_slots;

    /// Max additional active possible (max - dead)
    std::size_t remaining_slots;

    // ========================================================================
    // Object lifetime metrics
    // ========================================================================

    /// Total objects created over lifetime
    std::size_t objects_created;

    /// Objects still creatable (max - created)
    max_objects_type objects_remaining;

    // ========================================================================
    // Slab metrics
    // ========================================================================

    /// Active (non-null) slabs
    std::size_t slab_count;

    /// Slab vector size (includes nulls)
    std::size_t slab_vector_size;

    // ========================================================================
    // Memory metrics
    // ========================================================================

    /// Memory for all slabs
    std::size_t slab_memory_bytes;

    /// Memory for slab pointer vector
    std::size_t vector_memory_bytes;

    /// Total memory usage
    std::size_t total_memory_bytes;

    // ========================================================================
    // Derived metrics
    // ========================================================================

    /// Fraction in use: active / remaining (0 if remaining == 0)
    double slot_utilization;

    /// Fraction dead: dead / allocated (0 if allocated == 0)
    double dead_slot_ratio;

    /// Fraction exhausted: created / max (0 if max == 0)
    double lifetime_exhaustion;

    /// Average bytes: memory / active (0 if active == 0)
    double bytes_per_object;
};


/**
 * The number of slots to allocate per slab.
 *
 * There are several predefined values, but you can provide an explicit value,
 * e.g., SlotsPerSlab(1 * 1024 * 1024).
 *
 * The SlotMap will allocate memory in chunks, called slabs. Each slab contains
 * some metadata about the slab, 2^IndexBits slots, and a bitmask
 * (2^IndexBits)/8 bytes long.
 *
 * A slab is an internal implementation detail, but it could have a measurable
 * impact on performance, especially for smaller IndexBits values.
 *
 * For consideration, the size of each slot can be imagined as the size of
 * `union { size_type; mapped_type; }` plus `sizeof(version_type)`.
 *
 * @note  When SlotsPerSlab >= 2^IndexBits (the maximum possible slots), the
 * implementation can make optimization trade offs, knowing that there will ever
 * only be at most one slot. SlotsPerSlab::All explicitly requests this
 * optimization.
 */
enum class SlotsPerSlab : std::size_t
{
    /**
     * The user provides the slots per slab to the SlotMap constructor. The
     * default constructor uses SlotsPerSlab::Default for the number of slabs.
     */
    Dynamic = 0,

    /**
     * The default is used if no SlotsPerSlab is provided. It is also the value
     * used in the SlotMap default constructor when Dynamic is specified.
     */
    Default = 4096,

    /**
     * Put all slots into a single slab, which is allocated when the SlotMap is
     * default constructed.
     *
     * @note  Any value larger than 2^IndexBits will put all slots into the same
     * slab, this is just a convenient way of specifying it. Imagine a family of
     * SlotMap types.
     *
     * @code
     * template <KeyC KeyT>
     * using SingleSlotMap = wjh::slotmap::SlotMap<
     *     wjh::slotmap::Traits<
     *         KeyT,
     *         SlotsPerSlab::All,
     *         UseAliveBitForLookup::Yes>>;
     * @endcode
     */
    All = std::size_t(-1),
};

/**
 * Controls whether use() reads the alive-bit from Slot or the bitmap.
 *
 * true  (default): Faster lookups, slightly slower insert/erase
 * false: Slower lookups, slightly faster insert/erase
 *
 * @note  If Key::version_bits is not a power of two, then there are no extra
 * bits in the version to keep track of an alive bit. In such cases,
 * UseAliveBitForLookup is ignored, because there is no alive bit ti use.
 */
enum class UseAliveBitForLookup : bool
{
    No = false,
    Yes = true,
};

/**
 * Default value for the user bits field in keys returned by
 * emplace/try_emplace.
 *
 * When a SlotMap creates a new key, the user bits field will be set to this
 * value. Users can change individual keys after creation with key.with_user().
 *
 * Example usage:
 * @code
 * using MySlotMap = SlotMap<int, IndexBits(16), VersionBits(8), UserBits(8),
 *                           DefaultUserBits(0xFF)>;
 * MySlotMap map;
 * auto key = map.emplace(42);  // key.user().value == 0xFF
 * @endcode
 */
enum class DefaultUserBits : std::size_t
{
};

namespace literals {

constexpr IndexBits
operator ""_ib (unsigned long long value) noexcept
{
    return IndexBits(static_cast<unsigned>(value));
}

constexpr VersionBits
operator ""_vb (unsigned long long value) noexcept
{
    return VersionBits(static_cast<unsigned>(value));
}

constexpr UserBits
operator ""_ub (unsigned long long value) noexcept
{
    return UserBits(static_cast<unsigned>(value));
}

} // namespace literals

} // namespace wjh::slotmap

namespace wjh::slotmap_literals {
using namespace slotmap::literals;
}

#endif // WJH_SLOTMAP_153C55E8FEB34AA9AB579FCE39E431F4
