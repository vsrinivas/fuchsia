// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ASYNC_CPP_FUTURE_H_
#define LIB_ASYNC_CPP_FUTURE_H_

#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "garnet/public/lib/fxl/functional/apply.h"
#include "garnet/public/lib/fxl/macros.h"
#include "garnet/public/lib/fxl/memory/ref_counted.h"
#include "garnet/public/lib/fxl/memory/ref_ptr.h"
#include "garnet/public/lib/fxl/memory/weak_ptr.h"

namespace modular {

// # Futures
//
// A *future* is an object representing the eventual value of an asynchronous
// operation. They are a useful complement to callback functions or lambdas
// because they are _composable_: asynchronous operations can be sequentially
// executed, and an async operation's result can be passed to another async
// operation, like a Unix pipeline.
//
// To use a future:
//
// 1. A *producer*, typically an async operation, creates a Future with
//    Future<ResultType>::Create().
// 2. The producer starts its async operation (e.g. a network request or disk
//    read).
// 3. The producer synchronously returns the Future to a *consumer*.
// 4. A consumer attaches a *callback* lambda to the Future using Then(). (The
//    callback can be attached to the future any time after the future is
//    created, before or after the async operation is finished.)
// 5. Some time later, when the producer's async operation is finished, the
//    producer *completes* the future with a *result* using Complete(result).
//    |result| is 0 to N movable or copyable values, e.g. Complete(),
//    Complete(value), Complete(value1, value2, ...).
// 6. The consumer's callback is invoked after the future is completed, with the
//    completed result passed as zero or more parameters to the callback.
//
// The following example shows a simple use case:
//
// Producer:
//
// FuturePtr<Bytes> MakeNetworkRequest(NetworkRequest& request) {
//   auto f = Future<Bytes>::Create();
//   auto network_request_callback = [f] (Bytes bytes) {
//     f->Complete(bytes);
//   };
//   PerformAsyncNetworkRequest(request, network_request_callback);
//   return f;
// }
//
// Client:
//
// FuturePtr<Bytes> f = MakeNetworkRequest();
// f->Then([] (Bytes bytes) {
//   ProcessBytes(bytes);
// });
//
// ## Chaining and sequencing futures
//
// Futures can be composed; this is commonly called "chaining". Methods that
// attach callbacks, such as Then(), will return another Future: the returned
// Future is completed once the previous callback has finished executing. For
// example:
//
// Client:
//
// ShowProgressSpinner(true);
// FuturePtr<Bytes> f = MakeNetworkRequest(request);
// f->AsyncMap([] (Bytes zipped_bytes) {
//   // Use AsyncMap() if your callback wants to return another Future.
//   FuturePtr<Bytes> f = UnzipInBackground(zipped_bytes);
//   return f;
// })->Map([] (Bytes unzipped_bytes) {
//   // Use Map() if your callback returns a non-future: the callback's return
//   // value will be wrapped up into the returned Future.
//   JPEGImage image = DecodeImageSynchronously(unzipped_bytes);
//   return image;
// })->AsyncMap([] (JPEGImage image) {
//   FuturePtr<> f = UpdateUIAsynchronously(image);
//   return f;
// })->Then([] {
//   // Use Then() if your callback returns void. Note that Then() still returns
//   // a Future<>, which will be completed only when your callback finishes
//   // executing.
//   ShowProgressSpinner(false);
// });
//
// ## Use Weak*() variants to cancel callback chains
//
// "Weak" variants exist for all chain/sequence methods (WeakThen(),
// WeakConstThen(), WeakMap() and WeakAsyncMap()). These are almost identical
// to their non-weak counterparts but take an fxl::WeakPtr<T> as a first
// argument. If, at callback invocation time, the WeakPtr is invalid, execution
// will halt and no future further down the chain will be executed.
//
// Example:
//
// FuturePtr<> f = MakeFuture();
// f->WeakThen(weak_ptr_factory.GetWeakPtr(), [] {
//   FXL_LOG(INFO) << "This won't execute";
// })->Then([] {
//   FXL_LOG(INFO) << "Neither will this";
// });
// weak_ptr_factory.InvalidateWeakPtrs();
// f->Complete();
//
// ## Use Wait() to synchronize on multiple futures
//
// If multiple futures are running, use the static Wait() method to create a
// Future that completes when all the futures passed to it are completed:
//
// FuturePtr<Bytes> f1 = MakeNetworkRequest(request1);
// FuturePtr<Bytes> f2 = MakeNetworkRequest(request2);
// FuturePtr<Bytes> f3 = MakeNetworkRequest(request3);
// std::vector<FuturePtr<Bytes>> requests{f1, f2, f3};
// Future<Bytes>::Wait(requests)->Then([] {
//   AllNetworkRequestsAreComplete();
// });
//
// ## Use Completer() to integrate with functions requiring callbacks
//
// Use the Completer() method to integrate with existing code that uses callback
// functions. Completer() returns an std::function<void(Result)> that, when
// called, calls Complete() on the future. Re-visiting the first example:
//
// Without Completer():
//
// FuturePtr<Bytes> MakeNetworkRequest(NetworkRequest& request) {
//   auto f = Future<Bytes>::Create();
//   auto network_request_callback = [f] (Bytes bytes) {
//     f->Complete(bytes);
//   };
//   PerformAsyncNetworkRequest(request, network_request_callback);
//   return f;
// }
//
// With Completer():
//
// FuturePtr<Bytes> MakeNetworkRequest(NetworkRequest& request) {
//   auto f = Future<Bytes>::Create();
//   PerformAsyncNetworkRequest(request, f->Completer());
//   return f;
// }
//
// ## Use error values to propagate errors back to consumers
//
// If the future can fail, use a value (or multiple values) that's capable of
// storing both the error and a successful result to propagate the return value
// back to the consumer. For example:
//
// FuturePtr<std::error_code, Bytes> f = MakeNetworkRequest();
// f->Then([] (std::error_code error, Bytes bytes) {
//   if (error) {
//     // handle error
//   } else {
//     // network request was successful
//     ProcessBytes(bytes);
//   }
// });
//
// ## fuchsia::modular::Future vs other Futures/Promises
//
// If you are familiar with Futures & Promises in other languages, this Future
// class is intentionally different from others, to better integrate with
// Fuchsia coding patterns:
//
// * NOT THREADSAFE. This will change in the future when thread safety is
//   required, but there are no use cases yet. (YAGNI!)
// * Support for multiple result values via variadic template parameters. This
//   is required for smooth integration with the fuchsia::modular::Operation
//   class.
// * Only a single callback can be set via Then(), since move semantics are used
//   so heavily in Fuchsia code. (If multiple callbacks were supported and the
//   result is moved, how does one move the result from one callback to
//   another?)
//   * Multiple callbacks can be set if the callback lambda takes the result via
//     const&. In this case, use ConstThen() to attach each callback, rather
//     than Then(). ConstThen() calls can also be chained, like Then().
// * There are no success/error callbacks and control flows: all callbacks are
//   "success callbacks".
//   * The traditional reason for error callbacks is to convert exceptions into
//     error values, but Google C++ style doesn't use exceptions, so error
//     callbacks aren't needed.
//   * There's some argument that using separate success/error control flow
//     paths is beneficial. However, in practice, a lot of client code using
//     this pattern don't attach error callbacks, only success callbacks, so
//     errors often go unchecked.
//   * If error values need to be propagated back to the client, use a dedicated
//     error type. (Note that many built-in types may have values that can be
//     interpreted as errors, e.g. nullptr, or 0 or -1.) This also forces a
//     consumer to inspect the type and check for errors.
// * No cancellation/progress support. Adding cancellation/progress:
//   * adds more complexity to the futures implementation,
//   * can be implemented on top of a core futures implementation for the few
//     cases where they're required, and
//   * typically requires extra cancel/progress callbacks, which adds more
//     control flow paths.
//   * see fxl::CancelableCallback if you need cancellation.
// * No execution contexts (yet).
//   * There needs to be a comprehensive story about runloops etc first.

template <typename... Result>
class Future;

template <typename... Result>
using FuturePtr = fxl::RefPtr<Future<Result...>>;

namespace {

// Useful type_traits function, ported from C++17.
template <typename From, typename To>
constexpr bool is_convertible_v = std::is_convertible<From, To>::value;

}  // namespace

template <typename... Result>
class Future : public fxl::RefCountedThreadSafe<Future<Result...>> {
 public:
  using result_tuple_type = std::tuple<Result...>;

