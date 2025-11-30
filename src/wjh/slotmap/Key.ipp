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

template <unsigned I, unsigned V, unsigned U, typename T>
constexpr Key<I, V, U, T>::
Key(index_type index, version_type version, user_type user) noexcept
: Base(value_type(
    (index.value & index_mask) |
    safe_shift_left(version.value & version_mask, version_shift) |
    safe_shift_left(user.value & user_mask, user_shift)))
{ }

template <unsigned I, unsigned V, unsigned U, typename T>
constexpr Key<I, V, U, T>::
Key(index_type index, version_type version) noexcept
: Key(index, version, user_type(0))
{ }

template <unsigned I, unsigned V, unsigned U, typename T>
constexpr Key<I, V, U, T>
Key<I, V, U, T>::
null() noexcept
{
    return Key{};
}

template <unsigned I, unsigned V, unsigned U, typename T>
constexpr typename Key<I, V, U, T>::index_type
Key<I, V, U, T>::
index() const noexcept
{
    return index_type{naked_index_type(Base::bits_ & index_mask)};
}

template <unsigned I, unsigned V, unsigned U, typename T>
constexpr Key<I, V, U, T>::version_type
Key<I, V, U, T>::
version() const noexcept
{
    return version_type{naked_version_type(
        safe_shift_right(Base::bits_, version_shift) & version_mask)};
}

template <unsigned I, unsigned V, unsigned U, typename T>
constexpr Key<I, V, U, T>::user_type
Key<I, V, U, T>::
user() const noexcept
{
    return user_type{
        naked_user_type(safe_shift_right(Base::bits_, user_shift) & user_mask)};
}

template <unsigned I, unsigned V, unsigned U, typename T>
constexpr Key<I, V, U, T>::value_type
Key<I, V, U, T>::
to_underlying() const noexcept
{
    return Base::bits_;
}

template <unsigned I, unsigned V, unsigned U, typename T>
constexpr Key<I, V, U, T>
Key<I, V, U, T>::
with_user(user_type new_user) const noexcept
{
    // Clear existing user bits and set new ones
    auto const cleared = Base::bits_ & ~safe_shift_left(user_mask, user_shift);
    auto const updated = cleared |
        safe_shift_left(new_user.value & user_mask, user_shift);

    Key result;
    result.Base::bits_ = updated;
    return result;
}

template <unsigned I, unsigned V, unsigned U, typename T>
constexpr bool
Key<I, V, U, T>::
is_null() const noexcept
{
    return Base::bits_ == value_type{0};
}

template <unsigned I, unsigned V, unsigned U, typename T>
constexpr std::size_t
Key<I, V, U, T>::
hash() const noexcept
{
    return detail::hash_bits(Base::bits_);
}

template <unsigned I, unsigned V, unsigned U, typename T>
template <unsigned Bits>
constexpr Key<I, V, U, T>::value_type
Key<I, V, U, T>::
make_mask() noexcept
{
    if constexpr (Bits == 0) {
        return value_type{0};
    } else {
        return (value_type{1} << Bits) - value_type{1};
    }
}

template <unsigned I, unsigned V, unsigned U, typename T>
constexpr Key<I, V, U, T>::value_type
Key<I, V, U, T>::
safe_shift_left(value_type val, unsigned shift) noexcept
{
    if (shift >= num_bits) {
        return value_type{0};
    }
    return val << shift;
}

template <unsigned I, unsigned V, unsigned U, typename T>
constexpr Key<I, V, U, T>::value_type
Key<I, V, U, T>::
safe_shift_right(value_type val, unsigned shift) noexcept
{
    if (shift >= num_bits) {
        return value_type{0};
    }
    return val >> shift;
}

} // namespace wjh::slotmap

template <unsigned I, unsigned V, unsigned U, typename T>
std::size_t
std::
hash<wjh::slotmap::Key<I, V, U, T>>::
operator () (wjh::slotmap::Key<I, V, U, T> const & key) const noexcept
{
    return key.hash();
}

#endif // WJH_SLOTMAP_C42A399B62274EFC833F38C7B58A2F64
