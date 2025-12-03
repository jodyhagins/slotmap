// ----------------------------------------------------------------------
// Copyright 2025 Jody Hagins
// Distributed under the MIT Software License
// See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT
// ----------------------------------------------------------------------
#ifndef WJH_SLOTMAP_E253A0A968334A30AADC240EA20ABEF2
#define WJH_SLOTMAP_E253A0A968334A30AADC240EA20ABEF2

#include <tuple>

namespace wjh::slotmap {

template <KeyC, SlotsPerSlab, UseAliveBitForLookup>
struct Traits;

namespace detail {

// Forward declaration
template <typename TraitsT>
class Slab;

/**
 * Determine if a SlotMap configuration should use single-slab storage.
 *
 * Single-slab storage is used when all possible slots can fit in one slab.
 * This is determined by whether SlotsPerSlab >= max_slots (2^IndexBits).
 */
template <typename KeyT, SlotsPerSlab sps>
struct use_single_slab_storage
{
    using index_type = typename KeyT::index_type;
    using size_type = typename KeyT::size_type;

    // max_slots is the total number of possible slots (2^IndexBits)
    static constexpr auto max_slots = ++size_type(index_type::mask);

    // If SlotsPerSlab::All or sps >= max_slots, use single slab
    static constexpr bool value = (sps == SlotsPerSlab::All) ||
        (sps != SlotsPerSlab::Dynamic &&
         static_cast<std::size_t>(sps) >= max_slots.value);
};

template <typename KeyT, SlotsPerSlab sps>
inline constexpr bool use_single_slab_storage_v =
    use_single_slab_storage<KeyT, sps>::value;

/**
 * Storage policy for single-slab SlotMaps.
 *
 * Uses a unique_ptr<Slab> instead of vector<unique_ptr<Slab>>, eliminating
 * vector indirection for small slot maps.
 */
template <typename KeyT, bool AllowAliveBit>
struct single_slab_storage_policy
{
    using mapped_type = typename KeyT::tag_type;
    using index_type = typename KeyT::index_type;
    using version_type = typename KeyT::version_type;
    using size_type = typename KeyT::size_type;
    using naked_size_type = typename size_type::value_type;
    using naked_index_type = typename index_type::value_type;
    using slab_traits = SlabTraits<
        mapped_type,
        index_type,
        version_type,
        size_type,
        AllowAliveBit>;
    using slab_type = Slab<slab_traits>;
    using slot_type = typename slab_type::slot_type;
    using storage_type = std::unique_ptr<slab_type>;

    static constexpr bool is_single_slab = true;

protected:
    storage_type slabs_{};

public:
    single_slab_storage_policy() = default;

protected:
    // Copy constructor doesn't copy slabs_ - use storage_clone_from() instead
    single_slab_storage_policy(single_slab_storage_policy const &) noexcept { }

    single_slab_storage_policy(single_slab_storage_policy &&) noexcept =
        default;
    single_slab_storage_policy & operator = (
        single_slab_storage_policy const &) = default;
    single_slab_storage_policy & operator = (
        single_slab_storage_policy &&) noexcept = default;
    ~single_slab_storage_policy() = default;

    // Direct slot access - no slab index calculation
    [[nodiscard]]
    slot_type const & storage_get_slot(index_type idx) const noexcept
    {
        assert(slabs_ && "storage_get_slot: null slab");
        assert(
            idx.value < (++size_type(index_type::mask)).value &&
            "storage_get_slot: index out of bounds");
        return slabs_->slot(idx);
    }

    [[nodiscard]]
    slot_type & storage_get_slot(index_type idx) noexcept
    {
        assert(slabs_ && "storage_get_slot: null slab");
        assert(
            idx.value < (++size_type(index_type::mask)).value &&
            "storage_get_slot: index out of bounds");
        return slabs_->slot(idx);
    }

    // Direct slab access
    [[nodiscard]]
    slab_type const * storage_get_slab(index_type) const noexcept
    {
        return slabs_.get();
    }

    [[nodiscard]]
    slab_type * storage_get_slab(index_type) noexcept
    {
        return slabs_.get();
    }

    void storage_clear() noexcept { slabs_.reset(); }

