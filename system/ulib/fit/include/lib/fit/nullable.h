// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIT_NULLABLE_H_
#define LIB_FIT_NULLABLE_H_

#include <assert.h>

#include <type_traits>
#include <utility>

#include "optional.h"

namespace fit {

// Determines whether a type can be compared with nullptr.
template <typename T, typename Comparable = bool>
struct is_comparable_with_null : public std::false_type {};
template <typename T>
struct is_comparable_with_null<T, decltype(std::declval<const T&>() == nullptr)>
    : public std::true_type {};

// Returns true if a value equals nullptr.
template <typename T, typename Comparable = bool>
struct is_null_predicate {
    constexpr bool operator()(const T& value) { return false; }
};
template <typename T>
struct is_null_predicate<T, decltype(std::declval<const T&>() == nullptr)> {
    constexpr bool operator()(const T& value) { return value == nullptr; }
};
template <typename T>
constexpr inline bool is_null(const T& value) {
    return is_null_predicate<T>()(value);
}

// Determines whether a type can be initialized, assigned, and compared
// with nullptr.
template <typename T>
struct is_nullable
    : public std::integral_constant<
          bool,
          std::is_constructible<T, decltype(nullptr)>::value &&
              std::is_assignable<T&, decltype(nullptr)>::value &&
              is_comparable_with_null<T>::value> {};
template <>
struct is_nullable<void> : public std::false_type {};

// Holds a value or nullptr.
//
// This class is similar to |std::optional<T>| except that it uses less
// storage when the value type can be initialized, assigned, and compared
// with nullptr.
//
// For example:
// - sizeof(fit::nullable<void*>) == sizeof(void*)
// - sizeof(std::optional<void*>) == sizeof(struct { bool; void*; })
// - sizeof(fit::nullable<int>) == sizeof(struct { bool; int; })
// - sizeof(std::optional<int>) == sizeof(struct { bool; int; })
template <typename T, bool = (is_nullable<T>::value &&
                              std::is_constructible<T, T&&>::value &&
                              std::is_assignable<T&, T&&>::value)>
class nullable final {
public:
    using value_type = T;

    constexpr nullable() = default;
    explicit constexpr nullable(decltype(nullptr)) {}
    explicit constexpr nullable(T value)
        : opt_(std::move(value)) {}
    nullable(const nullable& other) = default;
    nullable(nullable&& other) = default;
    ~nullable() = default;

    constexpr T& value() & { return opt_.value(); }
    constexpr const T& value() const& { return opt_.value(); }
    constexpr T&& value() && { return std::move(opt_.value()); }
    constexpr const T&& value() const&& { return std::move(opt_.value()); }

    template <typename U = T>
    constexpr T value_or(U&& default_value) const {
        return opt_.value_or(std::forward<U>(default_value));
    }

    constexpr T* operator->() { return &*opt_; }
    constexpr const T* operator->() const { return &*opt_; }
    constexpr T& operator*() { return *opt_; }
    constexpr const T& operator*() const { return *opt_; }

    constexpr bool has_value() const { return opt_.has_value(); }
    explicit constexpr operator bool() const { return has_value(); }

    nullable& operator=(const nullable& other) = default;
    nullable& operator=(nullable&& other) = default;

    nullable& operator=(decltype(nullptr)) {
        reset();
        return *this;
    }

    nullable& operator=(T value) {
        opt_ = std::move(value);
        return *this;
    }

    void reset() { opt_.reset(); }

    void swap(nullable& other) { opt_.swap(other.opt_); }

private:
    optional<T> opt_;
};

template <typename T>
class nullable<T, true> final {
public:
    using value_type = T;

    constexpr nullable()
        : value_(nullptr) {}
    explicit constexpr nullable(decltype(nullptr))
        : value_(nullptr) {}
    explicit constexpr nullable(T value)
        : value_(std::move(value)) {}
    nullable(const nullable& other) = default;
    nullable(nullable&& other)
        : value_(std::move(other.value_)) {
        other.value_ = nullptr;
    }
    ~nullable() = default;

    constexpr T& value() & {
        assert(has_value());
        return value_;
    }
    constexpr const T& value() const& {
        assert(has_value());
        return value_;
    }
    constexpr T&& value() && {
        assert(has_value());
        return std::move(value_);
    }
    constexpr const T&& value() const&& {
        assert(has_value());
        return std::move(value_);
    }

    template <typename U = T>
    constexpr T value_or(U&& default_value) const {
        return has_value() ? value_ : static_cast<T>(std::forward<U>(default_value));
    }

    constexpr T* operator->() { return &value_; }
    constexpr const T* operator->() const { return &value_; }
    constexpr T& operator*() { return value_; }
    constexpr const T& operator*() const { return value_; }

    constexpr bool has_value() const { return !(value_ == nullptr); }
    explicit constexpr operator bool() const { return has_value(); }

    nullable& operator=(const nullable& other) = default;
    nullable& operator=(nullable&& other) {
        if (&other == this)
            return *this;
        value_ = std::move(other.value_);
        other.value_ = nullptr;
        return *this;
    }

    nullable& operator=(decltype(nullptr)) {
        reset();
        return *this;
    }

    nullable& operator=(T value) {
        value_ = std::move(value);
        return *this;
    }

    void reset() { value_ = nullptr; }

    void swap(nullable& other) {
        using std::swap;
        swap(value_, other.value_);
    }

private:
    T value_;
};

template <typename T>
void swap(nullable<T>& a, nullable<T>& b) {
    a.swap(b);
}

template <typename T>
constexpr bool operator==(const nullable<T>& lhs, decltype(nullptr)) {
    return !lhs.has_value();
}
template <typename T>
constexpr bool operator!=(const nullable<T>& lhs, decltype(nullptr)) {
    return lhs.has_value();
}

template <typename T>
constexpr bool operator==(decltype(nullptr), const nullable<T>& rhs) {
    return !rhs.has_value();
}
template <typename T>
constexpr bool operator!=(decltype(nullptr), const nullable<T>& rhs) {
    return rhs.has_value();
}

template <typename T, typename U>
constexpr bool operator==(const nullable<T>& lhs, const nullable<U>& rhs) {
    return (lhs.has_value() == rhs.has_value()) && (!lhs.has_value() || *lhs == *rhs);
}
template <typename T, typename U>
constexpr bool operator!=(const nullable<T>& lhs, const nullable<U>& rhs) {
    return (lhs.has_value() != rhs.has_value()) || (lhs.has_value() && *lhs != *rhs);
}

template <typename T, typename U>
constexpr bool operator==(const nullable<T>& lhs, const U& rhs) {
    return (lhs.has_value() != is_null(rhs)) && (!lhs.has_value() || *lhs == rhs);
}
template <typename T, typename U>
constexpr bool operator!=(const nullable<T>& lhs, const U& rhs) {
    return (lhs.has_value() == is_null(rhs)) || (lhs.has_value() && *lhs != rhs);
}

template <typename T, typename U>
constexpr bool operator==(const T& lhs, const nullable<U>& rhs) {
    return (is_null(lhs) != rhs.has_value()) && (!rhs.has_value() || lhs == *rhs);
}
template <typename T, typename U>
constexpr bool operator!=(const T& lhs, const nullable<U>& rhs) {
    return (is_null(lhs) == rhs.has_value()) || (rhs.has_value() && lhs != *rhs);
}

} // namespace fit

#endif // LIB_FIT_NULLABLE_H_
