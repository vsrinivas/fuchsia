// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIT_VARIANT_H_
#define LIB_FIT_VARIANT_H_

#include <assert.h>

#include <new>
#include <type_traits>
#include <utility>

namespace fit {
namespace internal {

// This is a very basic partial implementation of |std::variant| which
// is compatible with C++ 14.  In its current state, it only implements
// enough of the API for internal usage.  Transforming this into a more
// complete and precise implementation of |std::variant| is left as an
// exercise for the reader.  ;)
//
// Some key differences:
// - always requires first type to be monostate
// - always default constructible
// - no relational operators
// - no visitors
// - no find by type, only find by index
// - no exception support
// - simplified get/set methods

// Unit type.
struct monostate final {
    constexpr bool operator==(const monostate& other) const { return true; }
    constexpr bool operator!=(const monostate& other) const { return false; }
};

// Tag for requesting in-place initialization of a variant alternative by index.
template <size_t index>
struct in_place_index_t final {};

#ifdef __cpp_inline_variables

// Inline variables are only available on C++ 17 and beyond.
template <size_t index>
inline constexpr in_place_index_t<index> in_place_index{};

#else

// On C++ 14 we need to provide storage for the variable so we define
// |in_place_index| as a reference instead.
template <size_t index>
struct in_place_index_holder {
    static constexpr in_place_index_t<index> instance{};
};

template <size_t index>
constexpr in_place_index_t<index> in_place_index_holder<index>::instance;

template <size_t index>
static constexpr const in_place_index_t<index>& in_place_index =
    in_place_index_holder<index>::instance;

#endif // __cpp_inline_variables

// Stores the contents of the variant as a recursively nested union
// of alternatives.  Conceptually it might be simpler to use
// std::in_place_storage<> but we would lose the ability to treat the
// storage as a literal type.
template <typename... Ts>
union variant_storage final {
    static constexpr bool copy_construct_supported = true;
    static constexpr bool move_construct_supported = true;

    void copy_construct_from(size_t index, const variant_storage& other) {}
    void move_construct_from(size_t index, variant_storage&& other) {}
    void destroy(size_t index) {}
    void swap(size_t index, variant_storage& other) {}
};

template <typename T0, typename... Ts>
union variant_storage<T0, Ts...> final {
    static constexpr bool copy_construct_supported =
        std::is_copy_constructible<T0>::value &&
        variant_storage<Ts...>::copy_construct_supported;
    static constexpr bool move_construct_supported =
        std::is_move_constructible<T0>::value &&
        variant_storage<Ts...>::move_construct_supported;

    constexpr variant_storage() = default;

    template <typename... Args>
    explicit constexpr variant_storage(in_place_index_t<0>, Args&&... args)
        : alt(std::forward<Args>(args)...) {}

    template <size_t index, typename... Args>
    explicit constexpr variant_storage(in_place_index_t<index>, Args&&... args)
        : rest(in_place_index<index - 1>, std::forward<Args>(args)...) {}

    constexpr T0& get(in_place_index_t<0>) { return alt; }

    constexpr const T0& get(in_place_index_t<0>) const { return alt; }

    template <size_t index>
    constexpr auto& get(in_place_index_t<index>) {
        return rest.get(in_place_index<index - 1>);
    }

    template <size_t index>
    constexpr const auto& get(in_place_index_t<index>) const {
        return rest.get(in_place_index<index - 1>);
    }

    template <typename... Args>
    auto& emplace(in_place_index_t<0>, Args&&... args) {
        new (&alt) T0(std::forward<Args>(args)...);
        return alt;
    }

    template <size_t index, typename... Args>
    auto& emplace(in_place_index_t<index>, Args&&... args) {
        return rest.emplace(in_place_index<index - 1>,
                            std::forward<Args>(args)...);
    }

