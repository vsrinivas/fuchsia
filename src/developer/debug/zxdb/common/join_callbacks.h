// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_COMMON_JOIN_CALLBACKS_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_COMMON_JOIN_CALLBACKS_H_

#include <lib/fit/function.h>
#include <lib/syslog/cpp/macros.h>

#include <vector>

#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/common/ref_ptr_to.h"
#include "src/lib/fxl/memory/ref_counted.h"

// Provides some helpers to join a series of callbacks into a single callback when they are all
// complete.
//
// There are three variants:
//
//   * JoinCallbacks<void> that joins a sequence of void callbacks.
//
//   * JoinCallbacks<T> that joins a sequence of single-parameter callbacks and provides the
//     result as a std::vector<T>
//
//   * JoinErrCallbacks that joins a series of callbacks that accept Err and reports the
//     global success or failure in a single Err (corresponding to the first error).
//
// The method of operation is the same for each:
//
//  1. Create as reference counted, providing the final callback to issue when complete:
//
//       auto join = fxl::MakeRefCounted<JoinCallbacks<int>>(
//           [](std::vector<int> params) { ... });
//
//
//  2. Create any sub-callbacks and schedule them to be executed.
//
//       DoAsyncOperation(join->AddCallback());
//
//     Note: Is is OK for the sub-callbacks to execute immediately. The final callback won't
//     be issued until Ready() is called.
//
//
//  3. Signal that you are done adding callbacks:
//
//       join->Ready();
//
//     If all sub-callbacks have already been issued or there are no sub-callbacks, this call will
//     synchronously issue the outer callback. If you do not call Ready(), the final callback will
//     never be issued and everything will leak.
//
// Sometimes you may encounter an error in the middle of creating callbacks. In this case, you can
// call Abandon() which will mark the operation complete and the final callback will never be
// issued.
//
// Typical usage using the "Err" variant:
//
//   auto join = fxl::MakeRefCounted<JoinErrCallbacks>([](const Err& err) {
//     if (err.has_error()) {
//       ... report error ...
//     } else {
//       ... do something on success.
//     }
//   });
//   for (const auto& thread : all_threads) {
//     if (!ScheduleAsyncWork(join->AddCallback())) {
//       // Error scheduling, give up on the whole thing.
//       join->Abandon();
//       ... report error ...
//       return;
//      }
//   }
//   join->Ready();
//
namespace zxdb {

class JoinCallbacksBase : public fxl::RefCountedThreadSafe<JoinCallbacksBase> {
 public:
  void Ready() {
    FX_DCHECK(state_ == kSetup);
    state_ = kWaiting;
    if (remaining_ == 0) {
      state_ = kDone;
      Issue();
    }
  }

  // Aborts the operation. Any pending operations using the child callbacks will not be canceled
  // (this class has no way to do that), but the result will be ignored.
  void Abandon() {
    FX_DCHECK(state_ != kDone);
    state_ = kAbandoned;
  }

 protected:
  FRIEND_REF_COUNTED_THREAD_SAFE(JoinCallbacksBase);

  // Implemented by the derived classes to issue the correct callback. There are ways to avoid this
  // virtual call but they are not worth the complexity for this use-case (we're already doing lots
  // of heap operations).
  virtual void Issue() = 0;

  void TrackAdd() {
    FX_DCHECK(state_ == kSetup);  // Can't add more callbacks after Waiting() or Abandon().
    remaining_++;
  }
  void TrackGotCallback() {
    FX_DCHECK(state_ != kDone);
    FX_DCHECK(remaining_ > 0);
    remaining_--;
    if (state_ == kWaiting && remaining_ == 0) {
      state_ = kDone;
      Issue();
    }
  }

  JoinCallbacksBase() = default;
  virtual ~JoinCallbacksBase() {
    // Destroyed too early. Most likely you forgot to call Waiting() or Abandon() (the state will be
    // kSetup in this case). This could also happen if there's an internal error (state == kWaiting)
    // where the reference count got decremented without checking the callback.
    FX_DCHECK(state_ == kAbandoned || state_ == kDone);
  }

