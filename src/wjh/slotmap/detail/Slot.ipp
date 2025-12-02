// ----------------------------------------------------------------------
// Copyright 2025 Jody Hagins
// Distributed under the MIT Software License
// See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT
// ----------------------------------------------------------------------
#ifndef WJH_SLOTMAP_C6ADB3C3DF9A4401A9030688B53417C0
#define WJH_SLOTMAP_C6ADB3C3DF9A4401A9030688B53417C0

namespace wjh::slotmap::detail {

template <typename TraitsT>
constexpr Slot<TraitsT>::
Slot(index_type next)
: version_bytes_{}
{
    set_free();
    set_next(next);
}

template <typename TraitsT>
constexpr typename Slot<TraitsT>::version_type
Slot<TraitsT>::
version() const noexcept
{
    if constexpr (has_embedded_alive_bit) {
        return version_type(naked_version_type(
            std::bit_cast<naked_version_type>(version_bytes_) & ~alive_bit));
    } else {
        return std::bit_cast<version_type>(version_bytes_);
    }
}

template <typename TraitsT>
constexpr void
Slot<TraitsT>::
set_version(version_type v) noexcept
{
    if constexpr (has_embedded_alive_bit) {
        assert(not (v.value & alive_bit));
        auto const ver = std::bit_cast<naked_version_type>(version_bytes_);
        version_bytes_ = std::bit_cast<decltype(version_bytes_)>(
            naked_version_type(v.value | (ver & alive_bit)));
    } else {
        version_bytes_ = std::bit_cast<decltype(version_bytes_)>(v);
    }
}

template <typename TraitsT>
constexpr typename Slot<TraitsT>::index_type
Slot<TraitsT>::
next() const noexcept
{
    assert(is_free());
    return *get<index_type>();
}

template <typename TraitsT>
constexpr void
Slot<TraitsT>::
set_next(index_type i) noexcept
{
    assert(is_free());
    *get<index_type>() = i;
}

template <typename TraitsT>
template <typename... Args>
constexpr typename Slot<TraitsT>::value_type &
Slot<TraitsT>::
emplace(Args &&... args)
{
    assert(is_free());
    auto ptr = std::construct_at(
        std::bit_cast<value_type *>(storage_.data()),
        std::forward<Args>(args)...);
    set_alive();
    return *ptr;
}

template <typename TraitsT>
constexpr void
Slot<TraitsT>::
destroy() noexcept(std::is_nothrow_destructible_v<value_type>)
{
    assert(is_alive());
    std::destroy_at(get<value_type>());
    set_free();
}

template <typename TraitsT>
constexpr typename Slot<TraitsT>::value_type &
Slot<TraitsT>::
value() noexcept
{
    assert(is_alive());
    return *get<value_type>();
}

template <typename TraitsT>
constexpr typename Slot<TraitsT>::value_type const &
Slot<TraitsT>::
value() const noexcept
{
    assert(is_alive());
    return *get<value_type>();
}

template <typename TraitsT>
template <typename U>
constexpr U *
Slot<TraitsT>::
get()
{
    return std::launder(reinterpret_cast<U *>(storage_.data()));
}

template <typename TraitsT>
template <typename U>
constexpr U const *
Slot<TraitsT>::
get() const
{
    return std::launder(reinterpret_cast<U const *>(storage_.data()));
}

template <typename TraitsT>
constexpr void
Slot<TraitsT>::
set_free()
{
    if constexpr (has_embedded_alive_bit) {
        auto version = std::bit_cast<naked_version_type>(version_bytes_);
        version &= ~alive_bit;
        version_bytes_ = std::bit_cast<decltype(version_bytes_)>(version);
    }
}

template <typename TraitsT>
constexpr bool
Slot<TraitsT>::
is_free() const
{
    if constexpr (has_embedded_alive_bit) {
        auto version = std::bit_cast<naked_version_type>(version_bytes_);
        return not (version & alive_bit);
    }
    return true;
}

template <typename TraitsT>
constexpr void
Slot<TraitsT>::
set_alive()
{
    if constexpr (has_embedded_alive_bit) {
        auto version = std::bit_cast<naked_version_type>(version_bytes_);
        version |= alive_bit;
        version_bytes_ = std::bit_cast<decltype(version_bytes_)>(version);
    }
}

template <typename TraitsT>
constexpr bool
Slot<TraitsT>::
is_alive() const noexcept
{
    if constexpr (has_embedded_alive_bit) {
        auto version = std::bit_cast<naked_version_type>(version_bytes_);
        return version & alive_bit;
    }
    return true;
}

template <typename TraitsT>
constexpr typename Slot<TraitsT>::naked_version_type
Slot<TraitsT>::
version_with_alive_bit() const noexcept
{
    return std::bit_cast<naked_version_type>(version_bytes_);
}

} // namespace wjh::slotmap::detail

#endif // WJH_SLOTMAP_C6ADB3C3DF9A4401A9030688B53417C0
