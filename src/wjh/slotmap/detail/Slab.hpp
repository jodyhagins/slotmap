// ----------------------------------------------------------------------
// Copyright 2025 Jody Hagins
// Distributed under the MIT Software License
// See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT
// ----------------------------------------------------------------------
// INTERNAL IMPLEMENTATION HEADER - Do not include directly.
// Use <wjh/slotmap/SlotMap.hpp> or <wjh/slotmap.hpp> instead.
// ----------------------------------------------------------------------
#ifndef WJH_SLOTMAP_0843744742F143B5826D3DA7D551B2EC
#define WJH_SLOTMAP_0843744742F143B5826D3DA7D551B2EC

#include "Slot.hpp"

#include <bit>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <memory>
#include <new>

namespace wjh::slotmap {
WJH_SLOTMAP_NAMESPACE_BEGIN

namespace detail {

template <
    typename ValueT,
    typename IndexT,
    typename VersionT,
    typename SizeT,
    bool AllowAliveBit>
struct SlabTraits
{
    using value_type = ValueT;
    using index_type = IndexT;
    using version_type = VersionT;
    using size_type = SizeT;

    using slab_traits = SlabTraits;
    using slot_traits =
        SlotTraits<value_type, size_type, version_type, AllowAliveBit>;
    using slot_type = Slot<slot_traits>;

