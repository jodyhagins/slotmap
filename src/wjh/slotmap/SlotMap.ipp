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
SlotMap(SlotMap const & other)
requires std::is_copy_constructible_v<mapped_type>
: free_list_head_{other.free_list_head_}
, size_{other.size_}
, slots_per_slab_{other.slots_per_slab_}
, log2_slots_per_slab_{other.log2_slots_per_slab_}
, next_slab_base_index_{other.next_slab_base_index_}
{
    // Reserve space for all slabs
    slabs_.reserve(other.slabs_.size());

    // Clone each non-null slab
    for (auto const & slab_ptr : other.slabs_) {
        if (slab_ptr) {
            slabs_.push_back(slab_ptr->clone());
        } else {
            slabs_.push_back(nullptr);
        }
    }
}

template <typename KeyT>
SlotMap<KeyT> &
SlotMap<KeyT>::
operator = (SlotMap const & other)
requires std::is_copy_constructible_v<mapped_type>
{
    if (this != &other) {
        // Copy-and-swap idiom for strong exception safety
        SlotMap copy(other);
        swap(copy);
    }
    return *this;
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
try_emplace(Args &&... args)
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
template <typename... Args>
SlotMap<KeyT>::key_type
SlotMap<KeyT>::
emplace(Args &&... args)
{
    auto key = try_emplace(std::forward<Args>(args)...);
    if (key.is_null()) {
        throw std::length_error(
            "SlotMap: capacity exhausted, cannot emplace new element");
    }
    return key;
}

template <typename KeyT>
bool
SlotMap<KeyT>::
erase(key_type key)
{
    auto const key_idx = key.index();
    if (auto * slab = get_slab(key_idx)) {
        auto const slab_idx = static_cast<std::size_t>(
            key_idx >> log2_slots_per_slab_);
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
            } else {
                // Slot is dead - check if slab can be recycled
                try_recycle_slab(slab_idx);
            }

            return true;
        }
    }
    return false;
}

template <typename KeyT>
std::optional<typename SlotMap<KeyT>::mapped_type>
SlotMap<KeyT>::
pop(key_type key)
requires std::is_move_constructible_v<mapped_type>
{
    auto const key_idx = key.index();
    if (auto * slab = get_slab(key_idx)) {
        auto const slab_idx = static_cast<std::size_t>(
            key_idx >> log2_slots_per_slab_);
        auto const slot_idx = index_type(
            naked_index_type(key_idx.value & (slots_per_slab_ - 1)));
        if (auto & slot = slab->slot(slot_idx);
            slot.version() == key.version() && slab->is_alive(slot_idx))
        {
            // Move the value out before destroying
            auto result = std::make_optional(std::move(slot.value()));

            // Destroy the value - returns true if slot can be reused
            bool const can_reuse = slab->destroy(slot_idx);
            --size_;
            if (can_reuse) {
                // Add to free list
                slot.set_next(free_list_head_);
                free_list_head_ = size_type(key_idx);
            } else {
                // Slot is dead - check if slab can be recycled
                try_recycle_slab(slab_idx);
            }

            return result;
        }
    }
    return std::nullopt;
}

