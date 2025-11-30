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
#include <sstream>
#include <stdexcept>
#include <vector>

#include <iostream>

namespace wjh::slotmap {

/**
 * A high-performance slot map container with O(1) insertion, deletion,
 * and lookup using persistent unique keys.
 *
 * @tparam KeyT  an instance of the wjh::slotmap::Key class template.
 */
template <typename KeyT>
class SlotMap
{
    static_assert(is_key_v<KeyT>);

public:
    // ========================================================================
    // Type Aliases
    // ========================================================================

    using key_type = KeyT;
    using value_type = typename key_type::tag_type;
    using index_type = typename key_type::index_type;
    using version_type = typename key_type::version_type;
    using user_type = typename key_type::user_type;
    using size_type = typename key_type::size_type;

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

    // Non-copyable for now (Phase 5)
    SlotMap(SlotMap const &) = delete;
    SlotMap & operator = (SlotMap const &) = delete;

    SlotMap(SlotMap && other) noexcept;
    SlotMap & operator = (SlotMap && other) noexcept;

    ~SlotMap() = default;

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

    // ========================================================================
    // Implementation Details (private)
    // ========================================================================

private:
    using naked_size_type = typename size_type::value_type;
    using naked_index_type = typename index_type::value_type;
    using slab_type =
        detail::Slab<value_type, index_type, version_type, size_type>;
    using slot_type = typename slab_type::slot_type;

    std::vector<std::unique_ptr<slab_type>> slabs_{};
    size_type free_list_head_ = end_of_free_list;
    naked_size_type size_ = 0;
    naked_size_type slots_per_slab_;
    unsigned log2_slots_per_slab_ = 0;
    naked_index_type next_slab_base_index_ = 0;

    static constexpr size_type compute_default_slab_size() noexcept;
    static void validate_slab_size(size_type slots_per_slab);
    slot_type & get_slot(index_type idx) noexcept;
    slot_type const & get_slot(index_type idx) const noexcept;
    void clear_slabs() noexcept;
};

} // namespace wjh::slotmap

namespace wjh {

template <typename KeyT>
using SlotMap = slotmap::SlotMap<KeyT>;

} // namespace wjh

#include "SlotMap.ipp"

#endif // WJH_SLOTMAP_E2D8A15AF47745D2A33C7CBE9DB11D95
