// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIT_OPTIONAL_H_
#define LIB_FIT_OPTIONAL_H_

#include <assert.h>

#include <new>
#include <type_traits>
#include <utility>

namespace fit {
namespace internal {

template <typename T, bool = std::is_assignable<T&, const T&>::value>
struct copy_assign_or_reconstruct final {
    static void assign(T* dest, const T& source) {
        dest->~T();
        new (dest) T(source);
    }
};

template <typename T>
struct copy_assign_or_reconstruct<T, true> final {
    static void assign(T* dest, const T& source) {
        *dest = source;
    }
};

template <typename T, bool = std::is_assignable<T&, T&&>::value>
struct move_assign_or_reconstruct final {
    static void assign(T* dest, T&& source) {
        dest->~T();
        new (dest) T(std::move(source));
    }

    static void swap(T& a, T& b) {
        T temp(std::move(a));
        a.~T();
        new (&a) T(std::move(b));
        b.~T();
        new (&b) T(std::move(temp));
    }
};

template <typename T>
struct move_assign_or_reconstruct<T, true> final {
    static void assign(T* dest, T&& source) {
        *dest = std::move(source);
    }

    static void swap(T& a, T& b) {
        using std::swap;
        swap(a, b);
    }
};

} // namespace internal

// A sentinel value for |fit::optional<T>| indicating that it contains
// no value.
struct nullopt_t {
    explicit constexpr nullopt_t(int) {}
};
static constexpr nullopt_t nullopt(0);

// A minimal implementation of an |std::optional<T>| work-alike for C++ 14.
//
// See also |fit::nullable<T>| which may be more efficient in certain
// circumstances if T can be initialized, assigned, and compared with
// nullptr.
//
// TODO(US-90): The initial implementation only covers a minimal subset of the
// std::optional API.  Flesh this out more fully then define fit::optional
// to be an alias for std::optional when compiling with C++ 17.
template <typename T>
class optional final {
public:
    using value_type = T;

    constexpr optional()
        : has_value_(false) {}
    constexpr optional(nullopt_t)
        : has_value_(false) {}

    explicit constexpr optional(T value)
        : has_value_(true), value_(std::move(value)) {}

    optional(const optional& other)
        : has_value_(other.has_value_) {
        if (has_value_) {
            new (&value_) T(other.value_);
        }
    }

    optional(optional&& other)
        : has_value_(other.has_value_) {
        if (has_value_) {
            new (&value_) T(std::move(other.value_));
            other.value_.~T();
            other.has_value_ = false;
        }
    }

    // TODO(US-90): Presence of this destructor makes the type non-literal.
    // We should specialize this type to handle the case where T is literal
    // explicitly so that expressions these types can be constexpr.
    ~optional() {
        if (has_value_) {
            value_.~T();
        }
    }

    constexpr T& value() & {
        assert(has_value_);
        return value_;
    }

    constexpr const T& value() const& {
        assert(has_value_);
        return value_;
    }

    constexpr T&& value() && {
        assert(has_value_);
        return std::move(value_);
    }

    constexpr const T&& value() const&& {
        assert(has_value_);
        return std::move(value_);
    }

    template <typename U = T>
    constexpr T value_or(U&& default_value) const {
        return has_value_ ? value_ : static_cast<T>(std::forward<U>(default_value));
    }

    constexpr T* operator->() { return &value_; }
    constexpr const T* operator->() const { return &value_; }
    constexpr T& operator*() { return value_; }
    constexpr const T& operator*() const { return value_; }

    bool has_value() const { return has_value_; }
    explicit operator bool() const { return has_value(); }

    optional& operator=(const optional& other) {
        if (&other == this)
            return *this;
        if (has_value_) {
            if (other.has_value_) {
                ::fit::internal::copy_assign_or_reconstruct<T>::assign(
                    &value_, other.value_);
            } else {
                reset();
            }
        } else if (other.has_value_) {
            new (&value_) T(other.value_);
            has_value_ = true;
        }
        return *this;
    }

    optional& operator=(optional&& other) {
        if (&other == this)
            return *this;
        if (has_value_) {
            if (other.has_value_) {
                ::fit::internal::move_assign_or_reconstruct<T>::assign(
                    &value_, std::move(other.value_));
                other.value_.~T();
                other.has_value_ = false;
            } else {
                reset();
            }
        } else if (other.has_value_) {
            new (&value_) T(std::move(other.value_));
            has_value_ = true;
            other.value_.~T();
            other.has_value_ = false;
        }
        return *this;
    }

    optional& operator=(nullopt_t) {
        reset();
        return *this;
    }

    optional& operator=(T value) {
        if (has_value_) {
            ::fit::internal::move_assign_or_reconstruct<T>::assign(
                &value_, std::move(value));
        } else {
            new (&value_) T(std::move(value));
            has_value_ = true;
        }
        return *this;
    }

    void reset() {
        if (has_value_) {
            value_.~T();
            has_value_ = false;
        }
    }

    void swap(optional& other) {
        if (&other == this)
            return;
        if (has_value_) {
            if (other.has_value_) {
                ::fit::internal::move_assign_or_reconstruct<T>::swap(
                    value_, other.value_);
            } else {
                new (&other.value_) T(std::move(value_));
                other.has_value_ = true;
                value_.~T();
                has_value_ = false;
            }
        } else if (other.has_value_) {
            new (&value_) T(std::move(other.value_));
            has_value_ = true;
            other.value_.~T();
            other.has_value_ = false;
        }
    }

    template <typename... Args>
    T& emplace(Args&&... args) {
        reset();
        new (&value_) T(std::forward<Args...>(args)...);
        has_value_ = true;
        return value_;
    }

private:
    bool has_value_;
    union {
        T value_;
    };
};

template <typename T>
void swap(optional<T>& a, optional<T>& b) {
    a.swap(b);
}

template <typename T>
constexpr bool operator==(const optional<T>& lhs, nullopt_t) {
    return !lhs.has_value();
}
template <typename T>
constexpr bool operator!=(const optional<T>& lhs, nullopt_t) {
    return lhs.has_value();
}

template <typename T>
constexpr bool operator==(nullopt_t, const optional<T>& rhs) {
    return !rhs.has_value();
}
template <typename T>
constexpr bool operator!=(nullopt_t, const optional<T>& rhs) {
    return rhs.has_value();
}

template <typename T, typename U>
constexpr bool operator==(const optional<T>& lhs, const optional<U>& rhs) {
    return (lhs.has_value() == rhs.has_value()) && (!lhs.has_value() || *lhs == *rhs);
}
template <typename T, typename U>
constexpr bool operator!=(const optional<T>& lhs, const optional<U>& rhs) {
    return (lhs.has_value() != rhs.has_value()) || (lhs.has_value() && *lhs != *rhs);
}

template <typename T, typename U>
constexpr bool operator==(const optional<T>& lhs, const U& rhs) {
    return lhs.has_value() && *lhs == rhs;
}
template <typename T, typename U>
constexpr bool operator!=(const optional<T>& lhs, const U& rhs) {
    return !lhs.has_value() || *lhs != rhs;
}

template <typename T, typename U>
constexpr bool operator==(const T& lhs, const optional<U>& rhs) {
    return rhs.has_value() && lhs == *rhs;
}
template <typename T, typename U>
constexpr bool operator!=(const T& lhs, const optional<U>& rhs) {
    return !rhs.has_value() || lhs != *rhs;
}

} // namespace fit

#endif // LIB_FIT_OPTIONAL_H_
