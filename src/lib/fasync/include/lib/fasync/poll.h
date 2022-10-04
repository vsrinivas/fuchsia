// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FASYNC_INCLUDE_LIB_FASYNC_POLL_H_
#define SRC_LIB_FASYNC_INCLUDE_LIB_FASYNC_POLL_H_

#include <lib/fasync/internal/compiler.h>

LIB_FASYNC_CPP_VERSION_COMPAT_BEGIN

#include <lib/fasync/type_traits.h>
#include <lib/fitx/result.h>

// |fasync::poll|: Type to be returned by futures to indicate their state of completion.
//
// |fasync::poll| can be seen as a more specialized version of |std::optional|: when an executor
// polls a future, the |fasync::poll| value is either still pending (no output) or is ready with a
// value of the output type. It has a specialization for |fitx::result| so that only one
// discriminator is used in this tri-state case.
//
// To make an |fasync::poll|:
//
//   fitx::ready(ready_value)      // Ready for |fasync::poll<T>|.
//   fitx::ready()                 // Ready for |fasync::poll<>| (no output value).
//   fitx::done(ready_value)       // Ready for |fasync::poll<T>|.
//   fitx::done()                  // Ready for |fasync::poll<>| (no output value).
//
//   fasync::pending()             // Pending.
//
// General functions that can always be called:
//
//   bool is_ready()
//   bool is_pending()
//
// Available only when is_ready() (will assert otherwise).
//
//   T& output()                    // Accesses the output.
//   T&& output()                   // Moves the output.

namespace fasync {

// |fasync::pending|: What futures should return if they have not completed their work.
//
// In order  not to be abandoned, the future must also arrange to be woken up later via
// |fasync::suspended_task|.
//
// Example:
//
//   fasync::make_future(
//     [this](fasync::context& context) -> fasync::poll<std::string> {
//       const char* string = GetString();
//       if (string == nullptr) {
//         // Will be woken up to try again
//         context.suspend_task().resume();
//         return fasync::pending();
//       }
//       return fasync::ready(string);
//   })
//
//   fasync::make_future(
//     [this](fasync::context& context) -> fasync::poll<std::string> {
//       const char* string = GetString();
//       if (string == nullptr) {
//         // Will be abandoned
//         return fasync::pending();
//       }
//       return fasync::ready(string);
//   })
//
struct LIB_FASYNC_NODISCARD pending final {};

// |fasync::ready|: Type representing a value of type T to return as the final result of a future's
// work. Returning a value through |fasync::poll| always requires using |fasync::ready| to
// distinguish the ready state from the pending state.
//
// |fasync::poll<T>| is implicitly constructible from any |fasync::ready<U>|, where T is
// constructible from U. This simplifies returning values when the T has converting constructors.
//
// Example:
//   fasync::make_future(
//     [](fasync::context&) -> fasync::poll<int> {
//       // Resolve immediately
//       return fasync::ready(42);
//     })
//
template <typename...>
class ready;

template <typename T>
class LIB_FASYNC_NODISCARD LIB_FASYNC_OWNER_OF(T) ready<T> {
  static_assert(!cpp17::is_reference_v<T>, "Cannot create a ready<T> where T is a reference.");

 public:
  using output_type = T;

  template <typename... Args,
            ::fasync::internal::requires_conditions<std::is_constructible<T, Args...>> = true>
  explicit constexpr ready(Args&&... args) : value_(std::forward<Args>(args)...) {}

  template <typename U, ::fasync::internal::requires_conditions<
                            cpp17::negation<std::is_same<ready<T>, ready<U>>>,
                            std::is_constructible<T, U>> = true>
  constexpr ready(ready<U>&& other) : value_(std::move(other.value_)) {}

  ~ready() = default;

  constexpr ready(const ready&) = default;
  constexpr ready& operator=(const ready&) = default;
  constexpr ready(ready&&) = default;
  constexpr ready& operator=(ready&&) = default;

 private:
  template <typename... Us>
  friend class ready;

  template <typename... Us>
  friend class poll;

  // TODO(schottm): look into [[no_unique_address]]. clang claims to support it in C++11 onwards
  T value_;
};

// Specialization of |fasync::ready| for empty values.
template <>
class LIB_FASYNC_NODISCARD ready<> {
 public:
  using output_type = void;

  constexpr ready() = default;
  ~ready() = default;

