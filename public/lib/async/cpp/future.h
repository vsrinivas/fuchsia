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
//   auto f = Future<Bytes>::Create("NetworkRequest");  // a "trace_name" that's
//                                                      // logged when things go
//                                                      // wrong
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
// ## Memory Management & Ownership
//
// FuturePtr is a fxl::RefPtr, which is a smart pointer similar to
// std::shared_ptr that holds a reference count (refcount). When you call
// Future::Create(), you are expected to maintain a reference to it. When the
// Future is deleted, its result and callbacks are also deleted.
//
// Each method documents how it affects the future's refcount. To summarize:
//
// * Calling Then() on a future does not affect its refcount. This applies to
//   all methods that returns a chained future, such as AsyncMap() and Map().
// * However, calling Then() on a future will cause that future to maintain a
//   reference to the returned chained future. So, you do not need to maintain a
//   reference to the returned future.
// * Calling Complete() does not affect the future's refcount.
// * Unlike Complete(), the closure returned by Completer() _does own_ the
//   future, so you do not need to maintain a reference to the future after
//   calling Completer(). (You do need to maintain a reference to the closure,
//   however.)
// * Wait(futures) returns a future that every future in |futures| owns, so you
//   do not need to maintain a reference to the returned future. The callback
//   attached to each future in |futures| will also keep a reference to
//   themselves, so that if a future that is Wait()ed on otherwise goes out of
//   scope, the future itself is kept alive.
//
// See each method's documentation for more details on memory management.
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
//   auto f = Future<Bytes>::Create("NetworkRequest");
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
//   auto f = Future<Bytes>::Create("NetworkRequest");
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

// type_traits functions, ported from C++17.
template <typename From, typename To>
constexpr bool is_convertible_v = std::is_convertible<From, To>::value;

template <class T>
constexpr bool is_void_v = std::is_void<T>::value;

}  // namespace

template <typename... Result>
class Future : public fxl::RefCountedThreadSafe<Future<Result...>> {
 public:
  using result_tuple_type = std::tuple<Result...>;

  // Creates a FuturePtr<Result...>. |trace_name| is used solely for debugging
  // purposes, and is logged when something goes wrong (e.g. Complete() is
  // called twice.)
  static FuturePtr<Result...> Create(std::string trace_name) {
    auto f = fxl::AdoptRef(new Future<Result...>);
    f->trace_name_ = std::move(trace_name);
    return f;
  }

  // Creates a FuturePtr<Result...> that's already completed. For example:
  //
  //   FuturePtr<int> f = Future<int>::CreateCompleted("MyFuture", 5);
  //   f->Then([] (int i) {
  //     // this lambda executes immediately
  //     assert(i == 5);
  //   });
  static FuturePtr<Result...> CreateCompleted(std::string trace_name,
                                              Result&&... result) {
    auto f = Create(std::move(trace_name));
    f->Complete(std::forward<Result>(result)...);
    return f;
  }

  // Returns a Future that completes when every future in |futures| is complete.
  // A strong reference is kept to every future in |futures|, so each future
  // will be kept alive if they otherwise go out of scope. The future returned
  // by Wait2() will also be kept alive until every future in |futures|
  // completes. The order of the results corresponds to the order of the given
  // futures, regardless of their completion order.
  //
  // For example:
  //
  // FuturePtr<Bytes> f1 = MakeNetworkRequest(request1);
  // FuturePtr<Bytes> f2 = MakeNetworkRequest(request2);
  // FuturePtr<Bytes> f3 = MakeNetworkRequest(request3);
  // std::vector<FuturePtr<Bytes>> requests{f1, f2, f3};
  // Future<Bytes>::Wait2("NetworkRequests", requests)->Then([](
  //     std::vector<std::tuple<Bytes>> bytes_vector) {
  //   // Note that bytes_vector is an std::vector<std::tuple<Bytes>>, not
  //   // std::vector<Bytes>. This is because Future is a variadic template, but
  //   // std::vector isn't, so each result must be wrapped in a std::tuple.
  //   Bytes f1_bytes = std::get<0>(bytes_vector[0]);
  //   Bytes f2_bytes = std::get<0>(bytes_vector[1]);
  //   Bytes f3_bytes = std::get<0>(bytes_vector[2]);
  // });
  //
  // This is similar to Promise.All() in JavaScript, or Join() in Rust.
  template <typename Results = std::vector<std::tuple<Result...>>>
  static FuturePtr<Results> Wait2(
      std::string trace_name,
      const std::vector<FuturePtr<Result...>>& futures) {
    using ElementType = std::tuple<Result...>;

    if (futures.empty()) {
      return Future<Results>::CreateCompleted(trace_name + "(Completed)", {});
    }

    // Use unique ptrs initially so that we can reserve even if there's a
    // |Result| type that's not default-constructible.
    auto results = std::make_shared<std::vector<std::unique_ptr<ElementType>>>(
        futures.size());

    FuturePtr<Results> all_futures_completed =
        Future<Results>::Create(trace_name + "(WillWait2)");

    auto finished_count = std::make_shared<size_t>(0);

    for (size_t i = 0; i < futures.size(); i++) {
      const auto& future = futures[i];
      // Note that |future| is captured by the callback, to ensure that it'll be
      // completed even if its refcount drops to zero. The callback will be
      // reset after it's run to prevent a retain cycle.
      future->SetCallback([i, future, all_futures_completed, results,
                           finished_count](Result&&... result) {
        (*results)[i] =
            std::make_unique<ElementType>(std::forward<Result&&>(result)...);

        if (++(*finished_count) == results->size()) {
          Results final_results;
          final_results.reserve(results->size());
          for (const auto& result : *results) {
            final_results.push_back(*result);
          }
          all_futures_completed->Complete(std::move(final_results));
        }

        // null out the callback, otherwise there'll be a retain cycle
        // (because |future| is on this callback's capture list).
        future->SetCallback(nullptr);
      });
    }

    return all_futures_completed;
  }

