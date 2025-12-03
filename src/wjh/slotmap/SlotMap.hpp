// ----------------------------------------------------------------------
// Copyright 2025 Jody Hagins
// Distributed under the MIT Software License
// See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT
// ----------------------------------------------------------------------
#ifndef WJH_SLOTMAP_E2D8A15AF47745D2A33C7CBE9DB11D95
#define WJH_SLOTMAP_E2D8A15AF47745D2A33C7CBE9DB11D95

#include "Key.hpp"
#include "types.hpp"

#include "detail/Slab.hpp"
#include "detail/SlotMap.hpp"

#include <bit>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace wjh::slotmap {

/**
 * Traits for SlotMap configuration.
 *
 * @tparam KeyT The key type (must satisfy KeyC concept)
 * @tparam nslots The slots per slab configuration
 * @tparam alive_bit Whether to use an available live bit to speed up lookup.
 */
template <KeyC KeyT, SlotsPerSlab nslots, UseAliveBitForLookup alive_bit>
struct Traits
: detail::storage_policy_t<KeyT, nslots, alive_bit>
{
protected:
    using storage_policy = detail::storage_policy_t<KeyT, nslots, alive_bit>;
    using storage_policy::storage_policy;

public:
    using key_type = KeyT;
    using mapped_type = typename key_type::tag_type;
    using index_type = typename key_type::index_type;
    using version_type = typename key_type::version_type;
    using user_type = typename key_type::user_type;

    using naked_size_type = typename storage_policy::naked_size_type;

    static constexpr auto slots_per_slab = nslots;
    static constexpr bool allow_alive_bit = bool(alive_bit);
    static constexpr bool use_alive_bit_for_lookup =
        detail::has_alive_bit<Traits>();
};

/**
 * A concept for any instantiation of the class template Traits, or anything
 * derived from an instantiation of Traits.
 */
template <typename T>
concept TraitsC = detail::TraitsC<T>;

/**
 * A high-performance slot map container with O(1) insertion, deletion,
 * and lookup using persistent unique keys.
 *
 * @tparam TraitsT  A set of traits with types and policies for this SlotMap
 * instantiation.
 */
