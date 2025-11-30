// ----------------------------------------------------------------------
// Copyright 2025 Jody Hagins
// Distributed under the MIT Software License
// See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT
// ----------------------------------------------------------------------
#ifndef WJH_SLOTMAP_0843744742F143B5826D3DA7D551B2EC
#define WJH_SLOTMAP_0843744742F143B5826D3DA7D551B2EC

#include "Slot.hpp"

#include <cassert>
#include <cstddef>
#include <cstring>
#include <memory>
#include <new>

namespace wjh::slotmap::detail {

/**
 * A slab containing a fixed-size array of slots plus metadata.
 *
 * Memory layout:
 *   - Slab metadata (dead_count, slots_per_slab)
 *   - Array of Slot objects
 *   - Alive bitmap (ceil(slots_per_slab / 8) bytes)
 *
 * @tparam T The value type stored in slots
 * @tparam IndexT Strong type for indices (e.g., Key::Index)
 * @tparam VersionT Strong type for versions (e.g., Key::Version)
 * @tparam SizeT The size type for counts
 */
template <typename T, typename IndexT, typename VersionT, typename SizeT>
class alignas(std::max(alignof(SizeT), alignof(Slot<T, SizeT, VersionT>))) Slab
{
public:
    using value_type = T;
    using index_type = IndexT;
    using version_type = VersionT;
    using size_type = SizeT;
    // Use size_type for Slot's next-link type so it can hold end_of_free_list
    using slot_type = Slot<T, size_type, VersionT>;

    static constexpr auto max_version = version_type(version_type::mask);

    [[nodiscard]]
    static constexpr std::size_t total_bytes_needed(size_type slots_per_slab);

    /**
     * Create a new slab with the given number of slots.
     *
     * All slots are initialized with version 0 and alive bits cleared.
     *
     * @param slots_per_slab Number of slots in this slab (must be > 0)
     * @return Unique pointer to the newly created slab
     * @throws std::bad_alloc if allocation fails
     */
    [[nodiscard]]
    static std::unique_ptr<Slab> create(size_type slots_per_slab);

    template <typename ValT>
    static std::unique_ptr<Slab> create(ValT slots_per_slab)
    requires requires { size_type(slots_per_slab); };

    /**
     * Create a deep copy of this slab.
     *
     * Copies all slot metadata (versions, next links) and alive values.
     * The new slab has identical structure to the original.
     *
     * @return Unique pointer to the cloned slab
     * @throws std::bad_alloc if allocation fails
     * @throws Any exception from T's copy constructor
     *
     * @note Only available if T is copy constructible
     */
    [[nodiscard]]
    std::unique_ptr<Slab> clone() const
    requires std::is_copy_constructible_v<T>;

    // Non-copyable, non-movable
    Slab(Slab const &) = delete;
    Slab & operator = (Slab const &) = delete;
    Slab(Slab &&) = delete;
    Slab & operator = (Slab &&) = delete;

    ~Slab();

    static void operator delete (void * ptr);
    static void operator delete[] (void *) = delete;

    // ========================================================================
    // Slot lifecycle management
    // ========================================================================

    struct EmplaceResult
    {
        version_type version;
        size_type next;
    };

    /**
     * Emplace a value into a slot.
     *
     * @param index Slot index within this slab
     * @param args Arguments to forward to T's constructor
     * @return The version for this insertion (to be used in the key)
     *
     * @pre Slot must not be alive (is_alive(index) == false)
     * @post Slot is alive (is_alive(index) == true)
     */
    template <typename... Args>
    EmplaceResult emplace(index_type index, Args &&... args);

    /**
     * Destroy the value in a slot.
     *
     * Destroys the value, clears the alive bit, and increments the version.
     * If the version was already at max (all bits 1), the slot becomes dead
     * and is not suitable for reuse.
     *
     * @param index Slot index within this slab
     * @return true if slot can be reused (added to free list),
     *         false if slot is dead (version exhausted)
     *
     * @pre Slot must be alive (is_alive(index) == true)
     * @post Slot is not alive (is_alive(index) == false)
     */
    bool destroy(index_type index);

    /**
     * Check if a slot is alive (has a constructed value).
     */
    [[nodiscard]]
    bool is_alive(index_type index) const noexcept;

    // ========================================================================
    // Slot access (for free-list management by SlotMap)
    // ========================================================================

    [[nodiscard]]
    slot_type & slot(index_type index) noexcept;

    [[nodiscard]]
    slot_type const & slot(index_type index) const noexcept;

    [[nodiscard]]
    size_type slots_per_slab() const noexcept;

    // ========================================================================
    // Dead count tracking
    // ========================================================================

    [[nodiscard]]
    size_type dead_count() const noexcept;

    [[nodiscard]]
    bool can_be_recycled() const noexcept;

    /**
     * Recycle the slab so it can be reused.
     *
     * @param first_index  The true index of the first slot in this slab.
     *
     * @param last_next  The next value for the last slot in this slab.
     *                   This is size_type to allow storing end_of_free_list.
     *
     * @pre can_be_recycled()
     */
    void recycle(index_type first_index, size_type last_next);

private:
    using naked_index_type = typename index_type::value_type;
    using naked_size_type = typename size_type::value_type;

    explicit Slab(size_type slots_per_slab) noexcept;

    /**
     * Calculate the number of bytes needed for the alive bitmap.
     */
    static constexpr std::size_t bitmap_size(size_type slots_per_slab) noexcept;

    slot_type * slots() noexcept;
    slot_type const * slots() const noexcept;

    std::byte * bitmap() noexcept;
    std::byte const * bitmap() const noexcept;

    void set_alive(index_type index, bool alive) noexcept;
    [[maybe_unused]]
    bool are_all_dead() const;

    naked_size_type dead_count_;
    naked_size_type slots_per_slab_;
};

} // namespace wjh::slotmap::detail

#include "Slab.ipp"

#endif // WJH_SLOTMAP_0843744742F143B5826D3DA7D551B2EC