  // DEPRECATED: Use Wait2() instead. This is an older Wait() method that
  // returns a void subfuture rather than a future with results.
  //
  // TODO(MI4-1101): Convert existing uses of Wait() to Wait2(), and remove this
  // method.
  static FuturePtr<> Wait(std::string trace_name,
                          const std::vector<FuturePtr<Result...>>& futures) {
    if (futures.size() == 0) {
      return Future<>::CreateCompleted(trace_name + "(Completed)");
    }

    FuturePtr<> subfuture = Future<>::Create(trace_name + "(WillWait)");

    auto pending_futures = std::make_shared<size_t>(futures.size());

    for (auto future : futures) {
      future->AddConstCallback([subfuture, pending_futures](const Result&...) {
        if (--(*pending_futures) == 0) {
          subfuture->Complete();
        }
      });
    }

    return subfuture;
  }

  // Completes a future with |result|. This causes any callbacks registered
  // with Then(), ConstThen(), etc to be invoked with |result| passed to them
  // as a parameter.
  //
  // Calling Complete() does not affect this future's refcount. This is because:
  //
  // 1. Any callbacks that are registered are called immediately and
  //    synchronously, so the future's lifetime does not need to be extended
  //    before callbacks are invoked.
  // 2. Then() correctly handles cases where the future may be deleted by their
  //    callbacks.
  // 3. There is no danger of the future being deleted before Complete() is
  //    called, because if Complete() is called, the code that calls Complete()
  //    must have a reference to the future.
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
  //
  // The returned closure will maintain a reference to the future, so that the
  // closure can call Complete() on it correctly later. In other words, calling
  // Completer() will increase this future's refcount, and you do not need to
  // maintain a reference to it. After the closure is called, the future's
  // refcount will drop by 1. This enables you to write code like
  //
  //   {
  //     auto f = Future<>::Create();
  //     CallAsyncMethod(f->Completer());
  //     // f will now go out of scope, but f->Completer() owns it, so it's
  //     // still kept alive.
  //   }
  std::function<void(Result...)> Completer() {
    return [shared_this = FuturePtr<Result...>(this)](Result&&... result) {
      shared_this->Complete(std::forward<Result&&>(result)...);
    };
  }

  // Attaches a |callback| that is invoked when the future is completed with
  // Complete(), and returns a Future that is complete once |callback| has
  // finished executing.
  //
  // * The callback is invoked immediately (synchronously); it is not scheduled
  //   on the event loop.
  // * The callback is invoked on the same thread as the code that calls
  //   Complete().
  // * Only one callback can be attached: any callback that was previously
  //   attached with Then() is discarded.
  // * |callback| is called after callbacks attached with ConstThen().
  // * It is safe for |callback| to delete the future that Then() is invoked on.
  //   If this occurs, any chained futures returned by Then(), Map() etc will be
  //   de-referenced by this future and not be completed, even if a reference to
  //   the chained future is maintained elsewhere.
  // * It is also safe for |callback| to delete the chained future that Then()
  //   returns.
  // * The future returned by Then() will be owned by this future, so you do not
  //   need to maintain a reference to it.
  //
  // The type of this function looks complex, but is basically:
  //
  //   FuturePtr<> Then(std::function<void(Result...)> callback);
  template <typename Callback, typename = typename std::enable_if_t<is_void_v<
                                   std::result_of_t<Callback(Result...)>>>>
  FuturePtr<> Then(Callback callback) {
    return SubfutureCreate(
        Future<>::Create(trace_name_ + "(Then)"),
        SubfutureVoidCallback<Result...>(std::move(callback)),
        SubfutureCompleter<>(), [] { return true; });
  }