    static constexpr bool allow_alive_bit = AllowAliveBit;
};

/**
 * A slab containing a fixed-size array of slots plus metadata.
 *
 * Memory layout:
 *   - Slab metadata (dead_count, slots_per_slab)
 *   - Array of Slot objects
 *   - Alive bitmap (ceil(slots_per_slab / 8) bytes)
 *
 * @tparam TraitsT  An instance of SlabTraits
 */
template <typename TraitsT>
class alignas(std::max(
    alignof(typename TraitsT::size_type),
    alignof(typename TraitsT::slot_type))) Slab
{
public:
    using value_type = typename TraitsT::value_type;
    using index_type = typename TraitsT::index_type;
    using version_type = typename TraitsT::version_type;
    using size_type = typename TraitsT::size_type;
    using slot_traits = typename TraitsT::slot_traits;
    using slot_type = typename TraitsT::slot_type;

    static constexpr auto max_version = version_type(version_type::mask);

    [[nodiscard]]
    static constexpr std::size_t total_bytes_needed(size_type slots_per_slab)
    {
        static_assert(std::is_unsigned_v<naked_size_type>);
        static_assert(sizeof(naked_size_type) <= sizeof(std::size_t));

        auto const slots_bytes = slots_per_slab.value * sizeof(slot_type);
        auto const bitmap_bytes = bitmap_size(slots_per_slab);
        auto const bytes_needed = sizeof(Slab) + slots_bytes + bitmap_bytes;
        return bytes_needed;
    }

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
    static std::unique_ptr<Slab> create(size_type slots_per_slab)
    {
        static_assert(noexcept(Slab(slots_per_slab)));
        static_assert(noexcept(slot_type{}));

        // Allocate raw memory with proper alignment
        void * raw = ::operator new (
            total_bytes_needed(slots_per_slab),
            std::align_val_t{alignof(Slab)});

        // Construct the Slab header
        auto * slab = ::new (raw) Slab(slots_per_slab);

        // Construct all the slots (value-initialized to zero)
        auto * slot_mem = reinterpret_cast<std::byte *>(slab + 1);
        for (naked_size_type i = 0; i < slots_per_slab; ++i) {
            ::new (static_cast<void *>(slot_mem)) slot_type{};
            slot_mem += sizeof(slot_type);
        }

        // Zero the bitmap (slot_mem now points to bitmap start)
        std::memset(slot_mem, 0, bitmap_size(slots_per_slab));

        return std::unique_ptr<Slab>(slab);
    }

    template <typename ValT>
    static std::unique_ptr<Slab> create(ValT slots_per_slab)
    requires requires { size_type(slots_per_slab); }
    {
        return create(size_type(slots_per_slab));
    }

    /**
     * Create a deep copy of this slab.
     *
     * Copies all slot metadata (versions, next links) and alive values.
     * The new slab has identical structure to the original.
     *
     * @return Unique pointer to the cloned slab
     * @throws std::bad_alloc if allocation fails
     * @throws Any exception from value_type's copy constructor
     *
     * @note Only available if value_type is copy constructible
     */
    [[nodiscard]]
    std::unique_ptr<Slab> clone() const
    requires std::is_copy_constructible_v<value_type>
    {
        auto const slab_size = size_type(slots_per_slab_);

        // Allocate raw memory with proper alignment
        void * raw = ::operator new (
            total_bytes_needed(slab_size),
            std::align_val_t{alignof(Slab)});

        // Construct the Slab header with same dead_count
        auto * new_slab = ::new (raw) Slab(slab_size);
        new_slab->dead_count_ = dead_count_;

        // Track how many slots we've successfully copied (for exception safety)
        naked_size_type slots_constructed = 0;

        try {
            auto const * src_slots = this->slots();
            auto * dst_mem = reinterpret_cast<std::byte *>(new_slab + 1);

            for (naked_size_type i = 0; i < slots_per_slab_; ++i) {
                auto * dst_slot = ::new (static_cast<void *>(dst_mem))
                    slot_type{};
                auto & src = src_slots[i];

                dst_slot->set_version(src.version());
                if (is_alive(index_type(naked_index_type(i)))) {
                    dst_slot->emplace(src.value());
                } else {
                    dst_slot->set_next(src.next());
                }

                dst_mem += sizeof(slot_type);
                ++slots_constructed;
            }

            // Copy the bitmap (dst_mem now points to bitmap start)
            std::memcpy(dst_mem, bitmap(), bitmap_size(slab_size));

            return std::unique_ptr<Slab>(new_slab);

        } catch (...) {
            // Clean up partially constructed slots
            auto * dst_mem = reinterpret_cast<std::byte *>(new_slab + 1);
            for (naked_size_type i = 0; i < slots_constructed; ++i) {
                auto * dst_slot = std::launder(
                    reinterpret_cast<slot_type *>(dst_mem));
                if (is_alive(index_type(naked_index_type(i)))) {
                    dst_slot->destroy();
                }
                std::destroy_at(dst_slot);
                dst_mem += sizeof(slot_type);
            }
            std::destroy_at(new_slab);
            ::operator delete (raw, std::align_val_t{alignof(Slab)});
            throw;
        }
    }

    // Non-copyable, non-movable
    Slab(Slab const &) = delete;
    Slab & operator = (Slab const &) = delete;
    Slab(Slab &&) = delete;
    Slab & operator = (Slab &&) = delete;

    ~Slab()
    {
        auto * const slot_array = this->slots();

        // Destroy all alive values using bitmap scanning
        for_each_alive(
            [slot_array](index_type idx) { slot_array[idx].destroy(); });

        // Destroy all slot objects
        // Note: If slot_type is trivially destructible, this loop optimizes
        // away
        for (naked_size_type i = 0; i < slots_per_slab_; ++i) {
            slot_array[i].~slot_type();
        }
    }

    static void operator delete (void * ptr)
    {
        ::operator delete (ptr, std::align_val_t{alignof(Slab)});
    }

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
     * @param args Arguments to forward to value_type's constructor
     * @return The version for this insertion (to be used in the key)
     *
     * @pre Slot must not be alive (is_alive(index) == false)
     * @post Slot is alive (is_alive(index) == true)
     */
    template <typename... Args>
    EmplaceResult emplace(index_type index, Args &&... args)
    {
        assert(not is_alive(index));
        auto & s = slots()[index];
        auto result = EmplaceResult{.version = s.version(), .next = s.next()};
        s.emplace(std::forward<Args>(args)...);
        set_alive(index, true);
        return result;
    }

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
    bool destroy(index_type index)
    {
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

    /**
     * Check if a slot is alive (has a constructed value).
     */
    [[nodiscard]]
    bool is_alive(index_type index) const noexcept
    {
        auto const byte_idx = static_cast<std::size_t>(index) / 8;
        auto const bit_idx = static_cast<unsigned>(index % 8);
        return (bitmap()[byte_idx] & (std::byte{1} << bit_idx)) != std::byte{0};
    }

    /**
     * Iterate over all alive slots.
     *
     * @param func Callback invoked for each alive slot with signature:
     *             void(index_type slot_index) or
     *             bool(index_type slot_index) - return false to stop
     * @return Number of slots visited
     */
    template <typename F>
    size_type for_each_alive(F && func) const
    {
        auto const * bm = bitmap();
        auto const num_slots = slots_per_slab_;
        naked_size_type visited = 0;

        // Process bitmap in 64-bit chunks for efficiency
        std::size_t slot_base = 0;
        std::size_t const num_bytes = bitmap_size(size_type(num_slots));

        // Process 8-byte (64-bit) chunks
        std::size_t byte_idx = 0;
        while (byte_idx + 8 <= num_bytes) {
            // Load 64 bits from the bitmap
            std::uint64_t word;
            std::memcpy(&word, bm + byte_idx, sizeof(word));

            while (word != 0) {
                // Find the index of the lowest set bit
                auto const bit_pos = static_cast<std::size_t>(
                    std::countr_zero(word));
                auto const slot_idx = static_cast<naked_index_type>(
                    slot_base + bit_pos);

                // Invoke callback
                using R = decltype(func(index_type(slot_idx)));
                if constexpr (std::is_same_v<R, bool>) {
                    if (not func(index_type(slot_idx))) {
                        return size_type(visited);
                    }
                } else {
                    func(index_type(slot_idx));
                }
                ++visited;

                // Clear the lowest set bit
                word &= word - 1;
            }

            byte_idx += 8;
            slot_base += 64;
        }

        // Process remaining bytes one at a time
        while (byte_idx < num_bytes) {
            auto byte_val = static_cast<unsigned char>(bm[byte_idx]);

            while (byte_val != 0) {
                auto const bit_pos = static_cast<std::size_t>(
                    std::countr_zero(byte_val));
                auto const slot_idx = static_cast<naked_index_type>(
                    slot_base + bit_pos);

                // Bitmap may have padding bits when slots_per_slab is not a
                // multiple of 8. These padding bits are always 0, but we check
                // bounds defensively to ensure we never access beyond the
                // allocated slots array even if padding bits were corrupted.
                if (slot_idx >= num_slots) {
                    break;
                }

                using R = decltype(func(index_type(slot_idx)));
                if constexpr (std::is_same_v<R, bool>) {
                    if (not func(index_type(slot_idx))) {
                        return size_type(visited);
                    }
                } else {
                    func(index_type(slot_idx));
                }
                ++visited;

                byte_val &= static_cast<unsigned char>(byte_val - 1);
            }

            ++byte_idx;
            slot_base += 8;
        }

        return size_type(visited);
    }

    // ========================================================================
    // Slot access (for free-list management by SlotMap)
    // ========================================================================

    [[nodiscard]]
    slot_type & slot(index_type index) noexcept
    {
        return slots()[index];
    }

    [[nodiscard]]
    slot_type const & slot(index_type index) const noexcept
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

    [[nodiscard]]
    bool can_be_recycled() const noexcept
    {
        return dead_count_ == slots_per_slab_;
    }

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
    void recycle(index_type first_index, size_type last_next)
    {
        assert(can_be_recycled());
        assert(are_all_dead());

        dead_count_ = 0;
        auto const end = this->slots() + slots_per_slab_ - 1;
        for (auto slot = this->slots(); slot != end; ++slot) {
            first_index.value += 1;
            slot->set_version(version_type{});
            slot->set_next(first_index);
        }
        end->set_version(version_type{});
        end->set_next(last_next);

        // Make sure...
        std::memset(bitmap(), 0, bitmap_size(size_type(slots_per_slab_)));
    }

private:
    using naked_index_type = typename index_type::value_type;
    using naked_size_type = typename size_type::value_type;

    explicit Slab(size_type slots_per_slab) noexcept
    : dead_count_{0}
    , slots_per_slab_{slots_per_slab}
    { }

    /**
     * Calculate the number of bytes needed for the alive bitmap.
     */
    static constexpr std::size_t bitmap_size(size_type slots_per_slab) noexcept
    {
        return (static_cast<std::size_t>(slots_per_slab) + 7) / 8;
    }

    slot_type * slots() noexcept
    {
        return std::launder(reinterpret_cast<slot_type *>(this + 1));
    }

    slot_type const * slots() const noexcept
    {
        return std::launder(reinterpret_cast<slot_type const *>(this + 1));
    }

    std::byte * bitmap() noexcept
    {
        auto * slot_end = reinterpret_cast<std::byte *>(
            slots() + slots_per_slab_);
        return std::launder(slot_end);
    }

    std::byte const * bitmap() const noexcept
    {
        auto const * slot_end = reinterpret_cast<std::byte const *>(
            slots() + slots_per_slab_);
        return std::launder(slot_end);
    }

    void set_alive(index_type index, bool alive) noexcept
    {
        auto const byte_idx = static_cast<std::size_t>(index) / 8;
        auto const bit_idx = static_cast<unsigned>(index % 8);
        auto const mask = std::byte{1} << bit_idx;

        if (alive) {
            bitmap()[byte_idx] |= mask;
        } else {
            bitmap()[byte_idx] &= ~mask;
        }
    }

    [[maybe_unused]]
    bool are_all_dead() const
    {
        auto const limit = bitmap_size(size_type(slots_per_slab_));
        auto const bytes = bitmap();
        for (std::size_t i = 0; i < limit; ++i) {
            if (bytes[i] != std::byte{0}) {
                return false;
            }
        }
        return true;
    }

    naked_size_type dead_count_;
    naked_size_type slots_per_slab_;
};

} // namespace detail

WJH_SLOTMAP_NAMESPACE_END
} // namespace wjh::slotmap

#endif // WJH_SLOTMAP_0843744742F143B5826D3DA7D551B2EC
