// ----------------------------------------------------------------------
// Copyright 2025 Jody Hagins
// Distributed under the MIT Software License
// See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT
// ----------------------------------------------------------------------
#ifndef WJH_SLOTMAP_FC1C5D6C7D5C44F8AA067828C821C8DF
#define WJH_SLOTMAP_FC1C5D6C7D5C44F8AA067828C821C8DF

namespace wjh::slotmap {

template <typename TraitsT>
SlotMap<TraitsT>::
SlotMap()
requires traits_type::is_single_slab
: slots_per_slab_{end_of_free_list}
{ }

template <typename TraitsT>
SlotMap<TraitsT>::
SlotMap()
requires(not traits_type::is_single_slab)
: SlotMap(compute_default_slab_size())
{ }

template <typename TraitsT>
SlotMap<TraitsT>::
SlotMap(size_type slots_per_slab)
requires(not traits_type::is_single_slab)
: traits_type(slots_per_slab)
, slots_per_slab_{slots_per_slab}
{
    validate_slab_size(slots_per_slab);
}

template <typename TraitsT>
SlotMap<TraitsT>::
SlotMap(SlotMap const & other)
requires std::is_copy_constructible_v<mapped_type>
: traits_type{static_cast<traits_type const &>(other)}
, free_list_head_{other.free_list_head_}
, size_{other.size_}
, dead_slots_{other.dead_slots_}
, objects_created_{other.objects_created_}
, slots_per_slab_{other.slots_per_slab_}
, next_slab_base_index_{other.next_slab_base_index_}
{
    this->storage_clone_from(static_cast<traits_type const &>(other));
}

template <typename TraitsT>
SlotMap<TraitsT> &
SlotMap<TraitsT>::
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

template <typename TraitsT>
SlotMap<TraitsT>::
SlotMap(SlotMap && other) noexcept
: traits_type{std::move(other)}
, free_list_head_{other.free_list_head_}
, size_{other.size_}
, dead_slots_{other.dead_slots_}
, objects_created_{other.objects_created_}
, slots_per_slab_{other.slots_per_slab_}
, next_slab_base_index_{other.next_slab_base_index_}
{
    other.free_list_head_ = end_of_free_list;
    other.size_ = 0;
    other.dead_slots_ = 0;
    other.objects_created_ = 0;
    other.next_slab_base_index_ = 0;
}

template <typename TraitsT>
SlotMap<TraitsT> &
SlotMap<TraitsT>::
operator = (SlotMap && other) noexcept
{
    if (this != &other) {
        traits_type::operator = (std::move(other));

        free_list_head_ = other.free_list_head_;
        size_ = other.size_;
        dead_slots_ = other.dead_slots_;
        objects_created_ = other.objects_created_;
        slots_per_slab_ = other.slots_per_slab_;
        next_slab_base_index_ = other.next_slab_base_index_;

        other.free_list_head_ = end_of_free_list;
        other.size_ = 0;
        other.dead_slots_ = 0;
        other.objects_created_ = 0;
        other.next_slab_base_index_ = 0;
    }
    return *this;
}

template <typename TraitsT>
bool
SlotMap<TraitsT>::
is_empty() const noexcept
{
    return size_ == 0;
}

template <typename TraitsT>
SlotMap<TraitsT>::size_type
SlotMap<TraitsT>::
size() const noexcept
{
    return size_type(size_);
}

template <typename TraitsT>
constexpr SlotMap<TraitsT>::size_type
SlotMap<TraitsT>::
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

template <typename TraitsT>
void
SlotMap<TraitsT>::
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

template <typename TraitsT>
bool
SlotMap<TraitsT>::
allocate_new_slab()
{
    // Check if we've exhausted the index space
    if (size_type(next_slab_base_index_) >= end_of_free_list) {
        return false;
    }

    // Calculate the slab index for multi-slab storage
    auto const new_slab_idx = this->storage_slab_index(
        index_type(static_cast<naked_index_type>(next_slab_base_index_)));

    // Get storage slot for the new slab (may resize vector for multi-slab)
    auto * storage_slot = this->storage_allocate_slot(new_slab_idx);
    if (not storage_slot) {
        return false; // Already allocated (single slab) or allocation failed
    }

    // Create the slab
    auto slab = slab_type::create(size_type(slots_per_slab_));

    // Initialize the free list within the slab
    initialize_slab_free_list(
        slab.get(),
        index_type(static_cast<naked_index_type>(next_slab_base_index_)));

    // First slab, slot 0 gets version 1 to avoid null key
    if (new_slab_idx == 0) {
        using naked_version_type = typename version_type::value_type;
        slab->slot(index_type(naked_index_type{0}))
            .set_version(version_type{naked_version_type{1}});
    }

    *storage_slot = std::move(slab);
    next_slab_base_index_ += slots_per_slab_;

    return true;
}

template <typename TraitsT>
void
SlotMap<TraitsT>::
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

template <typename TraitsT>
template <typename... Args>
SlotMap<TraitsT>::key_type
SlotMap<TraitsT>::
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
    auto * slab = this->storage_get_slab(idx);
    assert(slab);

