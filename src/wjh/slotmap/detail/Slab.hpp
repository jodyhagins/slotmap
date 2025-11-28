// ----------------------------------------------------------------------
// Copyright 2025 Jody Hagins
// Distributed under the MIT Software License
// See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT
// ----------------------------------------------------------------------
#ifndef WJH_SLOTMAP_0843744742F143B5826D3DA7D551B2EC
#define WJH_SLOTMAP_0843744742F143B5826D3DA7D551B2EC

#include "Slot.hpp"

#include <cstddef>
#include <memory>
#include <new>

namespace wjh::slotmap::detail {

/**
 * A slab containing a fixed-size array of slots plus metadata.
 *
 * Memory layout:
 *   - Slab metadata (dead_count, slots_per_slab)
 *   - Array of Slot<T, IndexT, VersionT> objects
 *
 * @tparam T The value type stored in slots
 * @tparam IndexT The index type for free-list linking
 * @tparam VersionT The version type for ABA protection
 * @tparam SizeT The size type for counts
 */
template <typename T, typename IndexT, typename VersionT, typename SizeT>
class alignas(std::max(alignof(SizeT), alignof(Slot<T, IndexT, VersionT>))) Slab
{
public:
    using value_type = T;
    using index_type = IndexT;
    using version_type = VersionT;
    using size_type = SizeT;
    using slot_type = Slot<T, IndexT, VersionT>;

    /**
     * Create a new slab with the given number of slots.
     *
     * @param slots_per_slab Number of slots in this slab (must be > 0)
     * @return Unique pointer to the newly created slab
     * @throws std::bad_alloc if allocation fails
     */
    [[nodiscard]]
    static std::unique_ptr<Slab> create(size_type slots_per_slab)
    {
        // Calculate total size needed: header + slots array
        auto const bytes_needed = sizeof(Slab) +
            slots_per_slab * sizeof(slot_type);

        // Allocate raw memory with proper alignment
        void * raw = ::operator new (
            bytes_needed,
            std::align_val_t{alignof(Slab)});

        // Construct the Slab header
        auto * slab = ::new (raw) Slab(slots_per_slab);

        // Construct all the slots
        auto * slots = reinterpret_cast<std::byte *>(slab + 1);
        for (size_type i = 0; i < slots_per_slab; ++i) {
            ::new (static_cast<void *>(slots)) slot_type{};
            slots += sizeof(slot_type);
        }

        return std::unique_ptr<Slab>(slab);
    }

    // Non-copyable, non-movable
    Slab(Slab const &) = delete;
    Slab & operator = (Slab const &) = delete;
    Slab(Slab &&) = delete;
    Slab & operator = (Slab &&) = delete;

    ~Slab()
    {
        auto * slots = this->slots();
        // Destroy all slots (note: alive slots should have been destroyed
        // by SlotMap before this is called)
        for (size_type i = 0; i < slots_per_slab_; ++i) {
            slots[i].~slot_type();
        }
    }

    // ========================================================================
    // Prevent direct allocation - use create() instead
    // ========================================================================

    // Only operator delete is public (needed by unique_ptr destructor)
    static void operator delete (void * ptr)
    {
        ::operator delete (ptr, std::align_val_t{alignof(Slab)});
    }

    static void operator delete[] (void *) = delete;

    // ========================================================================
    // Slot access
    // ========================================================================

    [[nodiscard]]
    slot_type & slot(size_type index) noexcept
    {
        return slots()[index];
    }

    [[nodiscard]]
    slot_type const & slot(size_type index) const noexcept
    {
        return slots()[index];
    }

    [[nodiscard]]
    size_type slots_per_slab() const noexcept
    {
        return slots_per_slab_;
    }

    // ========================================================================
    // Dead count tracking
    // ========================================================================

    [[nodiscard]]
    size_type dead_count() const noexcept
    {
        return dead_count_;
    }

    void increment_dead_count() noexcept { ++dead_count_; }

    void reset_dead_count() noexcept { dead_count_ = 0; }

private:
    explicit Slab(size_type slots_per_slab) noexcept
    : dead_count_{0}
    , slots_per_slab_{slots_per_slab}
    { }

    slot_type * slots()
    {
        return std::launder(reinterpret_cast<slot_type *>(this + 1));
    }

    slot_type const * slots() const
    {
        return std::launder(reinterpret_cast<slot_type const *>(this + 1));
    }

    size_type dead_count_;
    size_type slots_per_slab_;
};

} // namespace wjh::slotmap::detail

#endif // WJH_SLOTMAP_0843744742F143B5826D3DA7D551B2EC