    // Returns the slab count (always 0 or 1 for single slab)
    [[nodiscard]]
    std::size_t storage_slab_count() const noexcept
    {
        return slabs_ ? 1 : 0;
    }

    // Returns the storage capacity (always 0 or 1 for single slab)
    [[nodiscard]]
    std::size_t storage_capacity() const noexcept
    {
        return slabs_ ? 1 : 0;
    }

    // Clone storage (for copy constructor)
    void storage_clone_from(single_slab_storage_policy const & other)
    requires std::is_copy_constructible_v<mapped_type>
    {
        if (other.slabs_) {
            slabs_ = other.slabs_->clone();
        }
    }

    // Allocate slab storage (returns slot in vector, or just checks if exists)
    // Returns pointer to allocated storage slot, nullptr if already allocated
    [[nodiscard]]
    std::unique_ptr<slab_type> * storage_allocate_slot(std::size_t)
    {
        if (slabs_) {
            return nullptr; // Already have the single slab
        }
        return &slabs_;
    }

    // Iterate over all slabs
    template <typename F>
    void storage_for_each_slab(F && func) const
    {
        if (slabs_) {
            func(slabs_.get(), std::size_t{0});
        }
    }

    template <typename F>
    void storage_for_each_slab(F && func)
    {
        if (slabs_) {
            func(slabs_.get(), std::size_t{0});
        }
    }

    // No slab recycling for single-slab storage
    void storage_try_recycle_slab(std::size_t, auto &&) noexcept
    {
        // Single-slab storage cannot recycle - slots are never dead enough
        // to warrant recycling when the entire index space is one slab
    }

    // Index decomposition methods - single slab always uses direct indexing
    [[nodiscard]]
    static constexpr std::size_t storage_slab_index(index_type) noexcept
    {
        return 0;
    }

    [[nodiscard]]
    static constexpr index_type storage_slot_index(index_type idx) noexcept
    {
        return idx;
    }

    [[nodiscard]]
    static constexpr naked_size_type storage_base_index(std::size_t) noexcept
    {
        return 0;
    }

    // Statistics helpers
    [[nodiscard]]
    std::size_t storage_vector_size() const noexcept
    {
        return slabs_ ? 1 : 0;
    }

    [[nodiscard]]
    static constexpr std::size_t storage_vector_memory_bytes() noexcept
    {
        return 0;
    }

    friend void swap(
        single_slab_storage_policy & a,
        single_slab_storage_policy & b) noexcept
    {
        using std::swap;
        swap(a.slabs_, b.slabs_);
    }
};

/**
 * Storage policy for multi-slab SlotMaps.
 *
 * Uses vector<unique_ptr<Slab>> to support growing index spaces.
 */
template <typename KeyT, bool AllowAliveBit>
struct multi_slab_storage_policy
{
    using mapped_type = typename KeyT::tag_type;
    using index_type = typename KeyT::index_type;
    using version_type = typename KeyT::version_type;
    using size_type = typename KeyT::size_type;
    using naked_size_type = typename size_type::value_type;
    using naked_index_type = typename index_type::value_type;
    using slab_traits = SlabTraits<
        mapped_type,
        index_type,
        version_type,
        size_type,
        AllowAliveBit>;
    using slab_type = Slab<slab_traits>;
    using slot_type = typename slab_type::slot_type;
    using storage_type = std::vector<std::unique_ptr<slab_type>>;

    static constexpr bool is_single_slab = false;

protected:
    storage_type slabs_{};
    unsigned log2_slots_per_slab_;
    naked_size_type slots_per_slab_mask_;

    explicit multi_slab_storage_policy(size_type slots_per_slab)
    : log2_slots_per_slab_{unsigned(std::countr_zero(slots_per_slab.value))}
    , slots_per_slab_mask_{naked_size_type(slots_per_slab.value - 1)}
    { }

    // Copy constructor doesn't copy slabs_ - use storage_clone_from() instead
    multi_slab_storage_policy(multi_slab_storage_policy const & other) noexcept
    : log2_slots_per_slab_{other.log2_slots_per_slab_}
    , slots_per_slab_mask_{other.slots_per_slab_mask_}
    { }