  // Equivalent to Then(), but guards execution of |callback| with a WeakPtr.
  // If, at the time |callback| is to be executed, |weak_ptr| has been
  // invalidated, |callback| is not run, nor is the next Future in the chain
  // completed.
  template <typename Callback, typename T,
            typename = typename std::enable_if_t<
                is_void_v<std::result_of_t<Callback(Result...)>>>>
  FuturePtr<> WeakThen(fxl::WeakPtr<T> weak_ptr, Callback callback) {
    return SubfutureCreate(
        Future<>::Create(trace_name_ + "(WeakThen)"),
        SubfutureVoidCallback<Result...>(std::move(callback)),
        SubfutureCompleter<>(), [weak_ptr] { return !!weak_ptr; });
  }

  // Similar to Then(), except that:
  //
  // * |const_callback| must take in the completed result via a const&,
  // * multiple callbacks can be attached,
  // * |const_callback| is called _before_ the Then() callback.
  FuturePtr<> ConstThen(std::function<void(const Result&...)> const_callback) {
    FuturePtr<> subfuture = Future<>::Create(trace_name_ + "(ConstThen)");
    AddConstCallback(SubfutureCallback<const Result&...>(
        subfuture,
        SubfutureVoidCallback<const Result&...>(std::move(const_callback)),
        SubfutureCompleter<>(), [] { return true; }));
    return subfuture;
  }

  // See WeakThen().
  template <typename T>
  FuturePtr<> WeakConstThen(
      fxl::WeakPtr<T> weak_ptr,
      std::function<void(const Result&...)> const_callback) {
    FuturePtr<> subfuture = Future<>::Create(trace_name_ + "(WeakConstThen)");
    AddConstCallback(SubfutureCallback<const Result&...>(
        subfuture,
        SubfutureVoidCallback<const Result&...>(std::move(const_callback)),
        SubfutureCompleter<>(), [weak_ptr] { return !!weak_ptr; }));
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
    return SubfutureCreate(
        AsyncMapResult::element_type::Create(trace_name_ + "(AsyncMap)"),
        SubfutureMapCallback(callback),
        SubfutureAsyncMapCompleter<AsyncMapResult>(), [] { return true; });
  }

  template <typename Callback, typename T,
            typename AsyncMapResult = std::result_of_t<Callback(Result...)>,
            typename MapResult =
                typename AsyncMapResult::element_type::result_tuple_type,
            typename = typename std::enable_if_t<
                is_convertible_v<FuturePtr<MapResult>, AsyncMapResult>>>
  AsyncMapResult WeakAsyncMap(fxl::WeakPtr<T> weak_ptr, Callback callback) {
    return SubfutureCreate(
        AsyncMapResult::element_type::Create(trace_name_ + "(WeakAsyncMap)"),
        SubfutureMapCallback(callback),
        SubfutureAsyncMapCompleter<AsyncMapResult>(),
        [weak_ptr] { return !!weak_ptr; });
  }

  // Attaches a |callback| that is invoked when this future is completed with
  // Complete(). The returned future is completed with |callback|'s return
  // value, when |callback| finishes executing.
  template <typename Callback,
            typename MapResult = std::result_of_t<Callback(Result...)>>
  FuturePtr<MapResult> Map(Callback callback) {
    return SubfutureCreate(Future<MapResult>::Create(trace_name_ + "(Map)"),
                           SubfutureMapCallback(std::move(callback)),
                           SubfutureCompleter<MapResult>(),
                           [] { return true; });
  }

  template <typename Callback, typename T,
            typename MapResult = std::result_of_t<Callback(Result...)>>
  FuturePtr<MapResult> WeakMap(fxl::WeakPtr<T> weak_ptr, Callback callback) {
    return SubfutureCreate(Future<MapResult>::Create(trace_name_ + "(WeakMap)"),
                           SubfutureMapCallback(std::move(callback)),
                           SubfutureCompleter<MapResult>(),
                           [weak_ptr] { return !!weak_ptr; });
  }

  const std::string& trace_name() const { return trace_name_; }

 private:
  Future() : result_{}, weak_factory_(this) {}
  FRIEND_REF_COUNTED_THREAD_SAFE(Future);

  // For unit tests only.
  friend class FutureTest;
  const std::tuple<Result...>& get() const {
    FXL_DCHECK(has_result_) << trace_name_ << ": get() called on unset future";

    return result_;
  }

  void SetCallback(std::function<void(Result...)>&& callback) {
    callback_ = callback;

    MaybeInvokeCallbacks();
  }

