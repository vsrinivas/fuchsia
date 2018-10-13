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

template <size_t index>
inline constexpr in_place_index_t<index> in_place_index{};

// Stores the contents of the variant as a recursively nested union
// of alternatives.  Conceptually it might be simpler to use
// std::in_place_storage<> but we would lose the ability to treat the
// storage as a literal type.
template <typename... Ts>
union variant_storage final {
    void copy_construct_from(size_t index, const variant_storage& other) {}
    void move_construct_from(size_t index, variant_storage&& other) {}
    void destroy(size_t index) {}
    void swap(size_t index, variant_storage& other) {}
};

template <typename T0, typename... Ts>
union variant_storage<T0, Ts...> final {
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

// We'll select different implementations depending on whether we can
// construct a literal type from the alternatives.
template <bool literal, typename... Ts>
class variant_impl;

// This representation is designed for use when all alternatives are trivial
template <typename... Ts>
class variant_impl<true, monostate, Ts...> final {
public:
    constexpr variant_impl()
        : index_(0),
          storage_(in_place_index<0>, monostate{}) {}

    template <size_t index, typename... Args>
    explicit constexpr variant_impl(in_place_index_t<index>, Args&&... args)
        : index_(index),
          storage_(in_place_index<index>, std::forward<Args>(args)...) {}

    variant_impl(const variant_impl& other) = default;
    variant_impl(variant_impl&& other) = default;
    ~variant_impl() = default;

    variant_impl& operator=(const variant_impl& other) = default;
    variant_impl& operator=(variant_impl&& other) = default;

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
        index_ = index;
        return storage_.emplace(in_place_index<index>,
                                std::forward<Args>(args)...);
    }

    void swap(variant_impl& other) {
        using std::swap;
        if (&other == this)
            return;
        if (index_ == other.index_) {
            storage_.swap(index_, other.storage_);
        } else {
            variant_impl temp(std::move(*this));
            *this = std::move(other);
            other = std::move(temp);
        }
    }

private:
    size_t index_;
    variant_storage<monostate, Ts...> storage_;
};

// This representation handles the non-trivial cases.
template <typename... Ts>
class variant_impl<false, monostate, Ts...> final {
public:
    constexpr variant_impl()
        : index_(0),
          storage_(in_place_index<0>, monostate{}) {}

    template <size_t index, typename... Args>
    explicit constexpr variant_impl(in_place_index_t<index>, Args&&... args)
        : index_(index),
          storage_(in_place_index<index>, std::forward<Args>(args)...) {}

    variant_impl(const variant_impl& other)
        : index_(other.index_) {
        this->storage_.copy_construct_from(this->index_, other.storage_);
    }

    variant_impl(variant_impl&& other)
        : index_(other.index_) {
        this->index_ = other.index_;
        this->storage_.move_construct_from(this->index_, std::move(other.storage_));
    }

    ~variant_impl() {
        this->storage_.destroy(this->index_);
    }

    variant_impl& operator=(const variant_impl& other) {
        if (&other == this)
            return *this;
        this->storage_.destroy(this->index_);
        this->index_ = other.index_;
        this->storage_.copy_construct_from(this->index_, other.storage_);
        return *this;
    }

    variant_impl& operator=(variant_impl&& other) {
        if (&other == this)
            return *this;
        this->storage_.destroy(this->index_);
        this->index_ = other.index_;
        this->storage_.move_construct_from(this->index_, std::move(other.storage_));
        return *this;
    }

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
        this->storage_.destroy(this->index_);
        this->index_ = index;
        return this->storage_.emplace(in_place_index<index>,
                                      std::forward<Args>(args)...);
    }

    void swap(variant_impl& other) {
        using std::swap;
        if (&other == this)
            return;
        if (index_ == other.index_) {
            storage_.swap(index_, other.storage_);
        } else {
            variant_impl temp(std::move(*this));
            *this = std::move(other);
            other = std::move(temp);
        }
    }

private:
    size_t index_;
    union {
        variant_storage<monostate, Ts...> storage_;
    };
};

template <bool literal, typename... Ts>
void swap(variant_impl<literal, Ts...>& a, variant_impl<literal, Ts...>& b) {
    a.swap(b);
}

// Declare variant implementation.
template <typename... Ts>
using variant = variant_impl<
    std::is_destructible<variant_storage<Ts...>>::value &&
        std::is_copy_constructible<variant_storage<Ts...>>::value &&
        std::is_copy_assignable<variant_storage<Ts...>>::value &&
        std::is_move_constructible<variant_storage<Ts...>>::value &&
        std::is_move_assignable<variant_storage<Ts...>>::value,
    Ts...>;

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
