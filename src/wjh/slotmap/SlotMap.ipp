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
    return const_cast<slot_type &>(
        const_cast<SlotMap const &>(*this).get_slot(idx));
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

template <typename KeyT>
SlotMap<KeyT>::slab_type *
SlotMap<KeyT>::
get_slab(index_type idx) noexcept
{
    return const_cast<slab_type *>(
        const_cast<SlotMap const &>(*this).get_slab(idx));
}

template <typename KeyT>
SlotMap<KeyT>::slab_type const *
SlotMap<KeyT>::
get_slab(index_type idx) const noexcept
{
    auto const slab_idx = static_cast<std::size_t>(idx >> log2_slots_per_slab_);
    if (slab_idx >= slabs_.size()) {
        return nullptr;
    }
    return slabs_[slab_idx].get();
}

template <typename KeyT>
bool
SlotMap<KeyT>::
allocate_new_slab()
{
    // Check if we've exhausted the index space
    if (size_type(next_slab_base_index_) >= end_of_free_list) {
        return false;
    }

    auto const new_slab_idx = static_cast<std::size_t>(
        next_slab_base_index_ >> log2_slots_per_slab_);

    // Ensure vector is large enough
    if (new_slab_idx >= slabs_.size()) {
        slabs_.resize(new_slab_idx + 1);
    }

    // Create the slab
    auto slab = slab_type::create(size_type(slots_per_slab_));

    // Initialize the free list within the slab
    initialize_slab_free_list(
        slab.get(),
        index_type(static_cast<naked_index_type>(next_slab_base_index_)));

    // Special case: first slab, slot 0 gets version 1 to avoid null key
    if (new_slab_idx == 0) {
        using naked_version_type = typename version_type::value_type;
        slab->slot(index_type(naked_index_type{0}))
            .set_version(version_type{naked_version_type{1}});
    }

    slabs_[new_slab_idx] = std::move(slab);
    next_slab_base_index_ += slots_per_slab_;

    return true;
}

template <typename KeyT>
void
SlotMap<KeyT>::
initialize_slab_free_list(slab_type * slab, index_type base)
{
    // Link all slots in the slab into a chain
    // Each slot points to the next, last slot points to old free list head
    auto const limit = static_cast<naked_size_type>(slots_per_slab_ - 1);
    auto const base_val = static_cast<naked_size_type>(base.value);
    for (naked_size_type i = 0; i < limit; ++i) {
        auto const idx = index_type(static_cast<naked_index_type>(i));
        auto const next_val = static_cast<naked_size_type>(base_val + i + 1);
        slab->slot(idx).set_next(size_type(next_val));
    }

    // Last slot points to old free list head
    auto const last_idx = index_type(static_cast<naked_index_type>(limit));
    slab->slot(last_idx).set_next(free_list_head_);

    // New free list head is first slot in new slab
    free_list_head_ = size_type(base);
}

template <typename KeyT>
template <typename... Args>
SlotMap<KeyT>::key_type
SlotMap<KeyT>::
emplace(Args &&... args)
{
    // Check if free list is empty, allocate new slab if needed
    if (free_list_head_ == end_of_free_list) {
        if (not allocate_new_slab()) {
            return key_type::null();
        }
    }

    // Pop from free list - convert size_type to index_type via value
    auto const idx = index_type(naked_index_type(free_list_head_.value));
    auto * slab = get_slab(idx);
    assert(slab);
    auto const slot_idx = index_type(
        naked_index_type(idx.value & (slots_per_slab_ - 1)));

    // Emplace the value - this returns the version and sets alive bit
    // Strong exception guarantee: if this throws, we haven't modified state
    auto [ver, next] = slab->emplace(slot_idx, std::forward<Args>(args)...);

    // Update free list head (only after successful emplace)
    free_list_head_ = next;
    ++size_;

    // TODO: allow user to set a default-user-type-value that gets used here.
    return key_type(idx, ver, user_type{});
}

template <typename KeyT>
bool
SlotMap<KeyT>::
erase(key_type key)
{
    auto const key_idx = key.index();
    if (auto * slab = get_slab(key_idx)) {
        auto const slot_idx = index_type(
            naked_index_type(key_idx.value & (slots_per_slab_ - 1)));
        if (auto & slot = slab->slot(slot_idx);
            slot.version() == key.version() && slab->is_alive(slot_idx))
        {
            // Destroy the value - returns true if slot can be reused
            bool const can_reuse = slab->destroy(slot_idx);
            --size_;
            if (can_reuse) {
                // Add to free list
                slot.set_next(free_list_head_);
                free_list_head_ = size_type(key_idx);
            }
            // TODO: Phase 6 - slab recycling when can_reuse is false

            return true;
        }
    }
    return false;
}

template <typename KeyT>
template <typename F>
bool
SlotMap<KeyT>::
use(key_type key, F && func)
{
    return const_cast<SlotMap const &>(*this).use(
        key,
        [&func](value_type const & x) {
            std::forward<F>(func)(const_cast<value_type &>(x));
        });
}

template <typename KeyT>
template <typename F>
bool
SlotMap<KeyT>::
use(key_type key, F && func) const
{
    auto const key_idx = key.index();
    if (auto const * slab = get_slab(key_idx)) {
        auto const slot_idx = index_type(
            naked_index_type(key_idx.value & (slots_per_slab_ - 1)));
        if (auto & slot = slab->slot(slot_idx);
            slot.version() == key.version() && slab->is_alive(slot_idx))
        {
            // Invoke the callable
            std::forward<F>(func)(std::as_const(slot.value()));
            return true;
        }
    }
    return false;
}

template <typename KeyT>
bool
SlotMap<KeyT>::
contains(key_type key) const
{
    return use(key, [](value_type const &) {});
}

} // namespace wjh::slotmap

#endif // WJH_SLOTMAP_FC1C5D6C7D5C44F8AA067828C821C8DF