  // Creates a FuturePtr<Result...>.
  static FuturePtr<Result...> Create() {
    return fxl::AdoptRef(new Future<Result...>);
  }

  // Creates a FuturePtr<Result...> that's already completed. For example:
  //
  //   FuturePtr<int> f = Future<int>::CreateCompleted(5);
  //   f->Then([] (int i) {
  //     // this lambda executes immediately
  //     assert(i == 5);
  //   });
  static FuturePtr<Result...> CreateCompleted(Result&&... result) {
    auto f = Create();
    f->Complete(std::forward<Result>(result)...);
    return f;
  }

  // Returns a Future that completes when every future in |futures| is complete.
  // For example:
  //
  // FuturePtr<Bytes> f1 = MakeNetworkRequest(request1);
  // FuturePtr<Bytes> f2 = MakeNetworkRequest(request2);
  // FuturePtr<Bytes> f3 = MakeNetworkRequest(request3);
  // std::vector<FuturePtr<Bytes>> requests{f1, f2, f3};
  // Future<Bytes>::Wait(requests)->Then([] {
  //   AllNetworkRequestsAreComplete();
  // });
  //
  // This is similar to Promise.All() in JavaScript, or Join() in Rust.
  // TODO: Return a FuturePtr<std::vector<Result>> instead of FuturePtr<>.
  static FuturePtr<> Wait(const std::vector<FuturePtr<Result...>>& futures) {
    if (futures.size() == 0) {
      return Future<>::CreateCompleted();
    }

    FuturePtr<> subfuture = Future<>::Create();

    auto pending_futures = std::make_shared<size_t>(futures.size());

    std::string trace_name;
    for (auto future : futures) {
      trace_name += "[" + future->trace_name() + "]";

      future->AddConstCallback([subfuture, pending_futures](const Result&...) {
        if (--(*pending_futures) == 0) {
          subfuture->Complete();
        }
      });
    }
    subfuture->set_trace_name("(Wait" + trace_name + ")");

    return subfuture;
  }