  constexpr ready(const ready&) = default;
  constexpr ready& operator=(const ready&) = default;
  constexpr ready(ready&&) = default;
  constexpr ready& operator=(ready&&) = default;
};

#if LIB_FASYNC_HAS_CPP_FEATURE(deduction_guides)

ready()->ready<>;

template <typename T>
ready(T&&) -> ready<std::decay_t<T>>;

#endif

template <typename E = ::fitx::failed, typename... Ts>
using try_ready = ready<::fitx::result<E, Ts...>>;

// Returns |fasync::ready<T>| for the given value, where T is deduced from the argument type. This
// utility is a C++14 compatible alternative to the C++17 deduction guide above.
//
// Example:
//
//   fasync::make_future(
//     [this](fasync::context& context) -> fasync::poll<std::string> {
//       const char* string = GetString();
//       if (string == nullptr) {
//         context.suspend_task().resume();
//         return fasync::pending();
//       }
//       return fasync::done(string);
//   })
//
inline constexpr ready<> done() { return ready<>(); }

template <typename T>
constexpr ready<std::decay_t<T>> done(T&& output) {
  return ready<std::decay_t<T>>(std::forward<T>(output));
}

template <typename...>
class poll;

// |fasync::poll<Ts...>| is the type returned by futures to signal either completion or a pending
// state. It can be constructed from either |fasync::pending| or |fasync::ready<Ts...>| and in the
// latter case the future's output can be accessed via |.output()|.

// Specialization of |fasync::poll| for empty values.
template <>
class LIB_FASYNC_NODISCARD poll<> {
 public:
  using output_type = void;

  constexpr poll(const poll&) = default;
  constexpr poll& operator=(const poll&) = default;
  constexpr poll(poll&&) = default;
  constexpr poll& operator=(poll&&) = default;

  constexpr poll(ready<>) : pending_(false) {}
  constexpr poll(pending) : pending_(true) {}

  constexpr bool is_pending() const { return pending_; }
  constexpr bool is_ready() const { return !is_pending(); }

  constexpr void swap(poll& other) {
    if (&other != this) {
      using std::swap;
      swap(pending_, other.pending_);
    }
  }

 private:
  bool pending_;
};

#if LIB_FASYNC_HAS_CPP_FEATURE(deduction_guides)

poll()->poll<>;

poll(pending)->poll<>;

poll(ready<>)->poll<>;

#endif

template <typename T>
class LIB_FASYNC_NODISCARD LIB_FASYNC_OWNER_OF(T) poll<T> {
  static_assert(!cpp17::is_reference_v<T>, "Cannot create a poll<T> where T is a reference.");

 public:
  using output_type = T;

  constexpr poll(const poll&) = default;
  constexpr poll& operator=(const poll&) = default;
  constexpr poll(poll&&) = default;
  constexpr poll& operator=(poll&&) = default;

  constexpr poll(pending) {}

  // Since we only have one parameter, we pretend it's an error so we don't have to pass void as the
  // first template parameter to ::fit::internal::storage
  template <typename U, ::fasync::internal::requires_conditions<std::is_constructible<T, U>> = true>
  constexpr poll(ready<U>&& ready) : storage_(::fit::internal::error_v, std::move(ready.value_)) {}

  template <typename U,
            ::fasync::internal::requires_conditions<cpp17::negation<std::is_same<poll<T>, poll<U>>>,
                                                    std::is_constructible<T, U>> = true>
  constexpr poll(poll<U>&& other) : storage_(std::move(other.storage_)) {}

  constexpr bool is_pending() const { return storage_.state == ::fit::internal::state_e::empty; }
  constexpr bool is_ready() const { return !is_pending(); }

  constexpr T& output() & {
    LIB_FASYNC_LIKELY if (!is_pending()) { return storage_.error_or_value.error; }
    __builtin_abort();
  }
  constexpr const T& output() const& {
    LIB_FASYNC_LIKELY if (!is_pending()) { return storage_.error_or_value.error; }
    __builtin_abort();
  }
  constexpr T&& output() && {
    LIB_FASYNC_LIKELY if (!is_pending()) { return std::move(storage_.error_or_value.error); }
    __builtin_abort();
  }
  constexpr const T&& output() const&& {
    LIB_FASYNC_LIKELY if (!is_pending()) { return std::move(storage_.error_or_value.error); }
    __builtin_abort();
  }

  constexpr void swap(poll& other) {
    if (&other != this) {
      using std::swap;
      swap(storage_, other.storage_);
    }
  }

