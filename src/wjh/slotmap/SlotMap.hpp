// ----------------------------------------------------------------------
// Copyright 2025 Jody Hagins
// Distributed under the MIT Software License
// See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT
// ----------------------------------------------------------------------
#ifndef WJH_SLOTMAP_E2D8A15AF47745D2A33C7CBE9DB11D95
#define WJH_SLOTMAP_E2D8A15AF47745D2A33C7CBE9DB11D95

#include "Key.hpp"
#include "Traits.hpp"
#include "abi.hpp"
#include "types.hpp"

#include "detail/Slab.hpp"
#include "detail/SlotMap.hpp"

#include <bit>
#include <concepts>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace wjh::slotmap {
WJH_SLOTMAP_NAMESPACE_BEGIN

// ============================================================================
// Callback Concepts
//
// These concepts provide better compile-time error messages when an invalid
// callback is passed to use() or for_each(). They check that the callback
// is invocable with at least one of the supported signatures.
// ============================================================================

/**
 * Concept for callbacks accepted by use() (non-const overload).
 *
 * Supported signatures (all may return void, bool, or other types):
 * - F(key_type, T&, Options&)
 * - F(key_type, T&)
 * - F(T&, Options&)
 * - F(T&)
 */
template <typename F, typename K, typename V>
concept UseCallbackC = std::invocable<F, K, V &, Options &> ||
    std::invocable<F, K, V &> || std::invocable<F, V &, Options &> ||
    std::invocable<F, V &>;

/**
 * Concept for callbacks accepted by use() (const overload).
 *
 * Supported signatures (all may return void, bool, or other types):
 * - F(key_type, T const&)
 * - F(T const&)
 *
 * @note Options parameter is not supported on const overloads because
 *       Options.erase would be meaningless.
 */
template <typename F, typename K, typename V>
concept ConstUseCallbackC = std::invocable<F, K, V const &> ||
    std::invocable<F, V const &>;

/**
 * Concept for callbacks accepted by for_each() (non-const overload).
 *
 * Same signatures as UseCallbackC. Return type must be void or bool.
 * - void: continue iteration
 * - bool: return false to stop, true to continue
 */
template <typename F, typename K, typename V>
concept ForEachCallbackC = UseCallbackC<F, K, V>;

/**
 * Concept for callbacks accepted by for_each() (const overload).
 *
 * Same signatures as ConstUseCallbackC. Return type must be void or bool.
 */
template <typename F, typename K, typename V>
concept ConstForEachCallbackC = ConstUseCallbackC<F, K, V>;

/**
 * A slot map container with O(1) insertion, deletion, and lookup using
 * persistent unique keys.
 *
 * Use cases are broad, but in general, it is most useful for managing objects
 * by identity. The classic example (for me anyway) is a matching engine or
 * order entry gateway. For most other people, it might be a game that contains
 * a bunch of entities or a class that manages a bunch of timers.
 *
 * The biggest downsides are that the keys are generated (you don't get to pick
 * them), and there is a fixed limit on the number of objects that can exist at
 * any single point in time.
 *
 * @tparam TraitsT  A set of traits with types and policies for this SlotMap
 * instantiation.
 */