template <typename TraitsT>
class BasicSlotMap
: detail::traits_t<TraitsT>
{
public:
    // ========================================================================
    // Type Aliases
    // ========================================================================

    using traits_type = detail::traits_t<TraitsT>;
    using key_type = typename traits_type::key_type;
    using mapped_type = typename key_type::tag_type;
    using index_type = typename key_type::index_type;
    using version_type = typename key_type::version_type;
    using user_type = typename key_type::user_type;
    using size_type = typename key_type::size_type;
    using slab_type = typename traits_type::slab_type;
    using slot_type = typename slab_type::slot_type;
    using statistics_type = Statistics;

    // ========================================================================
    // Constants
    // ========================================================================

    /**
     * Sentinel value marking end of free list.
     *
     * This value is one past the maximum valid index, which fits in size_type
     * (which has IndexBits + 1 bits) but cannot be a valid index_type.
     */
    static constexpr size_type end_of_free_list = ++size_type(index_type::mask);

    // ========================================================================
    // Constructors and Destructor
    // ========================================================================

    /**
     * Default constructor.
     *
     * Creates an empty SlotMap with a default slab size based on the
     * index space and value type size:
     * - If the entire index space fits in ~2MB, use a single slab
     * - Otherwise, use 4096 slots per slab (or adjusted to fit index space)
     *
     * @throws std::bad_alloc if initial slab allocation fails
     */
    BasicSlotMap()
    requires traits_type::is_single_slab;

    BasicSlotMap()
    requires(not traits_type::is_single_slab);

    /**
     * Construct with explicit slab size.
     *
     * Only available for multi-slab configurations where the slab size
     * can be customized. Single-slab configurations always use all slots.
     *
     * @param slots_per_slab Number of slots per slab (must be power of 2)
     * @throws std::invalid_argument if slots_per_slab is not a power of 2
     *         or exceeds the maximum index value
     * @throws std::bad_alloc if initial slab allocation fails
     */
    explicit BasicSlotMap(size_type slots_per_slab)
    requires(not traits_type::is_single_slab);

    /**
     * Copy constructor.
     *
     * Creates a deep copy with identical structure. All keys valid in the
     * source will be valid in the copy.
     *
     * @param other The SlotMap to copy from
     * @throws std::bad_alloc if allocation fails
     * @throws Any exception from T's copy constructor
     *
     * @note Only available if T is copy constructible
     */
    BasicSlotMap(BasicSlotMap const & other)
    requires std::is_copy_constructible_v<mapped_type>;

    /**
     * Copy assignment operator.
     *
     * Replaces contents with a deep copy of other using copy-and-swap.
     *
     * @param other The SlotMap to copy from
     * @return Reference to this
     * @throws std::bad_alloc if allocation fails
     * @throws Any exception from T's copy constructor
     *
     * @note Only available if T is copy constructible
     */
    BasicSlotMap & operator = (BasicSlotMap const & other)
    requires std::is_copy_constructible_v<mapped_type>;

    BasicSlotMap(BasicSlotMap && other) noexcept;
    BasicSlotMap & operator = (BasicSlotMap && other) noexcept;

    ~BasicSlotMap() = default;

    // ========================================================================
    // Element Access
    // ========================================================================

    /**
     * Access an element by key with callback.
     *
     * If key is valid and refers to an alive element, invokes func(value) and
     * returns the result. The callback can:
     * - Return void: use() returns bool (true if found, false otherwise)
     * - Return R: use() returns std::optional<R> (value if found, nullopt
     *             otherwise)
     *
     * Supported callback signatures:
     * - R (key_type, T &, Options &) [non-const only]
     * - R (key_type, T &)
     * - R (T &, Options &) [non-const only]
     * - R (T &)
     *
     * When Options is available (non-const SlotMap), you can:
     * - Set options.erase = true to erase the element after the callback
     *
     * @param key The key to look up
     * @param func Callable to invoke if key is valid
     * @return For void callbacks: bool (true if found)
     *         For non-void callbacks: std::optional<R> (result if found,
     * nullopt otherwise)
     *
     * @note The const overload does not support Options parameter.
     */
    template <typename F>
    [[nodiscard]]
    auto use(key_type key, F && func);

    template <typename F>
    [[nodiscard]]
    auto use(key_type key, F && func) const;

    /**
     * Check if a key refers to an alive element.
     *
     * @param key The key to check
     * @return true if key is valid and refers to an alive element
     */
    [[nodiscard]]
    bool contains(key_type key) const;

    // ========================================================================
    // Modifiers
    // ========================================================================

    /**
     * Construct a new element in-place.
     *
     * @param args Arguments to forward to T's constructor
     * @return A valid key for the new element (never null)
     * @throws std::length_error if no slots available (capacity exhausted)
     * @throws Any exception thrown by T's constructor (strong guarantee)
     */
    template <typename... Args>
    [[nodiscard]]
    key_type emplace(Args &&... args);

    /**
     * Try to construct a new element in-place.
     *
     * @param args Arguments to forward to T's constructor
     * @return A valid key for the new element, or null key if no slots
     * available
     * @throws Any exception thrown by T's constructor (strong guarantee)
     */
    template <typename... Args>
    [[nodiscard]]
    key_type try_emplace(Args &&... args);

    /**
     * Erase an element by key.
     *
     * If key is valid, destroys the element and frees or retires the slot.
     * May trigger slab recycling if the slot reaches max version and the
     * slab becomes exhausted.
     *
     * @param key The key of the element to erase
     * @return true if an element was erased, false otherwise
     */
    bool erase(key_type key);

    /**
     * Remove an element and return it.
     *
     * If key is valid, moves the element out, destroys the slot's value,
     * and frees or retires the slot.
     *
     * @param key The key of the element to remove
     * @return The moved element wrapped in optional, or nullopt if key invalid
     *
     * @note Only available if T is move constructible
     */
    [[nodiscard]]
    std::optional<mapped_type> pop(key_type key)
    requires std::is_move_constructible_v<mapped_type>;

    /**
     * Swap contents with another SlotMap.
     *
     * Exchanges the contents of this SlotMap with another.
     *
     * @param other The SlotMap to swap with
     */
    void swap(BasicSlotMap & other) noexcept;

    /**
     * Clear all elements from the container.
     *
     * Destroys all alive elements, increments all versions, and resets
     * the free list. Memory is retained for reuse.
     *
     * @post is_empty() == true, size() == 0
     * @note Keys that were valid before clear() are now invalid (version
     * mismatch)
     */
    void clear();

    /**
     * Reset the container to its initial state.
     *
     * Destroys all elements and deallocates all slabs, returning to a
     * freshly-constructed state.
     *
     * @post is_empty() == true, size() == 0
     */
    void reset();

    // ========================================================================
    // Iteration
    // ========================================================================

    /**
     * Iterate over all alive elements.
     *
     * Invokes the callable for each alive element. Supported signatures:
     * - void|bool (key_type, T &, Options &)
     * - void|bool (key_type, T &)
     * - void|bool (T &, Options &)
     * - void|bool (T &)
     *
     * @param func Callable to invoke for each element
     * @return Number of elements visited
     *
     * Early exit can be achieved in two ways:
     * - Set options.stop = true within the callback
     * - Return false from a bool-returning callback (return true to continue)
     *
     * @note The callback must return void or bool (compile-time enforced).
     * @note The const overload does not support Options; erase asserts.
     */
    template <typename F>
    size_type for_each(F && func);

    template <typename F>
    size_type for_each(F && func) const;

    // ========================================================================
    // Capacity
    // ========================================================================

    /**
     * Check if the container is empty.
     *
     * @return true if size() == 0
     */
    [[nodiscard]]
    bool is_empty() const noexcept;

    /**
     * Get the number of alive elements.
     *
     * @return Number of elements currently stored
     */
    [[nodiscard]]
    size_type size() const noexcept;

    /**
     * Pre-allocate enough slabs to hold at least n elements.
     *
     * Useful for avoiding allocations during hot paths.
     *
     * @param n Number of elements to reserve space for
     * @throws std::bad_alloc if allocation fails
     */
    void reserve(size_type n);

    /**
     * Get statistics about the current state of the SlotMap.
     *
     * Returns comprehensive metrics about slot usage, object lifetime,
     * memory consumption, and capacity. Useful for monitoring, debugging,
     * and capacity planning.
     *
     * Complexity: O(num_slabs) - iterates slab vector once.
     *
     * @return Statistics struct with current metrics
     */
    [[nodiscard]]
    statistics_type statistics() const noexcept;

    // ========================================================================
    // Implementation Details (private)
    // ========================================================================

private:
    using naked_size_type = typename size_type::value_type;
    using naked_index_type = typename index_type::value_type;

    static constexpr bool is_single_slab = traits_type::is_single_slab;

    size_type free_list_head_ = end_of_free_list;
    naked_size_type size_ = 0;
    naked_size_type dead_slots_ = 0;
    std::size_t objects_created_ = 0;
    naked_size_type slots_per_slab_;
    naked_size_type next_slab_base_index_ = 0;

    static constexpr size_type compute_default_slab_size() noexcept;
    static void validate_slab_size(size_type slots_per_slab);
    bool allocate_new_slab();
    void initialize_slab_free_list(slab_type * slab, index_type base);
    static size_type for_each(auto & self, auto & func);
    static auto use(auto & self, key_type key, auto & func);
};