    multi_slab_storage_policy(multi_slab_storage_policy &&) noexcept = default;
    multi_slab_storage_policy & operator = (
        multi_slab_storage_policy &&) noexcept = default;
    ~multi_slab_storage_policy() = default;

    // Copy assignment needs to preserve our own log2/mask values
    multi_slab_storage_policy & operator = (multi_slab_storage_policy const &) =
        default;

    // Slot access with slab index calculation
    [[nodiscard]]
    slot_type const & storage_get_slot(index_type idx) const noexcept
    {
        auto const slab_idx = storage_slab_index(idx);
        auto const slot_idx = storage_slot_index(idx);
        assert(
            slab_idx < slabs_.size() &&
            "storage_get_slot: slab index out of bounds");
        assert(slabs_[slab_idx] && "storage_get_slot: null slab");
        return slabs_[slab_idx]->slot(slot_idx);
    }

    [[nodiscard]]
    slot_type & storage_get_slot(index_type idx) noexcept
    {
        auto const slab_idx = storage_slab_index(idx);
        auto const slot_idx = storage_slot_index(idx);
        assert(
            slab_idx < slabs_.size() &&
            "storage_get_slot: slab index out of bounds");
        assert(slabs_[slab_idx] && "storage_get_slot: null slab");
        return slabs_[slab_idx]->slot(slot_idx);
    }

    // Slab access with bounds checking
    [[nodiscard]]
    slab_type const * storage_get_slab(index_type idx) const noexcept
    {
        auto const slab_idx = storage_slab_index(idx);
        if (slab_idx >= slabs_.size()) {
            return nullptr;
        }
        return slabs_[slab_idx].get();
    }

    [[nodiscard]]
    slab_type * storage_get_slab(index_type idx) noexcept
    {
        auto const slab_idx = storage_slab_index(idx);
        if (slab_idx >= slabs_.size()) {
            return nullptr;
        }
        return slabs_[slab_idx].get();
    }

    void storage_clear() noexcept { slabs_.clear(); }

    // Returns the slab count
    [[nodiscard]]
    std::size_t storage_slab_count() const noexcept
    {
        std::size_t count = 0;
        for (auto const & slab_ptr : slabs_) {
            if (slab_ptr) {
                ++count;
            }
        }
        return count;
    }

    // Returns the storage capacity
    [[nodiscard]]
    std::size_t storage_capacity() const noexcept
    {
        return slabs_.capacity();
    }

    // Clone storage (for copy constructor)
    void storage_clone_from(multi_slab_storage_policy const & other)
    requires std::is_copy_constructible_v<mapped_type>
    {
        slabs_.reserve(other.slabs_.size());
        for (auto const & slab_ptr : other.slabs_) {
            if (slab_ptr) {
                slabs_.push_back(slab_ptr->clone());
            } else {
                slabs_.push_back(nullptr);
            }
        }
    }

    // Allocate slab storage slot
    // Returns pointer to storage slot for the new slab
    [[nodiscard]]
    std::unique_ptr<slab_type> * storage_allocate_slot(std::size_t slab_idx)
    {
        if (slab_idx >= slabs_.size()) {
            slabs_.resize(slab_idx + 1);
        }
        return &slabs_[slab_idx];
    }

    // Iterate over all slabs
    template <typename F>
    void storage_for_each_slab(F && func) const
    {
        for (std::size_t i = 0; i < slabs_.size(); ++i) {
            if (slabs_[i]) {
                func(slabs_[i].get(), i);
            }
        }
    }

    template <typename F>
    void storage_for_each_slab(F && func)
    {
        for (std::size_t i = 0; i < slabs_.size(); ++i) {
            if (slabs_[i]) {
                func(slabs_[i].get(), i);
            }
        }
    }