template <typename KeyT>
template <typename F>
bool
SlotMap<KeyT>::
use(key_type key, F && func)
{
    return const_cast<SlotMap const &>(*this).use(
        key,
        [&func](mapped_type const & x) {
            std::forward<F>(func)(const_cast<mapped_type &>(x));
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
    return use(key, [](mapped_type const &) {});
}

namespace detail {
template <typename F, typename KeyT, typename ValT>
void
invoke_for_each(F & func, KeyT key, ValT & val, [[maybe_unused]] Break & brk)
{
    if constexpr (std::is_invocable_v<F, KeyT, ValT &, Break &>) {
        std::invoke(func, key, val, brk);
    } else if constexpr (std::is_invocable_v<F, KeyT, ValT &>) {
        std::invoke(func, key, val);
    } else if constexpr (std::is_invocable_v<F, ValT &, Break &>) {
        std::invoke(func, val, brk);
    } else {
        std::invoke(func, val);
    }
}
} // namespace detail

template <typename KeyT>
template <typename F>
SlotMap<KeyT>::size_type
SlotMap<KeyT>::
for_each(F && func)
{
    auto f = [&func](key_type key, mapped_type const & v, Break & brk) {
        detail::invoke_for_each(func, key, const_cast<mapped_type &>(v), brk);
    };
    return const_cast<SlotMap const &>(*this).for_each(f);
}

template <typename KeyT>
template <typename F>
SlotMap<KeyT>::size_type
SlotMap<KeyT>::
for_each(F && func) const
{
    naked_size_type visited = 0;
    Break brk{};

    // Iterate over all slabs
    for (std::size_t slab_idx = 0; slab_idx < slabs_.size() && not brk.stop;
         ++slab_idx)
    {
        auto const * slab = slabs_[slab_idx].get();
        if (not slab) {
            continue; // Skip recycled slabs
        }

        auto const base_idx = static_cast<naked_index_type>(
            slab_idx << log2_slots_per_slab_);

        // Iterate over slots in this slab
        for (naked_size_type slot_idx = 0;
             slot_idx < slots_per_slab_ && not brk.stop;
             ++slot_idx)
        {
            auto const idx = index_type(
                static_cast<naked_index_type>(slot_idx));

            if (slab->is_alive(idx)) {
                // Build the key from index + version
                auto const full_idx = index_type(
                    static_cast<naked_index_type>(base_idx + slot_idx));
                auto const ver = slab->slot(idx).version();
                auto const key = key_type(full_idx, ver, user_type{});

                detail::invoke_for_each(
                    func,
                    key,
                    slab->slot(idx).value(),
                    brk);
                ++visited;
            }
        }
    }

    return size_type(visited);
}

template <typename KeyT>
void
SlotMap<KeyT>::
swap(SlotMap & other) noexcept
{
    using std::swap;
    swap(slabs_, other.slabs_);
    swap(free_list_head_, other.free_list_head_);
    swap(size_, other.size_);
    swap(slots_per_slab_, other.slots_per_slab_);
    swap(log2_slots_per_slab_, other.log2_slots_per_slab_);
    swap(next_slab_base_index_, other.next_slab_base_index_);
}

template <typename KeyT>
void
SlotMap<KeyT>::
clear()
{
    // Start with empty free list - we'll rebuild it
    free_list_head_ = end_of_free_list;

    for (std::size_t slab_idx = 0; slab_idx < slabs_.size(); ++slab_idx) {
        auto * slab = slabs_[slab_idx].get();
        if (not slab) {
            continue;
        }

        auto const base_idx = static_cast<naked_size_type>(
            slab_idx << log2_slots_per_slab_);

        // Process all slots: destroy alive values, rebuild free list
        for (naked_size_type slot_idx = 0; slot_idx < slots_per_slab_;
             ++slot_idx)
        {
            auto const idx = index_type(
                static_cast<naked_index_type>(slot_idx));

            if (slab->is_alive(idx)) {
                // destroy() clears alive bit, increments version (or marks
                // dead)
                bool const can_reuse = slab->destroy(idx);
                if (can_reuse) {
                    // Add to free list
                    auto const full_idx = static_cast<naked_index_type>(
                        base_idx + slot_idx);
                    slab->slot(idx).set_next(free_list_head_);
                    free_list_head_ = size_type(full_idx);
                }
            } else {
                // Slot was already in free list - check if it's still usable
                auto const ver = slab->slot(idx).version();
                if (ver.value < version_type::mask) {
                    // Add to new free list
                    auto const full_idx = static_cast<naked_index_type>(
                        base_idx + slot_idx);
                    slab->slot(idx).set_next(free_list_head_);
                    free_list_head_ = size_type(full_idx);
                }
                // Dead slots (version == max) are not added
            }
        }
    }

    size_ = 0;
}

template <typename KeyT>
void
SlotMap<KeyT>::
reset()
{
    clear_slabs();
}

template <typename KeyT>
void
SlotMap<KeyT>::
reserve(size_type n)
{
    // Calculate how many total slots we need
    // Already have: next_slab_base_index_ slots allocated (across all slabs)
    // Plus free slots in partially filled slabs
    while (size_type(next_slab_base_index_) < n) {
        if (not allocate_new_slab()) {
            // Index space exhausted - can't allocate more
            break;
        }
    }
}

template <typename KeyT>
void
SlotMap<KeyT>::
try_recycle_slab(std::size_t slab_idx)
{
    auto * slab = slabs_[slab_idx].get();
    if (not slab || not slab->can_be_recycled()) {
        return;
    }

    // Check if there's room for more slabs in the index space
    auto const new_base = next_slab_base_index_;
    if (size_type(new_base) + size_type(slots_per_slab_) > end_of_free_list) {
        // No room for recycling - just delete the slab
        slabs_[slab_idx].reset();
        return;
    }

    // Calculate where the recycled slab will go
    auto const new_slab_idx = static_cast<std::size_t>(
        new_base >> log2_slots_per_slab_);

    // Ensure vector is large enough
    if (new_slab_idx >= slabs_.size()) {
        slabs_.resize(new_slab_idx + 1);
    }

    // Recycle the slab to the new position
    auto const first_index = index_type(
        static_cast<naked_index_type>(new_base));
    slab->recycle(first_index, free_list_head_);

    // Move slab pointer to new position
    if (new_slab_idx != slab_idx) {
        slabs_[new_slab_idx] = std::move(slabs_[slab_idx]);
    }

    // Update bookkeeping
    free_list_head_ = size_type(new_base);
    next_slab_base_index_ += slots_per_slab_;
}

} // namespace wjh::slotmap

#endif // WJH_SLOTMAP_FC1C5D6C7D5C44F8AA067828C821C8DF