 private:
  template <typename... Us>
  friend class poll;

  ::fit::internal::storage<T> storage_;
};

#if LIB_FASYNC_HAS_CPP_FEATURE(deduction_guides)

template <typename T>
poll(ready<T>&&) -> poll<T>;

template <typename T>
poll(poll<T>&&) -> poll<T>;

#endif

// |fasync::try_poll<E, Ts...>| is a convenience wrapper to make it easier to deal with
// |fasync::poll|s with an |::output_type| of |fitx::result<E, Ts...>|.
template <typename E = ::fitx::failed, typename... Ts>
using try_poll = poll<::fitx::result<E, Ts...>>;

template <typename... Ts>
constexpr void swap(poll<Ts...>& p, poll<Ts...>& q) {
  p.swap(q);
}

template <typename... Ts>
constexpr bool operator==(const poll<Ts...>& lhs, const poll<>& rhs) {
  return lhs.is_pending() == rhs.is_pending();
}
template <typename... Ts>
constexpr bool operator!=(const poll<Ts...>& lhs, const poll<>& rhs) {
  return !(lhs == rhs);
}

template <typename... Ts>
constexpr bool operator==(const poll<>& lhs, const poll<Ts...>& rhs) {
  return lhs.is_pending() == rhs.is_pending();
}
template <typename... Ts>
constexpr bool operator!=(const poll<>& lhs, const poll<Ts...>& rhs) {
  return !(lhs == rhs);
}

inline constexpr bool operator==(const poll<>& lhs, const poll<>& rhs) {
  return lhs.is_pending() == rhs.is_pending();
}
inline constexpr bool operator!=(const poll<>& lhs, const poll<>& rhs) { return !(lhs == rhs); }

template <typename... Ts>
constexpr bool operator==(const poll<Ts...>& lhs, const ready<>&) {
  return lhs.is_ready();
}
template <typename... Ts>
constexpr bool operator!=(const poll<Ts...>& lhs, const ready<>&) {
  return !lhs.is_ready();
}

template <typename... Ts>
constexpr bool operator==(const ready<>&, const poll<Ts...>& rhs) {
  return rhs.is_ready();
}
template <typename... Ts>
constexpr bool operator!=(const ready<>&, const poll<Ts...>& rhs) {
  return !rhs.is_ready();
}

template <typename... Ts>
constexpr bool operator==(const poll<Ts...>& lhs, pending) {
  return lhs.is_pending();
}
template <typename... Ts>
constexpr bool operator!=(const poll<Ts...>& lhs, pending) {
  return !lhs.is_pending();
}

template <typename... Ts>
constexpr bool operator==(pending, const poll<Ts...>& rhs) {
  return rhs.is_pending();
}
template <typename... Ts>
constexpr bool operator!=(pending, const poll<Ts...>& rhs) {
  return !rhs.is_pending();
}

template <typename T, typename U,
          ::fit::internal::enable_rel_op<decltype(std::declval<T>() == std::declval<U>())> = true>
constexpr bool operator==(const poll<T>& lhs, const poll<U>& rhs) {
  return lhs.is_pending() == rhs.is_pending() && (lhs.is_pending() || lhs.output() == rhs.output());
}
template <typename T, typename U,
          ::fit::internal::enable_rel_op<decltype(std::declval<poll<T>>() ==
                                                  std::declval<poll<U>>())> = true>
constexpr bool operator!=(const poll<T>& lhs, const poll<U>& rhs) {
  return !(lhs == rhs);
}

template <typename T, typename U,
          ::fit::internal::enable_rel_op<decltype(std::declval<T>() == std::declval<U>()),
                                         cpp17::negation<is_poll<U>>> = true>
constexpr bool operator==(const poll<T>& lhs, const U& rhs) {
  return lhs.is_ready() && lhs.output() == rhs;
}
template <typename T, typename U,
          ::fit::internal::enable_rel_op<decltype(std::declval<poll<T>>() == std::declval<U>()),
                                         cpp17::negation<is_poll<U>>> = true>
constexpr bool operator!=(const poll<T>& lhs, const U& rhs) {
  return !(lhs == rhs);
}

template <typename T, typename U,
          ::fit::internal::enable_rel_op<decltype(std::declval<T>() == std::declval<U>()),
                                         cpp17::negation<is_poll<T>>> = true>
constexpr bool operator==(const T& lhs, const poll<U>& rhs) {
  return rhs.is_ready() && rhs.output() == lhs;
}
template <typename T, typename U,
          ::fit::internal::enable_rel_op<decltype(std::declval<T>() == std::declval<poll<U>>()),
                                         cpp17::negation<is_poll<T>>> = true>
constexpr bool operator!=(const T& lhs, const poll<U>& rhs) {
  return !(lhs == rhs);
}

template <typename T, typename U,
          ::fit::internal::enable_rel_op<decltype(std::declval<T>() < std::declval<U>())> = true>
constexpr bool operator<(const poll<T>& lhs, const poll<U>& rhs) {
  return rhs.is_ready() && (lhs.is_pending() || lhs.output() < rhs.output());
}
template <typename T, typename U,
          ::fit::internal::enable_rel_op<decltype(std::declval<poll<T>>() <
                                                  std::declval<poll<U>>())> = true>
constexpr bool operator>=(const poll<T>& lhs, const poll<U>& rhs) {
  return !(lhs < rhs);
}
template <typename T, typename U,
          ::fit::internal::enable_rel_op<decltype(std::declval<poll<T>>() >=
                                                  std::declval<poll<U>>())> = true>
constexpr bool operator>(const poll<T>& lhs, const poll<U>& rhs) {
  return (lhs >= rhs) && (lhs != rhs);
}
template <typename T, typename U,
          ::fit::internal::enable_rel_op<decltype(std::declval<poll<T>>() >
                                                  std::declval<poll<U>>())> = true>
constexpr bool operator<=(const poll<T>& lhs, const poll<U>& rhs) {
  return !(lhs > rhs);
}

template <typename T, typename U,
          ::fit::internal::enable_rel_op<decltype(std::declval<T>() < std::declval<U>()),
                                         cpp17::negation<is_poll<U>>> = true>
constexpr bool operator<(const poll<T>& lhs, const U& rhs) {
  return !lhs.is_ready() || lhs.output() < rhs;
}
template <typename T, typename U,
          ::fit::internal::enable_rel_op<decltype(std::declval<poll<T>>() < std::declval<U>()),
                                         cpp17::negation<is_poll<U>>> = true>
constexpr bool operator>=(const poll<T>& lhs, const U& rhs) {
  return !(lhs < rhs);
}
template <typename T, typename U,
          ::fit::internal::enable_rel_op<decltype(std::declval<poll<T>>() >= std::declval<U>()),
                                         cpp17::negation<is_poll<U>>> = true>
constexpr bool operator>(const poll<T>& lhs, const U& rhs) {
  return (lhs >= rhs) && (lhs != rhs);
}
template <typename T, typename U,
          ::fit::internal::enable_rel_op<decltype(std::declval<poll<T>>() > std::declval<U>()),
                                         cpp17::negation<is_poll<U>>> = true>
constexpr bool operator<=(const poll<T>& lhs, const U& rhs) {
  return !(lhs > rhs);
}

template <typename T, typename U,
          ::fit::internal::enable_rel_op<decltype(std::declval<T>() < std::declval<U>()),
                                         cpp17::negation<is_poll<T>>> = true>
constexpr bool operator<(const T& lhs, const poll<U>& rhs) {
  return rhs.is_ready() && lhs < rhs.output();
}
template <typename T, typename U,
          ::fit::internal::enable_rel_op<decltype(std::declval<T>() < std::declval<poll<U>>()),
                                         cpp17::negation<is_poll<T>>> = true>
constexpr bool operator>=(const T& lhs, const poll<U>& rhs) {
  return !(lhs < rhs);
}
template <typename T, typename U,
          ::fit::internal::enable_rel_op<decltype(std::declval<T>() >= std::declval<poll<U>>()),
                                         cpp17::negation<is_poll<T>>> = true>
constexpr bool operator>(const T& lhs, const poll<U>& rhs) {
  return (lhs >= rhs) && (lhs != rhs);
}
template <typename T, typename U,
          ::fit::internal::enable_rel_op<decltype(std::declval<T>() > std::declval<poll<U>>()),
                                         cpp17::negation<is_poll<T>>> = true>
constexpr bool operator<=(const T& lhs, const poll<U>& rhs) {
  return !(lhs > rhs);
}

}  // namespace fasync

LIB_FASYNC_CPP_VERSION_COMPAT_END

#endif  // SRC_LIB_FASYNC_INCLUDE_LIB_FASYNC_POLL_H_