  // Completes a future with |result|. This causes any callbacks registered
  // with Then(), ConstThen(), etc to be invoked with |result| passed to them
  // as a parameter.
  void Complete(Result&&... result) {
    CompleteWithTuple(std::forward_as_tuple(std::forward<Result&&>(result)...));
  }

  // Returns a std::function<void(Result)> that, when called, calls Complete()
  // on this future. For example:
  //
  // FuturePtr<Bytes> MakeNetworkRequest(NetworkRequest& request) {
  //   auto f = Future<Bytes>::Create();
  //   PerformAsyncNetworkRequest(request, f->Completer());
  //   return f;
  // }
  std::function<void(Result...)> Completer() {
    return [shared_this = FuturePtr<Result...>(this)](Result&&... result) {
      shared_this->Complete(std::forward<Result&&>(result)...);
    };
  }

  // Attaches a |callback| that is invoked when the future is completed with
  // Complete(), and returns a Future that is complete once |callback| has
  // finished executing.
  //
  // * The callback is invoked immedately, on the same thread as the code that
  //   calls Complete().
  // * Only one callback can be attached: any callback that was previously
  //   attached with Then() is discarded.
  // * |callback| is called after callbacks attached with ConstThen().
  // * |callback| is reset to nullptr immediately after it's called.
  //
  // The type of this function looks complex, but is basically:
  //
  //   FuturePtr<> Then(std::function<void(Result...)> callback);
  //
  // The is_convertible_v in the type signature ensures that |callback| has a
  // void return type; see
  // <https://stackoverflow.com/questions/25385876/should-stdfunction-assignment-ignore-return-type>
  // for more info.
  template <typename Callback,
            typename = typename std::enable_if_t<
                is_convertible_v<std::result_of_t<Callback(Result...)>, void>>>
  FuturePtr<> Then(Callback callback) {
    FuturePtr<> subfuture = Future<>::Create();
    subfuture->set_trace_name(trace_name_ + "(Then)");
    SetCallback([trace_name = trace_name_, subfuture,
                 callback = std::move(callback)](Result&&... result) {
      callback(std::forward<Result>(result)...);
      subfuture->Complete();
    });
    return subfuture;
  }

  // Equivalent to Then(), but guards execution of |callback| with a WeakPtr.
  // If, at the time |callback| is to be executed, |weak_ptr| has been
  // invalidated, |callback| is not run, nor is the next Future in the chain
  // completed.
  template <typename Callback, typename T,
            typename = typename std::enable_if_t<
                is_convertible_v<std::result_of_t<Callback(Result...)>, void>>>
  FuturePtr<> WeakThen(fxl::WeakPtr<T> weak_ptr, Callback callback) {
    FuturePtr<> subfuture = Future<>::Create();
    subfuture->set_trace_name(trace_name_ + "(WeakThen)");
    SetCallback([weak_ptr, trace_name = trace_name_, subfuture,
                 callback = std::move(callback)](Result&&... result) {
      if (!weak_ptr)
        return;
      callback(std::forward<Result>(result)...);
      subfuture->Complete();
    });
    return subfuture;
  }