    auto const slot_idx = this->storage_slot_index(idx);

    // Emplace the value - this returns the version and sets alive bit
    // Strong exception guarantee: if this throws, we haven't modified state
    auto [ver, next] = slab->emplace(slot_idx, std::forward<Args>(args)...);

    // Update free list head (only after successful emplace)
    free_list_head_ = next;
    ++size_;
    ++objects_created_;

    // TODO: allow user to set a default-user-type-value that gets used here.
    return key_type(idx, ver, user_type{});
}

template <typename TraitsT>
template <typename... Args>
SlotMap<TraitsT>::key_type
SlotMap<TraitsT>::
emplace(Args &&... args)
{
    auto key = try_emplace(std::forward<Args>(args)...);
    if (key.is_null()) {
        throw std::length_error(
            "SlotMap: capacity exhausted, cannot emplace new element");
    }
    return key;
}

template <typename TraitsT>
bool
SlotMap<TraitsT>::
erase(key_type key)
{
    auto const key_idx = key.index();
    if (auto * slab = this->storage_get_slab(key_idx)) {
        auto const slab_idx = this->storage_slab_index(key_idx);
        auto const slot_idx = this->storage_slot_index(key_idx);

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
                // Slot is dead - increment counter and check if slab can be
                // recycled
                ++dead_slots_;
                this->storage_try_recycle_slab(
                    slab_idx,
                    [this](slab_type *)
                        -> std::optional<
                            std::tuple<std::size_t, index_type, size_type>> {
                        // Check if there's room for more slabs in the index
                        // space
                        auto const new_base = next_slab_base_index_;
                        if (size_type(new_base) + size_type(slots_per_slab_) >
                            end_of_free_list)
                        {
                            return std::nullopt;
                        }

                        auto const first_index = index_type(
                            static_cast<naked_index_type>(new_base));
                        auto const new_idx = this->storage_slab_index(
                            first_index);

                        // Update next_slab_base_index_ and free_list_head_
                        next_slab_base_index_ += slots_per_slab_;
                        free_list_head_ = size_type(new_base);

                        return std::tuple{
                            new_idx,
                            first_index,
                            free_list_head_};
                    });
            }

            return true;
        }
    }
    return false;
}

template <typename TraitsT>
std::optional<typename SlotMap<TraitsT>::mapped_type>
SlotMap<TraitsT>::
pop(key_type key)
requires std::is_move_constructible_v<mapped_type>
{
    auto const key_idx = key.index();
    if (auto * slab = this->storage_get_slab(key_idx)) {
        auto const slab_idx = this->storage_slab_index(key_idx);
        auto const slot_idx = this->storage_slot_index(key_idx);

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
                // Slot is dead - increment counter and check if slab can be
                // recycled (no-op for single slab)
                ++dead_slots_;
                this->storage_try_recycle_slab(
                    slab_idx,
                    [this](slab_type *)
                        -> std::optional<
                            std::tuple<std::size_t, index_type, size_type>> {
                        auto const new_base = next_slab_base_index_;
                        if (size_type(new_base) + size_type(slots_per_slab_) >
                            end_of_free_list)
                        {
                            return std::nullopt;
                        }

                        auto const first_index = index_type(
                            static_cast<naked_index_type>(new_base));
                        auto const new_idx = this->storage_slab_index(
                            first_index);

                        next_slab_base_index_ += slots_per_slab_;
                        free_list_head_ = size_type(new_base);

                        return std::tuple{
                            new_idx,
                            first_index,
                            free_list_head_};
                    });
            }

            return result;
        }
    }
    return std::nullopt;
}

namespace detail {

template <typename F, typename KeyT, typename ValT, typename... OptTs>
void
invoke_use(F & func, [[maybe_unused]] KeyT key, ValT & val, OptTs &... opts)
{
    if constexpr (std::is_invocable_v<F &, KeyT, ValT &, OptTs &...>) {
        std::invoke(func, key, val, opts...);
    } else if constexpr (std::is_invocable_v<F &, KeyT, ValT &>) {
        std::invoke(func, key, val);
    } else if constexpr (std::is_invocable_v<F &, ValT &, OptTs &...>) {
        std::invoke(func, val, opts...);
    } else {
        std::invoke(func, val);
    }
}

template <typename SelfT, typename F, typename KeyT, typename ValT>
inline constexpr bool use_callback_wants_options =
    not std::is_const_v<std::remove_reference_t<SelfT>> &&
    (std::is_invocable_v<F, KeyT, ValT, Options &> ||
     std::is_invocable_v<F, ValT, Options &>);


} // namespace detail

