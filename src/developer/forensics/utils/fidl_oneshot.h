// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_UTILS_FIDL_ONESHOT_H_
#define SRC_DEVELOPER_FORENSICS_UTILS_FIDL_ONESHOT_H_

#include <lib/async/cpp/task.h>
#include <lib/async/dispatcher.h>
#include <lib/fidl/cpp/interface_ptr.h>
#include <lib/fit/function.h>
#include <lib/fpromise/bridge.h>
#include <lib/fpromise/promise.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/syslog/cpp/macros.h>

#include <type_traits>
#include <utility>
#include <variant>

#include "src/developer/forensics/utils/errors.h"

namespace forensics {
namespace internal {

// Some template structs to deduce the types returned by a FIDL callback and package them
// appropriately.
//
// Note: this does not work for FIDL methods that don't return anything.
template <typename... T>
struct callback_result : std::false_type {};

// Match a member function and extract the arguments of the callback passed as the final value in
// |Args|.
template <typename Interface, typename... Args>
struct callback_result<void (Interface::*)(Args...)> {
  using type = typename callback_result<Args...>::type;
};

// Traverse the list of arguments to match the callback passed a the final argument of a FIDL call.
template <typename Head, typename Rest>
struct callback_result<Head, Rest> {
  using type = typename callback_result<Rest>::type;
};

// Match a callback with a single argument.
template <typename Arg>
struct callback_result<::fit::function<void(Arg)>> {
  using type = Arg;
};

// Match a callback with multiple arguments and return them in a tuple.
template <typename... Args>
struct callback_result<::fit::function<void(Args...)>> {
  using type = std::tuple<Args...>;
};

}  // namespace internal

// Creates a single connection to |Interface| and calls |Method| on it with |args|. The result is
// returned in a promise that completes with the returned values in the event of success or the
// appropriate error code in the event of an error. The returned promise will have at type of
//
// fpromise::promise<ReturnT, Error> or fpromise::promise<std::tuple<ReturnTs...>, Error>
//
// dependent on the number of values returned.
template <typename Interface, auto Method, typename... Args>
auto OneShotCall(async_dispatcher_t* dispatcher,
                 const std::shared_ptr<sys::ServiceDirectory>& services, const zx::duration timeout,
                 Args&&... args) {
  // Deduce how the results can be returned.
  using result_t = typename internal::callback_result<decltype(Method)>::type;

  ::fpromise::bridge<result_t, Error> bridge;

  // Construct a single-use callback that can be used to unblock the returned promise.
  ::fit::callback<void(std::variant<result_t, Error>)> complete =
      [c = std::move(bridge.completer)](std::variant<result_t, Error> result) mutable {
        if (!c) {
          return;
        }

        if (result.index() == 0) {
          c.complete_ok(std::move(std::get<result_t>(result)));
        } else {
          c.complete_error(std::move(std::get<Error>(result)));
        }
      };

  ::fidl::InterfacePtr<Interface> ptr;
  services->Connect(ptr.NewRequest(dispatcher));

  // Return Error::kConnectionError if the connection is lost.
  ptr.set_error_handler([complete = complete.share()](const zx_status_t status) mutable {
    if (complete == nullptr) {
      return;
    }

    FX_PLOGS(WARNING, status) << "Lost connection to " << Interface::Name_;
    complete(Error::kConnectionError);
  });

  // Return the results of |Method|.
  ((*ptr).*Method)(std::forward<Args>(args)...,
                   [complete = complete.share()](auto&&... result) mutable {
                     if (complete == nullptr) {
                       return;
                     }

                     complete(result_t(std::forward<decltype(result)>(result)...));
                   });

  // Return Error::kTimeout if |timeout| elapses.
  async::PostDelayedTask(
      dispatcher,
      [complete = complete.share()]() mutable {
        if (complete == nullptr) {
          return;
        }

        complete(Error::kTimeout);
      },
      timeout);

  // Keep |ptr| alive until the flow completes.
  return bridge.consumer.promise_or(::fpromise::error(Error::kLogicError))
      .then([ptr = std::move(ptr)](::fpromise::result<result_t, Error>& result) {
        return std::move(result);
      });
}

}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_UTILS_FIDL_ONESHOT_H_
