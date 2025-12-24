// ----------------------------------------------------------------------
// Copyright 2025 Jody Hagins
// Distributed under the MIT Software License
// See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT
// ----------------------------------------------------------------------
#ifndef WJH_SLOTMAP_E328214D1BC040C6BCA7583610D8EDEF
#define WJH_SLOTMAP_E328214D1BC040C6BCA7583610D8EDEF

#include "Key.hpp"
#include "types.hpp"

#include "detail/Slot.hpp"
#include "detail/SlotMap.hpp"

namespace wjh::slotmap {
WJH_SLOTMAP_NAMESPACE_BEGIN

/**
 * Traits for SlotMap configuration.
 *
 * @tparam KeyT The key type (must satisfy KeyC concept which is an
 * instantiation of the Key class template.
 *
 * @tparam nslots The slots per slab configuration
 *
 * @tparam alive_bit Whether to use an available live bit to speed up lookup.
 *
 * @tparam default_user Default value for user bits in newly created keys.
 */
template <
    KeyC KeyT,
    SlotsPerSlab nslots,
    UseAliveBitForLookup alive_bit,
    DefaultUserBits default_user = DefaultUserBits{0}>
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
    using naked_user_type = typename user_type::value_type;
    using naked_size_type = typename storage_policy::naked_size_type;

    static constexpr auto slots_per_slab = nslots;
    static constexpr bool allow_alive_bit = bool(alive_bit);
    static constexpr bool use_alive_bit_for_lookup =
        detail::has_alive_bit<Traits>();

    /// Default value for user bits in keys returned by emplace/try_emplace.
    /// The value is masked to fit within the configured user bits.
    static constexpr naked_user_type default_user_bits =
        static_cast<naked_user_type>(
            static_cast<std::size_t>(default_user) & user_type::mask);
};

/**
 * A concept for any instantiation of the class template Traits, or anything
 * derived from an instantiation of Traits.
 */
template <typename T>
concept TraitsC = detail::TraitsC<T>;

WJH_SLOTMAP_NAMESPACE_END
} // namespace wjh::slotmap

#endif // WJH_SLOTMAP_E328214D1BC040C6BCA7583610D8EDEF