namespace detail {

/**
 * Check if a slot is alive using the optimal method for the slot type.
 *
 * When the slot has an embedded alive bit (non-power-of-2 version bits),
 * read directly from the slot for cache locality. Otherwise, use the bitmap.
 *
 * This should only be used in cases where just the slot is accessed.
 */
template <typename SlotT, typename SlabT, typename IndexT>
[[nodiscard]]
constexpr bool
check_slot_alive_bit(
    SlotT const & slot,
    SlabT const * slab,
    IndexT index) noexcept
{
    if constexpr (SlotT::has_embedded_alive_bit) {
        return slot.is_alive();
    } else {
        return slab->is_alive(index);
    }
}

} // namespace detail

template <typename TraitsT>
bool
SlotMap<TraitsT>::
use(auto & self, key_type key, auto & func)
{
    using detail::use_callback_wants_options;
    using SelfT = std::remove_reference_t<decltype(self)>;
    using KeyT = key_type;
    using FuncT = decltype(func);
    auto const key_idx = key.index();
    if (auto * slab = self.storage_get_slab(key_idx)) {
        auto const slot_idx = self.storage_slot_index(key_idx);

        if (auto & slot = slab->slot(slot_idx);
            slot.version() == key.version() &&
            check_slot_alive_bit(slot, slab, slot_idx))
        {
            auto & value = slot.value();
            using ValT = decltype(value);

            if constexpr (use_callback_wants_options<SelfT, FuncT, KeyT, ValT>)
            {
                // Invoke the callable with Options support
                Options opts{};
                detail::invoke_use(func, key, value, opts);

                // Erase after callback if requested
                if (opts.erase) {
                    self.erase(key);
                }
            } else {
                detail::invoke_use(func, key, value);
            }

            return true;
        }
    }
    return false;
}

template <typename TraitsT>
template <typename F>
bool
SlotMap<TraitsT>::
use(key_type key, F && func)
{
    return use(*this, key, func);
}

template <typename TraitsT>
template <typename F>
bool
SlotMap<TraitsT>::
use(key_type key, F && func) const
{
    return use(*this, key, func);
}

template <typename TraitsT>
bool
SlotMap<TraitsT>::
contains(key_type key) const
{
    return use(key, [](mapped_type const &) {});
}

namespace detail {
template <typename F, typename KeyT, typename ValT>
auto
invoke_for_each(F & func, KeyT key, ValT & val, [[maybe_unused]] Options & opts)
{
    if constexpr (std::is_invocable_v<F &, KeyT, ValT &, Options &>) {
        return std::invoke(func, key, val, opts);
    } else if constexpr (std::is_invocable_v<F &, KeyT, ValT &>) {
        return std::invoke(func, key, val);
    } else if constexpr (std::is_invocable_v<F &, ValT &, Options &>) {
        return std::invoke(func, val, opts);
    } else {
        return std::invoke(func, val);
    }
}
} // namespace detail

template <typename TraitsT>
template <typename F>
SlotMap<TraitsT>::size_type
SlotMap<TraitsT>::
for_each(F && func)
{
    return for_each(*this, func);
}

template <typename TraitsT>
template <typename F>
SlotMap<TraitsT>::size_type
SlotMap<TraitsT>::
for_each(F && func) const
{
    return for_each(*this, func);
}

template <typename TraitsT>
SlotMap<TraitsT>::size_type
SlotMap<TraitsT>::
for_each(auto & self, auto & func)
{
    using SelfT = std::remove_reference_t<decltype(self)>;
    naked_size_type visited = 0;
    Options options{};

    // Use storage_for_each_slab to iterate over slabs
    self.storage_for_each_slab([&](slab_type * slab, std::size_t slab_idx) {
        if (options.stop) {
            return;
        }

        auto const base_idx = self.storage_base_index(slab_idx);

        // Use bitmap-scanning iteration for efficiency
        slab->for_each_alive([&](index_type slot_idx) -> bool {
            // Build the key from index + version
            auto const full_idx = index_type(
                static_cast<naked_index_type>(base_idx + slot_idx.value));
            auto const ver = slab->slot(slot_idx).version();
            auto const key = key_type(full_idx, ver, user_type{});
            auto & val = slab->slot(slot_idx).value();

            using R = decltype(
                detail::invoke_for_each(func, key, val, options));
            static_assert(
                std::is_void_v<R> || std::is_same_v<R, bool>,
                "for_each callback must return void or bool");

            if constexpr (std::is_void_v<R>) {
                detail::invoke_for_each(func, key, val, options);
            } else if (not detail::invoke_for_each(func, key, val, options)) {
                options.stop = true;
            }
            ++visited;

            if constexpr (std::is_const_v<SelfT>) {
                assert(not options.erase);
            } else if (options.erase) {
                self.erase(key);
                options.erase = false;
            }

            return not options.stop;
        });
    });

    return size_type(visited);
}

