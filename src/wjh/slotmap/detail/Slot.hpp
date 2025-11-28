// ----------------------------------------------------------------------
// Copyright 2025 Jody Hagins
// Distributed under the MIT Software License
// See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT
// ----------------------------------------------------------------------
#ifndef WJH_SLOTMAP_210A58D772C142B58C2FDB99480EA93F
#define WJH_SLOTMAP_210A58D772C142B58C2FDB99480EA93F

#include <array>
#include <cstddef>
#include <cstring>
#include <new>
#include <type_traits>
#include <utility>

namespace wjh::slotmap::detail {

/**
 * A slot in the slot map, containing either a value (when alive) or
 * a free-list link (when free).
 *
 * @tparam T The value type stored in the slot
 * @tparam IndexT The index type for free-list linking
 * @tparam VersionT The version type for ABA protection
 */
template <typename T, typename IndexT, typename VersionT>
class Slot
{
public:
    using value_type = T;
    using index_type = IndexT;
    using version_type = VersionT;

    /**
     * Default constructor is trivial, and DOES NOTHING.
     *
     * Value-initialization (i.e., Slot() or Slot{}) will zero-initialize.
     */
    constexpr Slot() = default;

    /**
     * Construct, setting the next index in the free list.
     *
     * @post  This instance will be FREE, with a version of 0 and the linking
     * index to the next free node in the list will be @p next.
     */
    explicit Slot(index_type next)
    : version_bytes_{}
    {
        set_next(next);
    }

    // Non-copyable, non-movable (managed by Slab)
    Slot(Slot const &) = delete;
    Slot & operator = (Slot const &) = delete;
    Slot(Slot &&) = delete;
    Slot & operator = (Slot &&) = delete;

    ~Slot() = default;

    // ========================================================================
    // Version access (always valid)
    // ========================================================================

    [[nodiscard]]
    version_type version() const noexcept
    {
        version_type v;
        std::memcpy(&v, version_bytes_.data(), sizeof(version_type));
        return v;
    }

    void set_version(version_type v) noexcept
    {
        std::memcpy(version_bytes_.data(), &v, sizeof(version_type));
    }

    // ========================================================================
    // Free-list access (only valid when FREE)
    // ========================================================================

    [[nodiscard]]
    index_type next() const noexcept
    {
        return *get<index_type>();
    }

    void set_next(index_type i) noexcept { *get<index_type>() = i; }

    // ========================================================================
    // Value access (only valid when ALIVE)
    // ========================================================================

    /**
     * Construct a value in-place
     *
     * @pre Slot must be in FREE state
     * @post Slot is in ALIVE state
     */
    template <typename... Args>
    T & emplace(Args &&... args)
    {
        return *::new (static_cast<void *>(storage_.data()))
            T(std::forward<Args>(args)...);
    }

    /**
     * Destroy the stored value
     *
     * @pre Slot must be in ALIVE state
     * @post Slot is in FREE state (next field may contain garbage)
     */
    void destroy() noexcept(std::is_nothrow_destructible_v<T>)
    {
        get<T>()->~T();
    }

    /**
     * Access the stored value
     *
     * @pre Slot must be in ALIVE state
     */
    [[nodiscard]]
    T & value() noexcept
    {
        return *get<T>();
    }

    [[nodiscard]]
    T const & value() const noexcept
    {
        return *get<T>();
    }

private:
    static constexpr std::size_t storage_alignment = std::max(
        alignof(index_type),
        alignof(T));
    static constexpr std::size_t storage_size = std::max(
        sizeof(index_type),
        sizeof(T));
    alignas(storage_alignment) std::array<std::byte, storage_size> storage_;

    // Version stored as byte array to avoid padding issues between
    // the union and the version field
    std::array<std::byte, sizeof(version_type)> version_bytes_;

    template <typename U>
    U * get()
    {
        return std::launder(reinterpret_cast<U *>(storage_.data()));
    }

    template <typename U>
    U const * get() const
    {
        return std::launder(reinterpret_cast<U const *>(storage_.data()));
    }
};

} // namespace wjh::slotmap::detail

#endif // WJH_SLOTMAP_210A58D772C142B58C2FDB99480EA93F