template <TraitsC TraitsT>
class BasicSlotMap
: detail::traits_t<TraitsT>
{
public:
    // ========================================================================
    // Types and Type Aliases
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

    struct TotalSize
    : detail::TypeBase<key_type::index_bits + key_type::version_bits, TotalSize>
    {
        using detail::TypeBase<
            key_type::index_bits + key_type::version_bits,
            TotalSize>::TypeBase;
    };

    // ========================================================================
    // Constants
    // ========================================================================

    /**
     * The maximum number of objects that can exist in the map at any given
     * time.
     *
     * This is 2^IndexBits.
     *
     * For example, if there are 3 IndexBits, then there are eight valid index
     * values: 0b000, 0b001, 0b010, 0b011, 0b100, 0b101, 0b110, and 0b111.
     */
    static constexpr size_type max_slots = ++size_type(index_type::mask);
    static constexpr size_type max_simultaneous_objects = max_slots;

    /**
     * The maximum number of unique objects that can be inserted into the map
     * over its lifetime.
     *
     * This is 2^IndexBits * 2^VersionBits - 1.
     *
     * For example, if there are 3 IndexBits and 12 VersionBits, then there can
     * be eight unique index values (and only 8 in use at any given time). There
     * can be 2^VersionBits versions (0b000000000000 to 0b111111111111).
     * However, index 0b000 always starts with version 1 instead of version 0,
     * so it will never get version 0. Thus, 2^IndexBits * 2^VersionBits - 1, or
     * 2^(IndexBits + VersionBits) - 1.
     */
    static constexpr TotalSize max_total_objects =
        detail::max_total_objects<TotalSize, key_type>();

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
     *
     * @throws std::invalid_argument if slots_per_slab is not a power of 2
     *         or exceeds the maximum index value
     *
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
     *
     * @throws std::bad_alloc if allocation fails
     *
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
     *
     * @return Reference to this
     *
     * @throws std::bad_alloc if allocation fails
     *
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
     * If key is valid and refers to an alive element, invokes func(value).
     *
     * If @p func returns void, the return type of @p use will be bool, where
     * true indicates that the object was found, and @p func called.
     *
     * If @p func returns anything else, the return type of @p use will be
     * std::optional<R> where R is the return type of @p func. If @p key is
     * found, the optional will be truthy, with the result of having called @p
     * func. Otherwise, it will be std::nullopt.
     *
     * Supported callback signatures:
     * - R (key_type, T &, Options &)
     * - R (key_type, T &)
     * - R (T &, Options &)
     * - R (T &)
     *
     * When Options is used, you can:
     * - Set options.erase = true to erase the element after the callback
     *
     * @param key The key to look up
     *
     * @param func Callable to invoke if key is valid
     *
     * @return true/false for void callbacks, std::optional<R> otherwise.
     */
    template <typename F>
    [[nodiscard]]
    auto use(key_type key, F && func)
    requires UseCallbackC<F, key_type, mapped_type>;

    /**
     * Access an element by key with callback.
     *
     * If key is valid and refers to an alive element, invokes func(value).
     *
     * If @p func returns void, the return type of @p use will be bool, where
     * true indicates that the object was found, and @p func called.
     *
     * If @p func returns anything else, the return type of @p use will be
     * std::optional<R> where R is the return type of @p func. If @p key is
     * found, the optional will be truthy, with the result of having called @p
     * func. Otherwise, it will be std::nullopt.
     *
     * Supported callback signatures:
     * - R (key_type, T const &)
     * - R (T const &)
     *
     * @param key The key to look up
     *
     * @param func Callable to invoke if key is valid
     *
     * @return true/false for void callbacks, std::optional<R> otherwise.
     */
    template <typename F>
    [[nodiscard]]
    auto use(key_type key, F && func) const
    requires ConstUseCallbackC<F, key_type, mapped_type>;

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
     *
     * @return A valid key for the new element (never null)
     *
     * @throws std::length_error if no slots available (capacity exhausted)
     *
     * @throws Any exception thrown by T's constructor (strong guarantee)
     */
    template <typename... Args>
    [[nodiscard]]
    key_type emplace(Args &&... args);

    /**
     * Try to construct a new element in-place.
     *
     * @param args Arguments to forward to T's constructor
     *
     * @return A valid key for the new element, or null key if no slots
     * available
     *
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
     *
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
     *
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
     *
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
     *
     * @return Number of elements visited
     *
     * Early exit can be achieved in two ways:
     * - Set options.stop = true within the callback
     * - Return false from a bool-returning callback (return true to continue)
     *
     * @note The callback must return void or bool (compile-time enforced).
     */
    template <typename F>
    size_type for_each(F && func)
    requires ForEachCallbackC<F, key_type, mapped_type>;

    /**
     * Iterate over all alive elements.
     *
     * Invokes the callable for each alive element. Supported signatures:
     * - void|bool (key_type, T const &)
     * - void|bool (T const &)
     *
     * @param func Callable to invoke for each element
     *
     * @return Number of elements visited
     *
     * Early exit can be achieved in two ways:
     * - Set options.stop = true within the callback
     * - Return false from a bool-returning callback (return true to continue)
     *
     * @note The callback must return void or bool (compile-time enforced).
     *
     * @note Options.erase is not supported on const overloads.
     */
    template <typename F>
    size_type for_each(F && func) const
    requires ConstForEachCallbackC<F, key_type, mapped_type>;

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

private:
    using naked_size_type = typename size_type::value_type;
    using naked_index_type = typename index_type::value_type;

    static constexpr bool is_single_slab = traits_type::is_single_slab;
    static constexpr size_type end_of_free_list = max_slots;

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
    void handle_slot_removal(std::size_t, slot_type &, index_type, bool);
    static size_type for_each(auto & self, auto & func);
    static auto use(auto & self, key_type key, auto & func);

    friend void swap(BasicSlotMap & a, BasicSlotMap & b) noexcept { a.swap(b); }
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

WJH_SLOTMAP_NAMESPACE_END
} // namespace wjh::slotmap

namespace wjh {

using slotmap::SlotMap;

} // namespace wjh

#include "SlotMap.ipp"

#endif // WJH_SLOTMAP_E2D8A15AF47745D2A33C7CBE9DB11D95