  // Similar to Then(), except that:
  //
  // * |const_callback| must take in the completed result via a const&,
  // * multiple callbacks can be attached,
  // * |const_callback| is called _before_ the Then() callback.
  FuturePtr<> ConstThen(std::function<void(const Result&...)> const_callback) {
    FuturePtr<> subfuture = Future<>::Create();
    subfuture->set_trace_name(trace_name_ + "(ConstThen)");
    AddConstCallback(
        [trace_name = trace_name_, subfuture,
         const_callback = std::move(const_callback)](const Result&... result) {
          const_callback(result...);
          subfuture->Complete();
        });
    return subfuture;
  }

  // See WeakThen().
  template <typename T>
  FuturePtr<> WeakConstThen(
      fxl::WeakPtr<T> weak_ptr,
      std::function<void(const Result&...)> const_callback) {
    FuturePtr<> subfuture = Future<>::Create();
    subfuture->set_trace_name(trace_name_ + "(WeakConstThen)");
    AddConstCallback(
        [weak_ptr, trace_name = trace_name_, subfuture,
         const_callback = std::move(const_callback)](const Result&... result) {
          if (!weak_ptr)
            return;
          const_callback(result...);
          subfuture->Complete();
        });
    return subfuture;
  }

  // Attaches a |callback| that is invoked when this future is completed with
  // Complete(). |callback| must return another future: when the returned future
  // completes, the future returned by AsyncMap() will complete. For example:
  //
  // ShowProgressSpinner(true);
  // FuturePtr<Bytes> f = MakeNetworkRequest(request);
  // f->AsyncMap([] (Bytes zipped_bytes) {
  //   FuturePtr<Bytes> f = UnzipInBackground(zipped_bytes);
  //   return f;
  // })->AsyncMap([] (Bytes unzipped_bytes) {
  //   FuturePtr<JPEGImage> f = DecodeImageInBackground(unzipped_bytes);
  //   return f;
  // })->AsyncMap([] (JPEGImage image) {
  //   FuturePtr<> f = UpdateUIAsynchronously(image);
  //   return f;
  // })->Then([] {
  //   ShowProgressSpinner(false);
  // });
  //
  // The type of this method looks terrifying, but is basically:
  //
  //   FuturePtr<CallbackResult>
  //     AsyncMap(std::function<FuturePtr<CallbackResult>(Result...)> callback);
  template <typename Callback,
            typename AsyncMapResult = std::result_of_t<Callback(Result...)>,
            typename MapResult =
                typename AsyncMapResult::element_type::result_tuple_type,
            typename = typename std::enable_if_t<
                is_convertible_v<FuturePtr<MapResult>, AsyncMapResult>>>
  AsyncMapResult AsyncMap(Callback callback) {
    AsyncMapResult subfuture = AsyncMapResult::element_type::Create();
    subfuture->set_trace_name(trace_name_ + "(AsyncMap)");

    SetCallback([trace_name = trace_name_, subfuture,
                 callback = std::move(callback)](Result&&... result) {
      AsyncMapResult future_result = callback(std::forward<Result>(result)...);

      future_result->SetCallbackWithTuple(
          [subfuture](MapResult&& transformed_result) {
            subfuture->CompleteWithTuple(
                std::forward<MapResult>(transformed_result));
          });
    });
    return subfuture;
  }

  template <typename Callback, typename T,
            typename AsyncMapResult = std::result_of_t<Callback(Result...)>,
            typename MapResult =
                typename AsyncMapResult::element_type::result_tuple_type,
            typename = typename std::enable_if_t<
                is_convertible_v<FuturePtr<MapResult>, AsyncMapResult>>>
  AsyncMapResult WeakAsyncMap(fxl::WeakPtr<T> weak_ptr, Callback callback) {
    AsyncMapResult subfuture = AsyncMapResult::element_type::Create();
    subfuture->set_trace_name(trace_name_ + "(WeakAsyncMap)");

    SetCallback([weak_ptr, trace_name = trace_name_, subfuture,
                 callback = std::move(callback)](Result&&... result) {
      if (!weak_ptr)
        return;
      AsyncMapResult future_result = callback(std::forward<Result>(result)...);

      future_result->SetCallbackWithTuple(
          [subfuture](MapResult&& transformed_result) {
            subfuture->CompleteWithTuple(
                std::forward<MapResult>(transformed_result));
          });
    });
    return subfuture;
  }