  void SetCallbackWithTuple(
      std::function<void(std::tuple<Result...>)>&& callback) {
    SetCallback([callback](Result&&... result) {
      callback(std::forward_as_tuple(std::forward<Result&&>(result)...));
    });
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

    const_callbacks_.emplace_back(callback);

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

  // The "subfuture" methods below are private helper functions designed to be
  // used with the futures that are returned by the public API (Then(), Map(),
  // etc); those returned futures are named "subfutures" in the code, which is
  // why the methods are named likewise.

  // A convenience method to call this->SetCallback() with the lambda returned
  // by SubfutureCallback().
  template <typename Subfuture, typename SubfutureCompleter, typename Callback,
            typename Guard>
  Subfuture SubfutureCreate(Subfuture subfuture, Callback&& callback,
                            SubfutureCompleter&& subfuture_completer,
                            Guard&& guard) {
    SetCallback(SubfutureCallback<Result...>(subfuture, callback,
                                             subfuture_completer, guard));
    return subfuture;
  }

  // Returns a lambda that:
  //
  // 1. calls |guard| before calling |callback|, and only calls |callback| if
  //    |guard| returns true;
  // 2. will not call |subfuture_completer| if either |this| or |subfuture| are
  //    destroyed by |callback|.
  template <typename... CoercedResult, typename Subfuture,
            typename SubfutureCompleter, typename Callback, typename Guard>
  auto SubfutureCallback(Subfuture subfuture, Callback&& callback,
                         SubfutureCompleter&& subfuture_completer,
                         Guard&& guard) {
    return [this, subfuture, callback, subfuture_completer,
            guard](CoercedResult&&... result) {
      if (!guard())
        return;

      auto weak_future = weak_factory_.GetWeakPtr();
      auto weak_subfuture = subfuture->weak_factory_.GetWeakPtr();
      auto subfuture_result = callback(std::forward<CoercedResult>(result)...);

      // |callback| above may delete this future or the returned subfuture when
      // it finishes executing, so check if |weak_future| and |weak_subfuture|
      // are still valid before attempting to complete the subfuture.
      if (weak_future && weak_subfuture)
        subfuture_completer(subfuture, std::move(subfuture_result));
    };
  }

  // Returns a lambda that calls |callback|, and returns an empty tuple. This is
  // designed to be used with the SubfutureMapCallback() method (below), which
  // returns a one-element tuple, so that calling either function will always
  // return a std::tuple<T...>. The consistent return type enables generic
  // programming techniques to be applied to |callback| since the return type is
  // consistent (it's always a std::tuple<T...>).
  template <typename... CoercedResult, typename Callback>
  auto SubfutureVoidCallback(Callback&& callback) {
    return [callback](CoercedResult&&... result) {
      callback(std::forward<CoercedResult>(result)...);
      return std::make_tuple();
    };
  }

  // See the documentation for SubfutureVoidCallback() above.
  template <typename Callback>
  auto SubfutureMapCallback(Callback&& callback) {
    return [callback](Result&&... result) {
      return std::make_tuple(callback(std::forward<Result>(result)...));
    };
  }

  // Returns a lambda that, when called with a subfuture and a std::tuple, will
  // complete the subfuture with the values from the tuple elements. This method
  // is designed to be used with SubfutureAsyncMapCompleter(), which will do
  // the same thing but can be passed Futures for the std::tuple values.
  // Together, this enables generic programming techniques to be applied to the
  // returned lambda, since the lambda presents a consistent API for callers.
  template <typename... SubfutureResult>
  auto SubfutureCompleter() {
    return [](FuturePtr<SubfutureResult...> subfuture,
              std::tuple<SubfutureResult...> subfuture_result) {
      subfuture->CompleteWithTuple(std::move(subfuture_result));
    };
  }

  // See the documentation for SubfutureCompleter() above.
  template <typename AsyncMapResult>
  auto SubfutureAsyncMapCompleter() {
    return [](AsyncMapResult subfuture,
              std::tuple<AsyncMapResult> subfuture_result) {
      std::get<0>(subfuture_result)
          ->SetCallbackWithTuple(
              [subfuture](
                  std::tuple<
                      typename AsyncMapResult::element_type::result_tuple_type>
                      transformed_result) {
                subfuture->CompleteWithTuple(
                    std::move(std::get<0>(transformed_result)));
              });
    };
  }

  template <typename... Args>
  friend class Future;

  std::string trace_name_;

  bool has_result_ = false;
  std::tuple<Result...> result_;

  // TODO(MI4-1102): Convert std::function to fit::function here & everywhere.

  // The callback attached to this future.
  std::function<void(Result...)> callback_;

  // Callbacks that have attached with the Const*() methods, such as
  // ConstThen().
  std::vector<std::function<void(const Result&...)>> const_callbacks_;

  // Keep this last in the list of members. (See WeakPtrFactory documentation
  // for more info.)
  fxl::WeakPtrFactory<Future<Result...>> weak_factory_;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(Future);
};

}  // namespace modular

#endif  // LIB_ASYNC_CPP_FUTURE_H_