template <typename TraitsT>
void
SlotMap<TraitsT>::
swap(SlotMap & other) noexcept
{
    using std::swap;
    // Swap the storage policy (handles slabs_ and any policy-specific state)
    swap(static_cast<traits_type &>(*this), static_cast<traits_type &>(other));
    swap(free_list_head_, other.free_list_head_);
    swap(size_, other.size_);
    swap(dead_slots_, other.dead_slots_);
    swap(objects_created_, other.objects_created_);
    swap(slots_per_slab_, other.slots_per_slab_);
    swap(next_slab_base_index_, other.next_slab_base_index_);
}

template <typename TraitsT>
void
SlotMap<TraitsT>::
clear()
{
    // Start with empty free list - we'll rebuild it
    free_list_head_ = end_of_free_list;

    this->storage_for_each_slab([&](slab_type * slab, std::size_t slab_idx) {
        auto const base_idx = this->storage_base_index(slab_idx);

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
                } else {
                    // Slot became dead during clear
                    ++dead_slots_;
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
    });

    size_ = 0;
}

template <typename TraitsT>
void
SlotMap<TraitsT>::
reset()
{
    this->storage_clear();
    free_list_head_ = end_of_free_list;
    size_ = 0;
    dead_slots_ = 0;
    objects_created_ = 0;
    next_slab_base_index_ = 0;
}

template <typename TraitsT>
void
SlotMap<TraitsT>::
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

template <typename TraitsT>
SlotMap<TraitsT>::statistics_type
SlotMap<TraitsT>::
statistics() const noexcept
{
    statistics_type stats{};

    // Configuration
    stats.slots_per_slab = slots_per_slab_;
    stats.max_slots = end_of_free_list.value; // 2^IndexBits

    // max_objects = 2^IndexBits * 2^VersionBits - 1
    // The -1 is because slot 0 starts at version 1 to avoid null key
    constexpr auto max_version_count = std::size_t{1} << key_type::version_bits;
    constexpr auto max_index_count = std::size_t{1} << key_type::index_bits;
    stats.max_objects = max_index_count * max_version_count - 1;

    // Slot accounting
    stats.active_slots = size_;
    stats.dead_slots = dead_slots_;
    stats.allocated_slots = next_slab_base_index_;
    stats.free_slots = stats.allocated_slots - stats.active_slots -
        stats.dead_slots;
    stats.unallocated_slots = stats.max_slots - stats.allocated_slots;

    // Capacity metrics
    stats.available_slots = stats.free_slots;
    stats.remaining_slots = stats.max_slots - stats.dead_slots;

    // Object lifetime metrics
    stats.objects_created = objects_created_;
    stats.objects_remaining = stats.max_objects - objects_created_;

    // Slab metrics - use storage policy methods
    stats.slab_count = this->storage_slab_count();
    stats.slab_vector_size = this->storage_vector_size();

    // Memory metrics
    auto const bytes_per_slab = slab_type::total_bytes_needed(
        size_type(slots_per_slab_));
    stats.slab_memory_bytes = stats.slab_count * bytes_per_slab;
    stats.vector_memory_bytes = this->storage_vector_memory_bytes();
    stats.total_memory_bytes = stats.slab_memory_bytes +
        stats.vector_memory_bytes;

    // Derived metrics
    stats.slot_utilization = stats.remaining_slots > 0
        ? static_cast<double>(stats.active_slots) / stats.remaining_slots
        : 0.0;
    stats.dead_slot_ratio = stats.allocated_slots > 0
        ? static_cast<double>(stats.dead_slots) / stats.allocated_slots
        : 0.0;
    stats.lifetime_exhaustion = stats.max_objects > 0
        ? static_cast<double>(stats.objects_created) / stats.max_objects
        : 0.0;
    stats.bytes_per_object = stats.active_slots > 0
        ? static_cast<double>(stats.total_memory_bytes) / stats.active_slots
        : 0.0;

    return stats;
}

} // namespace wjh::slotmap

#endif // WJH_SLOTMAP_FC1C5D6C7D5C44F8AA067828C821C8DF