  enum State {
    kSetup,
    kWaiting,  // This object is waiting for all the callbacks to be issued.
    kAbandoned,
    kDone,  // Callback issued, everything done.
  };

  State state_ = kSetup;

 private:
  int remaining_ = 0;  // Remaining callbacks to wait for.
};

// Supports joining a sequence of callbacks (with one parameter only) into a single callback that
// takes a vector of their parameters. The resulting vector will be in order that the callbacks
// were CREATED (not issued).
//
// This implementation requires that the parameter type be default-constructible. This requirement
// could be avoided but the alternatives add implementation complexity and some type of runtime
// overhead or fragility.
//
// Supporting multiple parameters for each callback adds significant template complexity and
// requires us either to store a std::tuple (which can be difficult to use) or have the caller
// provide some container type (difficult to use in a different way). It also makes the common case
// of one parameter more difficult (or we need even more template specializations).
//
// If you need multiple parameters, it's recommended you wrap the callback to pack the parameters
// into a struct and then pass that struct to the callback provided by this class.
template <typename T>
class JoinCallbacks : public JoinCallbacksBase {
 public:
  using ParamType = T;
  using VectorType = std::vector<ParamType>;
  using MainCallbackType = fit::callback<void(VectorType)>;

  // See comment above. This flags the error before we get to a more obscure one trying to
  // emplace_back() or assign below.
  static_assert(std::is_default_constructible<T>::value,
                "Type for JoinCallbacks must be default construtible.");
  static_assert(std::is_move_assignable<T>::value,
                "Type for JoinCallbacks must be move assignable.");

  fit::callback<void(T)> AddCallback() {
    TrackAdd();

    size_t slot_index = params_.size();
    params_.emplace_back();

    return [ref = RefPtrTo(this), slot_index](T param) mutable {
      if (ref->state_ != kAbandoned) {
        // Save the parameter result. This shouldn't happen in the "done" case but TrackGotCallback
        // will assert below if that happens.
        FX_DCHECK(slot_index < ref->params_.size());
        ref->params_[slot_index] = std::move(param);
      }
      ref->TrackGotCallback();
    };
  }

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(JoinCallbacks);
  FRIEND_MAKE_REF_COUNTED(JoinCallbacks);

  explicit JoinCallbacks(MainCallbackType cb) : cb_(std::move(cb)) {}

  void Issue() override { cb_(std::move(params_)); }

  MainCallbackType cb_;

  std::vector<T> params_;
};

// Specialization for when there are no callback parameters.
template <>
class JoinCallbacks<void> : public JoinCallbacksBase {
 public:
  fit::callback<void()> AddCallback() {
    TrackAdd();
    return [ref = RefPtrTo(this)]() mutable { ref->TrackGotCallback(); };
  }

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(JoinCallbacks);
  FRIEND_MAKE_REF_COUNTED(JoinCallbacks);

  explicit JoinCallbacks(fit::callback<void()> cb) : cb_(std::move(cb)) {}

  void Issue() override { cb_(); }

  fit::callback<void()> cb_;
};

// Joins multiple callbacks that take an Err parameter. The result of the main callback is either
// success if all sub-callbacks succeeded, or the Err corresponding to the first callback to issue
// an error.
class JoinErrCallbacks : public JoinCallbacksBase {
 public:
  fit::callback<void(const Err&)> AddCallback() {
    TrackAdd();
    return [ref = RefPtrTo(this)](const Err& err) mutable {
      if (ref->state_ == kWaiting && err.has_error()) {
        // Got an error for the first time, issue the error and abandon any remaining callbacks.
        ref->state_ = kAbandoned;
        ref->cb_(err);
      }
      ref->TrackGotCallback();
    };
  }

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(JoinErrCallbacks);
  FRIEND_MAKE_REF_COUNTED(JoinErrCallbacks);

  explicit JoinErrCallbacks(fit::callback<void(const Err&)> cb) : cb_(std::move(cb)) {}

  void Issue() override {
    // This is called by the base class only in the non-error cases.
    cb_(Err());
  }

  fit::callback<void(const Err&)> cb_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_COMMON_JOIN_CALLBACKS_H_
