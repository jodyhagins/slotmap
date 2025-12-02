// ----------------------------------------------------------------------
// Copyright 2025 Jody Hagins
// Distributed under the MIT Software License
// See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT
// ----------------------------------------------------------------------
#ifndef WJH_SLOTMAP_E2D8A15AF47745D2A33C7CBE9DB11D95
#define WJH_SLOTMAP_E2D8A15AF47745D2A33C7CBE9DB11D95

#include "Key.hpp"

#include "detail/Slab.hpp"

#include <bit>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace wjh::slotmap {

/**
 * Options for use() and for_each() operations.
 *
 * @var stop  Set true to stop iteration after this callback (for_each only).
 * @var erase Set true to erase the element after the callback returns.
 *            Ignored on const overloads (asserts in debug mode).
 */
struct Options
{
    bool stop = false;
    bool erase = false;
};

/**
 * Statistics about a SlotMap's current state.
 *
 * Terminology:
 * - Slot: A physical storage location identified by an index. Each slot can
 *     hold one object at a time, but many objects over its lifetime.
 * - Object: A value stored in a slot. A slot creates a new "object" each time
 *     it is reused (emplace after erase). The version field tracks this.
 *
 * All size values use std::size_t since IndexBits is limited to 63, ensuring
 * size_type (IndexBits + 1 bits) always fits in 64 bits.
 *
 * Complexity: O(num_slabs) to compute via statistics().
 */
struct Statistics
{
    static_assert(sizeof(std::size_t) >= sizeof(std::uint64_t));

    // ========================================================================
    // Configuration (immutable after construction of the SlotMap)
    // ========================================================================

    /// Configured slots per slab (power of 2)
    std::size_t slots_per_slab;

    /// Max simultaneous slots (2^IndexBits)
    std::size_t max_slots;

    /// Max objects ever creatable (2^IndexBits * 2^VersionBits - 1)
    /// The -1 is because slot 0 starts at version 1 to avoid the null key.
    std::size_t max_objects;

    // ========================================================================
    // Slot accounting
    // ========================================================================

    /// Number of slots currently holding alive objects
    std::size_t active_slots;

    // Number of slots on the free list, available for immediate use
    std::size_t free_slots;

    /// Number of exhausted slots (all versions have been used)
    std::size_t dead_slots;

    /// Total number of allocated slots (active + free + dead)
    std::size_t allocated_slots;

    /// Number of slots that have not yet been allocated
    std::size_t unallocated_slots;

    // ========================================================================
    // Capacity metrics
    // ========================================================================

    /// Slots usable without new slab (= free_slots)
    std::size_t available_slots;

    /// Max additional active possible (max - dead)
    std::size_t remaining_slots;

    // ========================================================================
    // Object lifetime metrics
    // ========================================================================

    /// Total objects created over lifetime
    std::size_t objects_created;

    /// Objects still creatable (max - created)
    std::size_t objects_remaining;

    // ========================================================================
    // Slab metrics
    // ========================================================================

    /// Active (non-null) slabs
    std::size_t slab_count;

    /// Slab vector size (includes nulls)
    std::size_t slab_vector_size;

    // ========================================================================
    // Memory metrics
    // ========================================================================

    /// Memory for all slabs
    std::size_t slab_memory_bytes;

    /// Memory for slab pointer vector
    std::size_t vector_memory_bytes;

    /// Total memory usage
    std::size_t total_memory_bytes;

    // ========================================================================
    // Derived metrics
    // ========================================================================

    /// Fraction in use: active / remaining (0 if remaining == 0)
    double slot_utilization;

    /// Fraction dead: dead / allocated (0 if allocated == 0)
    double dead_slot_ratio;

    /// Fraction exhausted: created / max (0 if max == 0)
    double lifetime_exhaustion;

    /// Average bytes: memory / active (0 if active == 0)
    double bytes_per_object;
};


/**
 * The number of slots to allocate per slab.
 *
 * There are several predefined values, but you can provide an explicit value,
 * e.g., SlotsPerSlab(1 * 1024 * 1024).
 *
 * The SlotMap will allocate memory in chunks, called slabs. Each slab contains
 * some metadata about the slab, 2^IndexBits slots, and a bitmask
 * (2^IndexBits)/8 bytes long.
 *
 * A slab is an internal implementation detail, but it could have a measurable
 * impact on performance, especially for smaller IndexBits values.
 *
 * For consideration, the size of each slot can be imagined as the size of
 * `union { size_type; mapped_type; }` plus `sizeof(version_type)`.
 *
 * @note  The maximum number of slots possible will be used if it is less than
 * SlotsPerSlab, which means all slots will be in a single slab.
 */
