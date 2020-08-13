// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_UTILS_FIT_PROMISE_H_
#define SRC_DEVELOPER_FORENSICS_UTILS_FIT_PROMISE_H_

#include <lib/fit/promise.h>

#include <memory>
#include <tuple>
#include <type_traits>

namespace forensics {
namespace fit {
namespace internal {

template <typename T>
struct is_unique_ptr : std::false_type {};

template <typename T>
struct is_unique_ptr<std::unique_ptr<T>> : std::true_type {};

template <typename T>
struct is_shared_ptr : std::false_type {};

template <typename T>
struct is_shared_ptr<std::shared_ptr<T>> : std::true_type {};

template <typename T>
struct is_managed_ptr {
  static constexpr bool value = is_unique_ptr<T>::value || is_shared_ptr<T>::value;
};

// To statically assert that all of the parameter pack of ExtendArgsLifetimeBeyondPromise contains
// only managed pointers, we recursively iterate over the elements of the pack, checking that the
// first element of the pack is a manager pointer and storing the logical and of that with the
// result of the rest of the pack. When no elements are left in the pack, and thus we have hit the
// base case of the recursion, we return true.
template <typename... Head>
struct only_managed_ptrs {
  static constexpr bool value = true;
};

// In this specialization of |only_manager_ptrs|, Head is the type of the first element of the
// parameter pack and Tail is the rest of the pack with Head removed.
template <typename Head, typename... Tail>
struct only_managed_ptrs<Head, Tail...> {
  static constexpr bool value = is_managed_ptr<Head>::value && only_managed_ptrs<Tail...>::value;
};

}  // namespace internal

// ExtendArgsLifetimeBeyondPromise takes a promise and the objects it needs to be alive to complete
// properly and guarantees that those objects are not destroyed unitl after the promise
// executes. Fot the sake of simplicity we only allow managed pointers to be used.
template <typename Promise, typename Arg, typename... Args>
Promise ExtendArgsLifetimeBeyondPromise(Promise promise, Arg&& head, Args&&... tail) {
  static_assert(internal::only_managed_ptrs<Arg, Args...>::value,
                "Only managed pointers (std::unique_ptr/std::shared_ptr) can be be kept alive by "
                "ExtendArgsLifetimeBeyondPromise");
  return promise.then([head = std::make_tuple(std::forward<Arg>(head)),
                       tail = std::make_tuple(std::forward<Args>(tail)...)](
                          typename Promise::result_type& result) ->
                      typename Promise::result_type { return std::move(result); });
}

}  // namespace fit
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_UTILS_FIT_PROMISE_H_
