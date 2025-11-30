// ----------------------------------------------------------------------
// Copyright 2025 Jody Hagins
// Distributed under the MIT Software License
// See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT
// ----------------------------------------------------------------------
#ifndef WJH_SLOTMAP_C6ADB3C3DF9A4401A9030688B53417C0
#define WJH_SLOTMAP_C6ADB3C3DF9A4401A9030688B53417C0

namespace wjh::slotmap::detail {

template <typename T, typename IndexT, typename VersionT>
constexpr Slot<T, IndexT, VersionT>::
Slot(index_type next)
: version_bytes_{}
{
    set_free();
    set_next(next);
}

template <typename T, typename IndexT, typename VersionT>
constexpr Slot<T, IndexT, VersionT>::version_type
Slot<T, IndexT, VersionT>::
version() const noexcept
{
    if constexpr (version_type::num_bits < version_digits) {
        return version_type(naked_version_type(
            std::bit_cast<naked_version_type>(version_bytes_) & ~alive_bit));
    } else {
        return std::bit_cast<version_type>(version_bytes_);
    }
}

template <typename T, typename IndexT, typename VersionT>
constexpr void
Slot<T, IndexT, VersionT>::
set_version(version_type v) noexcept
{
    if constexpr (version_type::num_bits < version_digits) {
        assert(not (v.value & alive_bit));
        auto const ver = std::bit_cast<naked_version_type>(version_bytes_);
        version_bytes_ = std::bit_cast<decltype(version_bytes_)>(
            naked_version_type(v.value | (ver & alive_bit)));
    } else {
        version_bytes_ = std::bit_cast<decltype(version_bytes_)>(v);
    }
}

template <typename T, typename IndexT, typename VersionT>
constexpr Slot<T, IndexT, VersionT>::index_type
Slot<T, IndexT, VersionT>::
next() const noexcept
{
    assert(is_free());
    return *get<index_type>();
}

template <typename T, typename IndexT, typename VersionT>
constexpr void
Slot<T, IndexT, VersionT>::
set_next(index_type i) noexcept
{
    assert(is_free());
    *get<index_type>() = i;
}

template <typename T, typename IndexT, typename VersionT>
template <typename... Args>
constexpr T &
Slot<T, IndexT, VersionT>::
emplace(Args &&... args)
{
    assert(is_free());
    auto ptr = std::construct_at(
        std::bit_cast<T *>(storage_.data()),
        std::forward<Args>(args)...);
    set_alive();
    return *ptr;
}

template <typename T, typename IndexT, typename VersionT>
constexpr void
Slot<T, IndexT, VersionT>::
destroy() noexcept(std::is_nothrow_destructible_v<T>)
{
    assert(is_alive());
    std::destroy_at(get<T>());
    set_free();
}

template <typename T, typename IndexT, typename VersionT>
constexpr T &
Slot<T, IndexT, VersionT>::
value() noexcept
{
    assert(is_alive());
    return *get<T>();
}

template <typename T, typename IndexT, typename VersionT>
constexpr T const &
Slot<T, IndexT, VersionT>::
value() const noexcept
{
    assert(is_alive());
    return *get<T>();
}

template <typename T, typename IndexT, typename VersionT>
template <typename U>
constexpr U *
Slot<T, IndexT, VersionT>::
get()
{
    return std::launder(reinterpret_cast<U *>(storage_.data()));
}

template <typename T, typename IndexT, typename VersionT>
template <typename U>
constexpr U const *
Slot<T, IndexT, VersionT>::
get() const
{
    return std::launder(reinterpret_cast<U const *>(storage_.data()));
}

template <typename T, typename IndexT, typename VersionT>
constexpr void
Slot<T, IndexT, VersionT>::
set_free()
{
    if constexpr (version_type::num_bits < version_digits) {
        auto version = std::bit_cast<naked_version_type>(version_bytes_);
        version &= ~alive_bit;
        version_bytes_ = std::bit_cast<decltype(version_bytes_)>(version);
    }
}

template <typename T, typename IndexT, typename VersionT>
constexpr bool
Slot<T, IndexT, VersionT>::
is_free() const
{
    if constexpr (version_type::num_bits < version_digits) {
        auto version = std::bit_cast<naked_version_type>(version_bytes_);
        return not (version & alive_bit);
    }
    return true;
}

template <typename T, typename IndexT, typename VersionT>
constexpr void
Slot<T, IndexT, VersionT>::
set_alive()
{
    if constexpr (version_type::num_bits < version_digits) {
        auto version = std::bit_cast<naked_version_type>(version_bytes_);
        version |= alive_bit;
        version_bytes_ = std::bit_cast<decltype(version_bytes_)>(version);
    }
}

template <typename T, typename IndexT, typename VersionT>
constexpr bool
Slot<T, IndexT, VersionT>::
is_alive() const
{
    if constexpr (version_type::num_bits < version_digits) {
        auto version = std::bit_cast<naked_version_type>(version_bytes_);
        return version & alive_bit;
    }
    return true;
}

} // namespace wjh::slotmap::detail

#endif // WJH_SLOTMAP_C6ADB3C3DF9A4401A9030688B53417C0
