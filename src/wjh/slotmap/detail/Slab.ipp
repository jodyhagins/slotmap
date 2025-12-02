// ----------------------------------------------------------------------
// Copyright 2025 Jody Hagins
// Distributed under the MIT Software License
// See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT
// ----------------------------------------------------------------------
#ifndef WJH_SLOTMAP_006AFAE205A14825A3C8918143AE0270
#define WJH_SLOTMAP_006AFAE205A14825A3C8918143AE0270

#include "Slot.hpp"

#include <bit>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <memory>
#include <new>

namespace wjh::slotmap::detail {

template <typename TraitsT>
Slab<TraitsT>::
Slab(size_type slots_per_slab) noexcept
: dead_count_{0}
, slots_per_slab_{slots_per_slab}
{ }

template <typename TraitsT>
constexpr std::size_t
Slab<TraitsT>::
total_bytes_needed(size_type slots_per_slab)
{
    static_assert(std::is_unsigned_v<naked_size_type>);
    static_assert(sizeof(naked_size_type) <= sizeof(std::size_t));

    auto const slots_bytes = slots_per_slab.value * sizeof(slot_type);
    auto const bitmap_bytes = bitmap_size(slots_per_slab);
    auto const bytes_needed = sizeof(Slab) + slots_bytes + bitmap_bytes;
    return bytes_needed;
}

template <typename TraitsT>
std::unique_ptr<Slab<TraitsT>>
Slab<TraitsT>::
create(size_type slots_per_slab)
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

template <typename TraitsT>
template <typename ValT>
std::unique_ptr<Slab<TraitsT>>
Slab<TraitsT>::
create(ValT slots_per_slab)
requires requires { size_type(slots_per_slab); }
{
    return create(size_type(slots_per_slab));
}

template <typename TraitsT>
std::unique_ptr<Slab<TraitsT>>
Slab<TraitsT>::
clone() const
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
            auto * dst_slot = ::new (static_cast<void *>(dst_mem)) slot_type{};
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

template <typename TraitsT>
Slab<TraitsT>::
~Slab()
{
    auto * const slot_array = this->slots();

    // Destroy all alive values using bitmap scanning
    for_each_alive([slot_array](index_type idx) { slot_array[idx].destroy(); });

    // Destroy all slot objects
    // Note: If slot_type is trivially destructible, this loop optimizes away
    for (naked_size_type i = 0; i < slots_per_slab_; ++i) {
        slot_array[i].~slot_type();
    }
}

template <typename TraitsT>
void
Slab<TraitsT>::
operator delete (void * ptr)
{
    ::operator delete (ptr, std::align_val_t{alignof(Slab)});
}

template <typename TraitsT>
template <typename... Args>
typename Slab<TraitsT>::EmplaceResult
Slab<TraitsT>::
emplace(index_type index, Args &&... args)
{
    assert(not is_alive(index));
    auto & s = slots()[index];
    auto result = EmplaceResult{.version = s.version(), .next = s.next()};
    s.emplace(std::forward<Args>(args)...);
    set_alive(index, true);
    return result;
}

template <typename TraitsT>
bool
Slab<TraitsT>::
destroy(index_type index)
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

template <typename TraitsT>
bool
Slab<TraitsT>::
is_alive(index_type index) const noexcept
{
    auto const byte_idx = static_cast<std::size_t>(index) / 8;
    auto const bit_idx = static_cast<unsigned>(index % 8);
    return (bitmap()[byte_idx] & (std::byte{1} << bit_idx)) != std::byte{0};
}

template <typename TraitsT>
template <typename F>
typename Slab<TraitsT>::size_type
Slab<TraitsT>::
for_each_alive(F && func) const
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

            // Don't process slots beyond slots_per_slab
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

template <typename TraitsT>
[[nodiscard]]
typename Slab<TraitsT>::slot_type &
Slab<TraitsT>::
slot(index_type index) noexcept
{
    return slots()[index];
}

template <typename TraitsT>
typename Slab<TraitsT>::slot_type const &
Slab<TraitsT>::
slot(index_type index) const noexcept
{
    return slots()[index];
}

template <typename TraitsT>
typename Slab<TraitsT>::size_type
Slab<TraitsT>::
slots_per_slab() const noexcept
{
    return slots_per_slab_;
}

template <typename TraitsT>
typename Slab<TraitsT>::size_type
Slab<TraitsT>::
dead_count() const noexcept
{
    return dead_count_;
}

template <typename TraitsT>
bool
Slab<TraitsT>::
can_be_recycled() const noexcept
{
    return dead_count_ == slots_per_slab_;
}

template <typename TraitsT>
void
Slab<TraitsT>::
recycle(index_type first_index, size_type last_next)
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

template <typename TraitsT>
constexpr std::size_t
Slab<TraitsT>::
bitmap_size(size_type slots_per_slab) noexcept
{
    return (static_cast<std::size_t>(slots_per_slab) + 7) / 8;
}

template <typename TraitsT>
typename Slab<TraitsT>::slot_type *
Slab<TraitsT>::
slots() noexcept
{
    return std::launder(reinterpret_cast<slot_type *>(this + 1));
}

template <typename TraitsT>
typename Slab<TraitsT>::slot_type const *
Slab<TraitsT>::
slots() const noexcept
{
    return std::launder(reinterpret_cast<slot_type const *>(this + 1));
}

template <typename TraitsT>
std::byte *
Slab<TraitsT>::
bitmap() noexcept
{
    auto * slot_end = reinterpret_cast<std::byte *>(slots() + slots_per_slab_);
    return std::launder(slot_end);
}

template <typename TraitsT>
std::byte const *
Slab<TraitsT>::
bitmap() const noexcept
{
    auto const * slot_end = reinterpret_cast<std::byte const *>(
        slots() + slots_per_slab_);
    return std::launder(slot_end);
}

template <typename TraitsT>
void
Slab<TraitsT>::
set_alive(index_type index, bool alive) noexcept
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

template <typename TraitsT>
bool
Slab<TraitsT>::
are_all_dead() const
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

} // namespace wjh::slotmap::detail

#endif // WJH_SLOTMAP_006AFAE205A14825A3C8918143AE0270