  // Attaches a |callback| that is invoked when this future is completed with
  // Complete(). The returned future is completed with |callback|'s return
  // value, when |callback| finishes executing.
  template <typename Callback,
            typename MapResult = std::result_of_t<Callback(Result...)>>
  FuturePtr<MapResult> Map(Callback callback) {
    FuturePtr<MapResult> subfuture = Future<MapResult>::Create();
    subfuture->set_trace_name(trace_name_ + "(Map)");
    SetCallback([trace_name = trace_name_, subfuture,
                 callback = std::move(callback)](Result&&... result) {
      MapResult&& callback_result = callback(std::forward<Result>(result)...);
      subfuture->Complete(std::forward<MapResult>(callback_result));
    });
    return subfuture;
  }

  template <typename Callback, typename T,
            typename MapResult = std::result_of_t<Callback(Result...)>>
  FuturePtr<MapResult> WeakMap(fxl::WeakPtr<T> weak_ptr, Callback callback) {
    FuturePtr<MapResult> subfuture = Future<MapResult>::Create();
    subfuture->set_trace_name(trace_name_ + "(WeakMap)");
    SetCallback([weak_ptr, trace_name = trace_name_, subfuture,
                 callback = std::move(callback)](Result&&... result) {
      if (!weak_ptr)
        return;
      MapResult&& callback_result = callback(std::forward<Result>(result)...);
      subfuture->Complete(std::forward<MapResult>(callback_result));
    });
    return subfuture;
  }

  // Useful in log messages and performance traces.
  void set_trace_name(const std::string& s) { trace_name_ = s; }
  void set_trace_name(std::string&& s) { trace_name_ = s; }

  const std::string& trace_name() const { return trace_name_; }

 private:
  Future() = default;
  FRIEND_REF_COUNTED_THREAD_SAFE(Future);

  std::string trace_name_;

  bool has_result_ = false;
  std::tuple<Result...> result_;

  std::function<void(Result...)> callback_;
  std::vector<std::function<void(const Result&...)>> const_callbacks_;

  // For unit tests only.
  friend class FutureTest;
  const std::tuple<Result...>& get() const {
    FXL_DCHECK(has_result_) << trace_name_ << ": get() called on unset future";

    return result_;
  }

  void SetCallback(std::function<void(Result...)>&& callback) {
    if (!callback) {
      return;
    }

    callback_ = std::move(callback);

    MaybeInvokeCallbacks();
  }

  void SetCallbackWithTuple(
      std::function<void(std::tuple<Result...>)>&& callback) {
    if (!callback) {
      return;
    }

    callback_ = [callback = std::move(callback)](Result&&... result) {
      callback(std::forward_as_tuple(std::forward<Result&&>(result)...));
    };

    MaybeInvokeCallbacks();
  }

  void AddConstCallback(std::function<void(const Result&...)>&& callback) {
    if (!callback) {
      return;
    }

    // It's impossible to add a const callback after a future is completed
    // *and* it has a callback: the completed value will be moved into the
    // callback and won't be available for a ConstThen().
    if (has_result_ && callback_) {
      FXL_LOG(FATAL)
          << "Future@" << static_cast<void*>(this)
          << (trace_name_.length() ? "(" + trace_name_ + ")" : "")
          << ": Cannot add a const callback after completed result is "
             "already moved into Then() callback.";
    }

    const_callbacks_.emplace_back(std::move(callback));

    MaybeInvokeCallbacks();
  }

  void CompleteWithTuple(std::tuple<Result...>&& result) {
    FXL_DCHECK(!has_result_)
        << "Future@" << static_cast<void*>(this)
        << (trace_name_.length() ? "(" + trace_name_ + ")" : "")
        << ": Complete() called twice.";

    result_ = std::forward<std::tuple<Result...>>(result);
    has_result_ = true;

    MaybeInvokeCallbacks();
  }

  void MaybeInvokeCallbacks() {
    if (!has_result_) {
      return;
    }

    if (const_callbacks_.size()) {
      // Move |const_callbacks_| to a local variable. MaybeInvokeCallbacks()
      // can be called multiple times if the client only uses ConstThen() or
      // WeakConstThen() to fetch the completed values. This prevents calling
      // these callbacks multiple times by moving them out of the members
      // scope.
      auto local_const_callbacks = std::move(const_callbacks_);
      for (auto& const_callback : local_const_callbacks) {
        fxl::Apply(const_callback, result_);
      }
    }

    if (callback_) {
      fxl::Apply(callback_, std::move(result_));
    }
  }

  template <typename... Args>
  friend class Future;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(Future);
};

}  // namespace modular

#endif  // LIB_ASYNC_CPP_FUTURE_H_
