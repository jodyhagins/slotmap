// ----------------------------------------------------------------------
// Copyright 2025 Jody Hagins
// Distributed under the MIT Software License
// See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT
// ----------------------------------------------------------------------
#ifndef WJH_SLOTMAP_FC1C5D6C7D5C44F8AA067828C821C8DF
#define WJH_SLOTMAP_FC1C5D6C7D5C44F8AA067828C821C8DF

namespace wjh::slotmap {

template <typename KeyT>
SlotMap<KeyT>::
SlotMap()
: SlotMap(compute_default_slab_size())
{ }

template <typename KeyT>
SlotMap<KeyT>::
SlotMap(size_type slots_per_slab)
: slots_per_slab_{slots_per_slab}
{
    validate_slab_size(slots_per_slab);
    log2_slots_per_slab_ = static_cast<unsigned>(
        std::countr_zero(slots_per_slab_));
}

template <typename KeyT>
SlotMap<KeyT>::
SlotMap(SlotMap && other) noexcept
: slabs_{std::move(other.slabs_)}
, free_list_head_{other.free_list_head_}
, size_{other.size_}
, slots_per_slab_{other.slots_per_slab_}
, log2_slots_per_slab_{other.log2_slots_per_slab_}
, next_slab_base_index_{other.next_slab_base_index_}
{
    other.free_list_head_ = end_of_free_list;
    other.size_ = 0;
    other.next_slab_base_index_ = 0;
}

template <typename KeyT>
SlotMap<KeyT> &
SlotMap<KeyT>::
operator = (SlotMap && other) noexcept
{
    if (this != &other) {
        // Clear current slabs (destructors handle alive slot cleanup)
        clear_slabs();

        slabs_ = std::move(other.slabs_);
        free_list_head_ = other.free_list_head_;
        size_ = other.size_;
        slots_per_slab_ = other.slots_per_slab_;
        log2_slots_per_slab_ = other.log2_slots_per_slab_;
        next_slab_base_index_ = other.next_slab_base_index_;

        other.free_list_head_ = end_of_free_list;
        other.size_ = 0;
        other.next_slab_base_index_ = 0;
    }
    return *this;
}

template <typename KeyT>
bool
SlotMap<KeyT>::
is_empty() const noexcept
{
    return size_ == 0;
}

template <typename KeyT>
SlotMap<KeyT>::size_type
SlotMap<KeyT>::
size() const noexcept
{
    return size_type(size_);
}

template <typename KeyT>
constexpr SlotMap<KeyT>::size_type
SlotMap<KeyT>::
compute_default_slab_size() noexcept
{
    constexpr size_type max_slots = end_of_free_list;

    // Use a single slab by default if it fits in 2MB.
    constexpr std::size_t limit = 2 * 1024 * 1024;
    if (auto n = slab_type::total_bytes_needed(size_type(max_slots));
        n <= limit)
    {
        return max_slots;
    }

    // Otherwise use 4096 slots per slab, but cap at largest power of 2
    // that fits in max_slots
    constexpr std::size_t default_size = 4096;
    if (default_size <= max_slots.value) {
        return size_type(naked_size_type(default_size));
    }
    return size_type(std::bit_floor(max_slots.value));
}

template <typename KeyT>
void
SlotMap<KeyT>::
validate_slab_size(size_type slots_per_slab)
{
    if (slots_per_slab.value == 0) {
        throw std::invalid_argument(
            "SlotMap: slots_per_slab must be greater than 0");
    }

    if (not std::has_single_bit(slots_per_slab.value)) {
        throw std::invalid_argument(
            "SlotMap: slots_per_slab must be a power of 2");
    }

    // Ensure slab size doesn't exceed the addressable index space
    if (slots_per_slab > end_of_free_list) {
        std::stringstream strm;
        strm << "SlotMap: slots_per_slab (" << slots_per_slab.value
            << ") exceeds maximum size (" << end_of_free_list.value << ")";
        throw std::invalid_argument(strm.str());
    }
}

template <typename KeyT>
SlotMap<KeyT>::slot_type &
SlotMap<KeyT>::
get_slot(index_type idx) noexcept
{
    auto const slab_idx = static_cast<std::size_t>(idx >> log2_slots_per_slab_);
    auto const slot_idx = static_cast<size_type>(idx & (slots_per_slab_ - 1));
    return slabs_[slab_idx]->slot(slot_idx);
}

template <typename KeyT>
SlotMap<KeyT>::slot_type const &
SlotMap<KeyT>::
get_slot(index_type idx) const noexcept
{
    auto const slab_idx = static_cast<std::size_t>(idx >> log2_slots_per_slab_);
    auto const slot_idx = static_cast<size_type>(idx & (slots_per_slab_ - 1));
    return slabs_[slab_idx]->slot(slot_idx);
}

template <typename KeyT>
void
SlotMap<KeyT>::
clear_slabs() noexcept
{
    slabs_.clear();
    free_list_head_ = end_of_free_list;
    size_ = 0;
    next_slab_base_index_ = 0;
}

} // namespace wjh::slotmap

#endif // WJH_SLOTMAP_FC1C5D6C7D5C44F8AA067828C821C8DF