    // Slab recycling support
    template <typename RecyclerF>
    void storage_try_recycle_slab(std::size_t slab_idx, RecyclerF && recycler)
    {
        auto * slab = slabs_[slab_idx].get();
        if (not slab || not slab->can_be_recycled()) {
            return;
        }

        // Ask the recycler where to put this slab (returns new_slab_idx or
        // nullopt)
        auto result = recycler(slab);
        if (not result) {
            // No room for recycling - just delete the slab
            slabs_[slab_idx].reset();
            return;
        }

        auto [new_slab_idx, first_index, free_list_head] = *result;

        // Ensure vector is large enough
        if (new_slab_idx >= slabs_.size()) {
            slabs_.resize(new_slab_idx + 1);
        }

        // Recycle the slab
        slab->recycle(first_index, free_list_head);

        // Move slab pointer to new position
        if (new_slab_idx != slab_idx) {
            slabs_[new_slab_idx] = std::move(slabs_[slab_idx]);
        }
    }

    // Index decomposition methods - multi-slab requires bit manipulation
    [[nodiscard]]
    constexpr std::size_t storage_slab_index(index_type idx) const noexcept
    {
        return static_cast<std::size_t>(idx.value >> log2_slots_per_slab_);
    }

    [[nodiscard]]
    constexpr index_type storage_slot_index(index_type idx) const noexcept
    {
        return index_type(
            static_cast<naked_index_type>(idx.value & slots_per_slab_mask_));
    }

    [[nodiscard]]
    constexpr naked_size_type storage_base_index(
        std::size_t slab_idx) const noexcept
    {
        return static_cast<naked_size_type>(slab_idx << log2_slots_per_slab_);
    }

    // Statistics helpers
    [[nodiscard]]
    std::size_t storage_vector_size() const noexcept
    {
        return slabs_.size();
    }

    [[nodiscard]]
    std::size_t storage_vector_memory_bytes() const noexcept
    {
        return storage_capacity() * sizeof(std::unique_ptr<slab_type>);
    }

    friend void swap(
        multi_slab_storage_policy & a,
        multi_slab_storage_policy & b) noexcept
    {
        using std::swap;
        swap(a.slabs_, b.slabs_);
        swap(a.log2_slots_per_slab_, b.log2_slots_per_slab_);
        swap(a.slots_per_slab_mask_, b.slots_per_slab_mask_);
    }
};

/**
 * Select the appropriate storage policy based on SlotsPerSlab configuration.
 */
template <typename KeyT, SlotsPerSlab sps, UseAliveBitForLookup alive_bit>
using storage_policy_t = std::conditional_t<
    use_single_slab_storage_v<KeyT, sps>,
    single_slab_storage_policy<KeyT, bool(alive_bit)>,
    multi_slab_storage_policy<KeyT, bool(alive_bit)>>;


template <typename TraitsT>
struct traits;

template <typename TraitsT>
requires KeyC<typename TraitsT::key_type>
struct traits<TraitsT>
{
    using type = TraitsT;
};

template <typename T>
using traits_t = typename traits<T>::type;

inline constexpr auto locate = [](auto defalt, auto... vs) {
    if constexpr ((std::is_same_v<decltype(defalt), decltype(vs)> || ...)) {
        auto const tuple = std::make_tuple(vs...);
        return std::get<decltype(defalt)>(tuple);
    } else {
        return defalt;
    }
};

template <typename T, auto... vs>
inline constexpr std::true_type is_slotmap_traits(Traits<T, vs...> const *);
inline constexpr std::false_type is_slotmap_traits(void const *);
template <typename T>
concept TraitsC = decltype(is_slotmap_traits(static_cast<T *>(nullptr)))::value;

template <typename T, auto... vs>
struct helper
: helper<
      Key<T,
          locate(IndexBits(0), vs...),
          locate(VersionBits(0), vs...),
          locate(UserBits(0), vs...)>,
      vs...>
{ };

template <TraitsC T>
struct helper<T>
{
    using type = T;
};

template <KeyC KeyT, auto... vs>
struct helper<KeyT, vs...>
{
    using type = Traits<
        KeyT,
        locate(SlotsPerSlab::Dynamic, vs...),
        locate(UseAliveBitForLookup::Yes, vs...)>;
};

template <typename T, auto... vs>
using helper_t = typename helper<T, vs...>::type;

} // namespace detail
} // namespace wjh::slotmap

#endif // WJH_SLOTMAP_E253A0A968334A30AADC240EA20ABEF2
