// ----------------------------------------------------------------------
// Copyright 2025 Jody Hagins
// Distributed under the MIT Software License
// See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT
// ----------------------------------------------------------------------
#ifndef WJH_SLOTMAP_006AFAE205A14825A3C8918143AE0270
#define WJH_SLOTMAP_006AFAE205A14825A3C8918143AE0270

#include "Slot.hpp"

#include <cassert>
#include <cstddef>
#include <cstring>
#include <memory>
#include <new>

namespace wjh::slotmap::detail {

template <typename T, typename IndexT, typename VersionT, typename SizeT>
Slab<T, IndexT, VersionT, SizeT>::
Slab(size_type slots_per_slab) noexcept
: dead_count_{0}
, slots_per_slab_{slots_per_slab}
{ }

template <typename T, typename IndexT, typename VersionT, typename SizeT>
constexpr std::size_t
Slab<T, IndexT, VersionT, SizeT>::
total_bytes_needed(size_type slots_per_slab)
{
    static_assert(std::is_unsigned_v<naked_size_type>);
    static_assert(sizeof(naked_size_type) <= sizeof(std::size_t));

    auto const slots_bytes = slots_per_slab.value * sizeof(slot_type);
    auto const bitmap_bytes = bitmap_size(slots_per_slab);
    auto const bytes_needed = sizeof(Slab) + slots_bytes + bitmap_bytes;
    return bytes_needed;
}

template <typename T, typename IndexT, typename VersionT, typename SizeT>
std::unique_ptr<Slab<T, IndexT, VersionT, SizeT>>
Slab<T, IndexT, VersionT, SizeT>::
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

template <typename T, typename IndexT, typename VersionT, typename SizeT>
template <typename ValT>
std::unique_ptr<Slab<T, IndexT, VersionT, SizeT>>
Slab<T, IndexT, VersionT, SizeT>::
create(ValT slots_per_slab)
requires requires { size_type(slots_per_slab); }
{
    return create(size_type(slots_per_slab));
}

template <typename T, typename IndexT, typename VersionT, typename SizeT>
Slab<T, IndexT, VersionT, SizeT>::
~Slab()
{
    // Destroy all alive slots
    auto * const slots = this->slots();
    for (naked_size_type i = 0; i < slots_per_slab_; ++i) {
        if (is_alive(index_type(naked_index_type(i)))) {
            slots[i].destroy();
        }
        slots[i].~slot_type();
    }
}

template <typename T, typename IndexT, typename VersionT, typename SizeT>
void
Slab<T, IndexT, VersionT, SizeT>::
operator delete (void * ptr)
{
    ::operator delete (ptr, std::align_val_t{alignof(Slab)});
}

template <typename T, typename IndexT, typename VersionT, typename SizeT>
template <typename... Args>
Slab<T, IndexT, VersionT, SizeT>::version_type
Slab<T, IndexT, VersionT, SizeT>::
emplace(index_type index, Args &&... args)
{
    assert(not is_alive(index));
    auto & s = slots()[index];
    auto const ver = s.version();
    s.emplace(std::forward<Args>(args)...);
    set_alive(index, true);
    return version_type{ver};
}

template <typename T, typename IndexT, typename VersionT, typename SizeT>
bool
Slab<T, IndexT, VersionT, SizeT>::
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

template <typename T, typename IndexT, typename VersionT, typename SizeT>
bool
Slab<T, IndexT, VersionT, SizeT>::
is_alive(index_type index) const noexcept
{
    auto const byte_idx = static_cast<std::size_t>(index) / 8;
    auto const bit_idx = static_cast<unsigned>(index % 8);
    return (bitmap()[byte_idx] & (std::byte{1} << bit_idx)) != std::byte{0};
}

template <typename T, typename IndexT, typename VersionT, typename SizeT>
[[nodiscard]]
Slab<T, IndexT, VersionT, SizeT>::slot_type &
Slab<T, IndexT, VersionT, SizeT>::
slot(index_type index) noexcept
{
    return slots()[index];
}

template <typename T, typename IndexT, typename VersionT, typename SizeT>
Slab<T, IndexT, VersionT, SizeT>::slot_type const &
Slab<T, IndexT, VersionT, SizeT>::
slot(index_type index) const noexcept
{
    return slots()[index];
}

template <typename T, typename IndexT, typename VersionT, typename SizeT>
Slab<T, IndexT, VersionT, SizeT>::size_type
Slab<T, IndexT, VersionT, SizeT>::
slots_per_slab() const noexcept
{
    return slots_per_slab_;
}

template <typename T, typename IndexT, typename VersionT, typename SizeT>
Slab<T, IndexT, VersionT, SizeT>::size_type
Slab<T, IndexT, VersionT, SizeT>::
dead_count() const noexcept
{
    return dead_count_;
}

template <typename T, typename IndexT, typename VersionT, typename SizeT>
bool
Slab<T, IndexT, VersionT, SizeT>::
can_be_recycled() const noexcept
{
    return dead_count_ == slots_per_slab_;
}

template <typename T, typename IndexT, typename VersionT, typename SizeT>
void
Slab<T, IndexT, VersionT, SizeT>::
recycle(index_type first_index, size_type last_next)
{
    assert(can_be_recycled());
    assert(are_all_dead());

    dead_count_ = 0;
    auto const end = this->slots() + slots_per_slab_ - 1;
    for (auto slot = this->slots(); slot != end; ++slot) {
        first_index.value += 1;
        slot->set_version(version_type{0});
        slot->set_next(first_index);
    }
    end->set_version(version_type{0});
    end->set_next(last_next);

    // Make sure...
    std::memset(bitmap(), 0, bitmap_size(slots_per_slab_));
}

template <typename T, typename IndexT, typename VersionT, typename SizeT>
constexpr std::size_t
Slab<T, IndexT, VersionT, SizeT>::
bitmap_size(size_type slots_per_slab) noexcept
{
    return (static_cast<std::size_t>(slots_per_slab) + 7) / 8;
}

template <typename T, typename IndexT, typename VersionT, typename SizeT>
Slab<T, IndexT, VersionT, SizeT>::slot_type *
Slab<T, IndexT, VersionT, SizeT>::
slots() noexcept
{
    return std::launder(reinterpret_cast<slot_type *>(this + 1));
}

template <typename T, typename IndexT, typename VersionT, typename SizeT>
Slab<T, IndexT, VersionT, SizeT>::slot_type const *
Slab<T, IndexT, VersionT, SizeT>::
slots() const noexcept
{
    return std::launder(reinterpret_cast<slot_type const *>(this + 1));
}

template <typename T, typename IndexT, typename VersionT, typename SizeT>
std::byte *
Slab<T, IndexT, VersionT, SizeT>::
bitmap() noexcept
{
    auto * slot_end = reinterpret_cast<std::byte *>(slots() + slots_per_slab_);
    return std::launder(slot_end);
}

template <typename T, typename IndexT, typename VersionT, typename SizeT>
std::byte const *
Slab<T, IndexT, VersionT, SizeT>::
bitmap() const noexcept
{
    auto const * slot_end = reinterpret_cast<std::byte const *>(
        slots() + slots_per_slab_);
    return std::launder(slot_end);
}

template <typename T, typename IndexT, typename VersionT, typename SizeT>
void
Slab<T, IndexT, VersionT, SizeT>::
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

template <typename T, typename IndexT, typename VersionT, typename SizeT>
bool
Slab<T, IndexT, VersionT, SizeT>::
are_all_dead() const
{
    auto const limit = bitmap_size(slots_per_slab_);
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