    void copy_construct_from(size_t index, const variant_storage& other) {
        if (index == 0) {
            new (&alt) T0(other.alt);
        } else {
            rest.copy_construct_from(index - 1, other.rest);
        }
    }

    void move_construct_from(size_t index, variant_storage&& other) {
        if (index == 0) {
            new (&alt) T0(std::move(other.alt));
        } else {
            rest.move_construct_from(index - 1, std::move(other.rest));
        }
    }

    void destroy(size_t index) {
        if (index == 0) {
            alt.~T0();
        } else {
            rest.destroy(index - 1);
        }
    }

    void swap(size_t index, variant_storage& other) {
        using std::swap;
        if (index == 0) {
            swap(alt, other.alt);
        } else {
            rest.swap(index - 1, other.rest);
        }
    }

    T0 alt;
    variant_storage<Ts...> rest;
};

// Holds the index and storage for a variant with a trivial destructor.
template <typename... Ts>
class variant_base_impl_trivial;
template <typename... Ts>
class variant_base_impl_trivial<monostate, Ts...> {
public:
    constexpr variant_base_impl_trivial()
        : index_(0),
          storage_(in_place_index<0>, monostate{}) {}

    template <size_t index, typename... Args>
    explicit constexpr variant_base_impl_trivial(
        in_place_index_t<index>, Args&&... args)
        : index_(index),
          storage_(in_place_index<index>, std::forward<Args>(args)...) {}

    // Used by emplace.
    void destroy() {}

protected:
    size_t index_;
    variant_storage<monostate, Ts...> storage_;
};

// Holds the index and storage for a variant with a non-trivial destructor.
template <typename... Ts>
class variant_base_impl_non_trivial;
template <typename... Ts>
class variant_base_impl_non_trivial<monostate, Ts...> {
public:
    constexpr variant_base_impl_non_trivial()
        : index_(0),
          storage_(in_place_index<0>, monostate{}) {}

    template <size_t index, typename... Args>
    explicit constexpr variant_base_impl_non_trivial(
        in_place_index_t<index>, Args&&... args)
        : index_(index),
          storage_(in_place_index<index>, std::forward<Args>(args)...) {}

    ~variant_base_impl_non_trivial() {
        destroy();
    }

    // Used by emplace and by the destructor.
    void destroy() {
        storage_.destroy(index_);
    }

protected:
    size_t index_;
    union {
        variant_storage<monostate, Ts...> storage_;
    };
};

// Selects an appropriate variant base class depending on whether
// its destructor is trivial or non-trivial.
template <typename... Ts>
using variant_base_impl =
    std::conditional_t<
        std::is_destructible<
            variant_base_impl_trivial<Ts...>>::value,
        variant_base_impl_trivial<Ts...>,
        variant_base_impl_non_trivial<Ts...>>;

// Implements non-trivial move-construction and move-assignment.
template <typename... Ts>
class variant_move_impl_non_trivial : protected variant_base_impl<Ts...> {
    using base = variant_base_impl<Ts...>;

public:
    using base::base;

    variant_move_impl_non_trivial(
        const variant_move_impl_non_trivial& other) = default;

    variant_move_impl_non_trivial(
        variant_move_impl_non_trivial&& other) {
        index_ = other.index_;
        storage_.move_construct_from(index_, std::move(other.storage_));
    }

    variant_move_impl_non_trivial& operator=(
        const variant_move_impl_non_trivial& other) = default;

    variant_move_impl_non_trivial& operator=(
        variant_move_impl_non_trivial&& other) {
        if (&other == this)
            return *this;
        storage_.destroy(index_);
        index_ = other.index_;
        storage_.move_construct_from(index_, std::move(other.storage_));
        return *this;
    }

protected:
    using base::index_;
    using base::storage_;
};

// Selects an appropriate variant base class for moving.
template <typename... Ts>
using variant_move_impl =
    std::conditional_t<
        (std::is_move_constructible<variant_base_impl<Ts...>>::value &&
         std::is_move_assignable<variant_base_impl<Ts...>>::value) ||
            !variant_storage<Ts...>::move_construct_supported,
        variant_base_impl<Ts...>,
        variant_move_impl_non_trivial<Ts...>>;

// Implements non-trivial copy-construction and copy-assignment.
template <typename... Ts>
class variant_copy_impl_non_trivial : protected variant_move_impl<Ts...> {
    using base = variant_move_impl<Ts...>;

public:
    using base::base;

