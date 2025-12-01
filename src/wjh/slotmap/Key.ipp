// ----------------------------------------------------------------------
// Copyright 2025 Jody Hagins
// Distributed under the MIT Software License
// See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT
// ----------------------------------------------------------------------
#ifndef WJH_SLOTMAP_C42A399B62274EFC833F38C7B58A2F64
#define WJH_SLOTMAP_C42A399B62274EFC833F38C7B58A2F64

#include "detail.hpp"

#include <cassert>
#include <compare>
#include <cstddef>
#include <functional>

namespace wjh::slotmap {

template <typename T, unsigned I, unsigned V, unsigned U>
constexpr Key<T, I, V, U>::
Key(index_type index, version_type version, user_type user) noexcept
: Base(value_type(
    (index.value & index_mask) |
    safe_shift_left(version.value & version_mask, version_shift) |
    safe_shift_left(user.value & user_mask, user_shift)))
{ }

template <typename T, unsigned I, unsigned V, unsigned U>
constexpr Key<T, I, V, U>::
Key(index_type index, version_type version) noexcept
: Key(index, version, user_type(0))
{ }

template <typename T, unsigned I, unsigned V, unsigned U>
constexpr Key<T, I, V, U>
Key<T, I, V, U>::
null() noexcept
{
    return Key{};
}

template <typename T, unsigned I, unsigned V, unsigned U>
constexpr typename Key<T, I, V, U>::index_type
Key<T, I, V, U>::
index() const noexcept
{
    return index_type{naked_index_type(Base::bits_ & index_mask)};
}

template <typename T, unsigned I, unsigned V, unsigned U>
constexpr Key<T, I, V, U>::version_type
Key<T, I, V, U>::
version() const noexcept
{
    return version_type{naked_version_type(
        safe_shift_right(Base::bits_, version_shift) & version_mask)};
}

template <typename T, unsigned I, unsigned V, unsigned U>
constexpr Key<T, I, V, U>::user_type
Key<T, I, V, U>::
user() const noexcept
{
    return user_type{
        naked_user_type(safe_shift_right(Base::bits_, user_shift) & user_mask)};
}

template <typename T, unsigned I, unsigned V, unsigned U>
constexpr Key<T, I, V, U>::value_type
Key<T, I, V, U>::
to_underlying() const noexcept
{
    return Base::bits_;
}

template <typename T, unsigned I, unsigned V, unsigned U>
constexpr Key<T, I, V, U>
Key<T, I, V, U>::
with_user(user_type new_user) const noexcept
{
    // Clear existing user bits and set new ones
    auto const cleared = Base::bits_ & ~safe_shift_left(user_mask, user_shift);
    auto const updated = value_type(
        cleared | safe_shift_left(new_user.value & user_mask, user_shift));

    Key result;
    result.Base::bits_ = updated;
    return result;
}

template <typename T, unsigned I, unsigned V, unsigned U>
constexpr bool
Key<T, I, V, U>::
is_null() const noexcept
{
    return Base::bits_ == value_type{0};
}

template <typename T, unsigned I, unsigned V, unsigned U>
constexpr std::size_t
Key<T, I, V, U>::
hash() const noexcept
{
    return detail::hash_bits(Base::bits_);
}

template <typename T, unsigned I, unsigned V, unsigned U>
template <unsigned Bits>
constexpr Key<T, I, V, U>::value_type
Key<T, I, V, U>::
make_mask() noexcept
{
    if constexpr (Bits == 0) {
        return value_type{0};
    } else {
        return (value_type{1} << Bits) - value_type{1};
    }
}

template <typename T, unsigned I, unsigned V, unsigned U>
constexpr Key<T, I, V, U>::value_type
Key<T, I, V, U>::
safe_shift_left(value_type val, unsigned shift) noexcept
{
    if (shift >= num_bits) {
        return value_type{0};
    }
    return value_type(val << shift);
}

template <typename T, unsigned I, unsigned V, unsigned U>
constexpr Key<T, I, V, U>::value_type
Key<T, I, V, U>::
safe_shift_right(value_type val, unsigned shift) noexcept
{
    if (shift >= num_bits) {
        return value_type{0};
    }
    return value_type(val >> shift);
}

} // namespace wjh::slotmap

template <typename T, unsigned I, unsigned V, unsigned U>
std::size_t
std::
hash<wjh::slotmap::Key<T, I, V, U>>::
operator () (wjh::slotmap::Key<T, I, V, U> const & key) const noexcept
{
    return key.hash();
}

#endif // WJH_SLOTMAP_C42A399B62274EFC833F38C7B58A2F64