/**
 * A possibly more convenient way to declare a SlotMap with its component
 * pieces, rather than a traits class.
 *
 * There are three basic forms.
 *
 * 1. SlotMap<TraitsC T> : Pass one type, which is Traits, or derived from
 *     Traits.
 *
 * 2. SlotMap<KeyC T, options...> : Pass one type, which is Key, followed by
 *     Traits options.
 *
 * 3. SlotMap<typename ValueT, bits_and_options...> : Pass one type, which is
 * the mapped type, followed by key bit values and Traits options.
 *
 * @note  The bits and options values can come in any order, and they will
 * evaluate to the same type. Unmentioned bits will get a value of 0.
 * Unmentioned SlotsPerSlab will get a value of SlotsPerSlab::Dynamic.
 * Unmentioned UseAliveBitForLookup will get a value of
 * UseAliveBitForLookup::Yes.
 *
 * For example, all of these yield the exact same type.
 *
 * SlotMap<
 *     Key<int, IndexBits(10), VersionBits(6), UserBits(0)>,
 *     SlotsPerSlab::All,
 *     UseAliveBitForLookup::Yes>
 *
 * SlotMap<
 *     Key<int, VersionBits(6), IndexBits(10)>,
 *     SlotsPerSlab::All>
 *
 * SlotMap<int, IndexBits(10), VersionBits(6), SlotsPerSlab::All>
 *
 * SlotMap<int, VersionBits(6), SlotsPerSlab::All, VersionBits(10)>
 */
template <typename T, auto... vs>
using SlotMap = BasicSlotMap<detail::helper_t<T, vs...>>;

} // namespace wjh::slotmap

namespace wjh {

using slotmap::SlotMap;

} // namespace wjh

#include "SlotMap.ipp"

#endif // WJH_SLOTMAP_E2D8A15AF47745D2A33C7CBE9DB11D95