    variant_copy_impl_non_trivial(
        const variant_copy_impl_non_trivial& other) {
        index_ = other.index_;
        storage_.copy_construct_from(index_, other.storage_);
    }

    variant_copy_impl_non_trivial(
        variant_copy_impl_non_trivial&&) = default;

    variant_copy_impl_non_trivial& operator=(
        const variant_copy_impl_non_trivial& other) {
        if (&other == this)
            return *this;
        storage_.destroy(index_);
        index_ = other.index_;
        storage_.copy_construct_from(index_, other.storage_);
        return *this;
    }

    variant_copy_impl_non_trivial& operator=(
        variant_copy_impl_non_trivial&&) = default;

protected:
    using base::index_;
    using base::storage_;
};

// Selects an appropriate variant base class for copying.
// Use the base impl if the type is trivially
template <typename... Ts>
using variant_copy_impl =
    std::conditional_t<
        (std::is_copy_constructible<variant_move_impl<Ts...>>::value &&
         std::is_copy_assignable<variant_move_impl<Ts...>>::value) ||
            !variant_storage<Ts...>::copy_construct_supported,
        variant_move_impl<Ts...>,
        variant_copy_impl_non_trivial<Ts...>>;

// Actual variant type.
template <typename... Ts>
class variant : private variant_copy_impl<Ts...> {
    using base = variant_copy_impl<Ts...>;

public:
    constexpr variant() = default;

    template <size_t index, typename... Args>
    explicit constexpr variant(in_place_index_t<index> i, Args&&... args)
        : base(i, std::forward<Args>(args)...) {}

    variant(const variant&) = default;
    variant(variant&&) = default;
    ~variant() = default;

    variant& operator=(const variant&) = default;
    variant& operator=(variant&&) = default;

    constexpr size_t index() const { return index_; }

    template <size_t index>
    constexpr auto& get() {
        assert(index_ == index);
        return storage_.get(in_place_index<index>);
    }

    template <size_t index>
    constexpr const auto& get() const {
        assert(index_ == index);
        return storage_.get(in_place_index<index>);
    }

    template <size_t index, typename... Args>
    auto& emplace(Args&&... args) {
        this->destroy();
        index_ = index;
        return storage_.emplace(in_place_index<index>,
                                std::forward<Args>(args)...);
    }

    void swap(variant& other) {
        using std::swap;
        if (&other == this)
            return;
        if (index_ == other.index_) {
            storage_.swap(index_, other.storage_);
        } else {
            variant temp(std::move(*this));
            *this = std::move(other);
            other = std::move(temp);
        }
    }

private:
    using base::index_;
    using base::storage_;
};

// Swaps variants.
template <typename... Ts>
void swap(variant<Ts...>& a, variant<Ts...>& b) {
    a.swap(b);
}

// Gets the type of a variant alternative with the given index.
template <size_t index, typename Variant>
struct variant_alternative;

template <size_t index, typename T0, typename... Ts>
struct variant_alternative<index, variant<T0, Ts...>>
    : variant_alternative<index - 1, variant<Ts...>> {};

template <typename T0, typename... Ts>
struct variant_alternative<0, variant<T0, Ts...>> {
    using type = T0;
};

template <size_t index, typename Variant>
using variant_alternative_t = typename variant_alternative<index, Variant>::type;

} // namespace internal
} // namespace fit

#endif // LIB_FIT_VARIANT_H_