enum class SlotsPerSlab : std::size_t
{
    /**
     * The user provides the slots per slab to the SlotMap constructor. The
     * default constructor uses SlotsPerSlab::Default.
     */
    Dynamic = 0,

    /**
     * The default is used if no SlotsPerSlab is provided. It is also the value
     * used in the SlotMap default constructor when Dynamic is specified.
     */
    Default = 4096,

    /**
     * Put all slots into a single slab, which is allocated when the SlotMap is
     * default constructed.
     *
     * @note  Any value larger than 2^IndexBits will put all slots into the same
     * slab, this is just a convenient way of specifying it. Imagine a family of
     * SlotMap types.
     *
     * @code
     * template <KeyC KeyT>
     * using SingleSlotMap = wjh::slotmap::SlotMap<
     *     wjh::slotmap::Traits<KeyT, SlotsPerSlab::All>>;
     * @endcode
     */
    All = std::size_t(-1),
};

template <KeyC KeyT, SlotsPerSlab sps>
struct Traits
{
    using key_type = KeyT;

    static constexpr SlotsPerSlab slots_per_slab = sps;
};

namespace detail {
template <typename TraitsT>
struct traits;

template <typename TraitsT>
requires KeyC<typename TraitsT::key_type>
struct traits<TraitsT>
{
    using type = TraitsT;
};

template <typename T>
requires KeyC<T>
struct traits<T>
: traits<Traits<T, SlotsPerSlab::Dynamic>>
{ };

template <typename T>
using traits_t = typename traits<T>::type;
} // namespace detail

/**
 * A high-performance slot map container with O(1) insertion, deletion,
 * and lookup using persistent unique keys.
 *
 * @tparam TraitsT  A set of traits with types and policies for this SlotMap
 * instantiation.
 */
template <typename TraitsT>
class SlotMap
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
    SlotMap();

    /**
     * Construct with explicit slab size.
     *
     * @param slots_per_slab Number of slots per slab (must be power of 2)
     * @throws std::invalid_argument if slots_per_slab is not a power of 2
     *         or exceeds the maximum index value
     * @throws std::bad_alloc if initial slab allocation fails
     */
    explicit SlotMap(size_type slots_per_slab);

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
    SlotMap(SlotMap const & other)
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
    SlotMap & operator = (SlotMap const & other)
    requires std::is_copy_constructible_v<mapped_type>;

    SlotMap(SlotMap && other) noexcept;
    SlotMap & operator = (SlotMap && other) noexcept;

    ~SlotMap() = default;

    // ========================================================================
    // Element Access
    // ========================================================================

    /**
     * Access an element by key.
     *
     * If key is valid and refers to an alive element, invokes func(value).
     *
     * @param key The key to look up
     * @param func Callable with signature void(T&) or void(T const&)
     * @return true if element was found and func was called, false otherwise
     */
    template <typename F>
    bool use(key_type key, F && func);

    template <typename F>
    bool use(key_type key, F && func) const;

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
    void swap(SlotMap & other) noexcept;

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
    using slab_type =
        detail::Slab<mapped_type, index_type, version_type, size_type>;
    using slot_type = typename slab_type::slot_type;

    std::vector<std::unique_ptr<slab_type>> slabs_{};
    size_type free_list_head_ = end_of_free_list;
    naked_size_type size_ = 0;
    naked_size_type dead_slots_ = 0;
    std::size_t objects_created_ = 0;
    naked_size_type slots_per_slab_;
    unsigned log2_slots_per_slab_ = 0;
    naked_size_type next_slab_base_index_ = 0;

    static constexpr size_type compute_default_slab_size() noexcept;
    static void validate_slab_size(size_type slots_per_slab);
    slot_type & get_slot(index_type idx) noexcept;
    slot_type const & get_slot(index_type idx) const noexcept;
    slab_type * get_slab(index_type idx) noexcept;
    slab_type const * get_slab(index_type idx) const noexcept;
    void clear_slabs() noexcept;
    bool allocate_new_slab();
    void initialize_slab_free_list(slab_type * slab, index_type base);
    void try_recycle_slab(std::size_t slab_idx);
    static size_type for_each(auto & self, auto & func);
    static bool use(auto & self, key_type key, auto & func);
};

} // namespace wjh::slotmap

namespace wjh {

template <typename TraitsT>
using SlotMap = slotmap::SlotMap<TraitsT>;

} // namespace wjh

#include "SlotMap.ipp"

#endif // WJH_SLOTMAP_E2D8A15AF47745D2A33C7CBE9DB11D95
