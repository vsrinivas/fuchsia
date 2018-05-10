// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdlib.h>
#include <string.h>
#include "status.h"

namespace overnet {

// Completion callback function: a move-only functor that ensures that the
// underlying callback is called once and only once

enum AllocatedCallback { ALLOCATED_CALLBACK };

template <class Arg, size_t kMaxPayload = sizeof(void*)>
class Callback {
 public:
  // Use a (manually constructed) vtable to encode what to do when a callback is
  // made (or not)
  // This has been observed to generate more efficient code than writing out a
  // C++ style vtable
  struct VTable {
    void (*call)(void* env, Arg&& arg);
    void (*not_called)(void* env);
  };

  explicit Callback(const VTable* vtable, void* env, size_t sizeof_env)
      : vtable_(vtable) {
    assert(sizeof_env <= sizeof(env_));
    memcpy(&env_, env, sizeof_env);
  };
  Callback() : vtable_(&null_vtable) {}
  ~Callback() { vtable_->not_called(&env_); }

  template <class F,
            typename = typename std::enable_if_t<
                sizeof(F) <= sizeof(void*) && std::is_trivially_copyable<F>()>>
  Callback(F&& f) {
    vtable_ = &SmallFunctor<F>::vtable;
    SmallFunctor<F>::InitEnv(&env_, std::forward<F>(f));
  }

  template <class F>
  Callback(AllocatedCallback, F&& f)
      : Callback([pf = new F(std::forward<F>(f))](Arg&& arg) {
          (*pf)(std::forward<Arg>(arg));
          delete pf;
        }){};

  Callback(const Callback&) = delete;
  Callback& operator=(const Callback&) = delete;

  Callback(Callback&& other)
      : Callback(other.vtable_, &other.env_, sizeof(other.env_)) {
    other.vtable_ = &null_vtable;
  }

  Callback& operator=(Callback&& other) {
    vtable_->not_called(&env_);
    vtable_ = other.vtable_;
    env_ = other.env_;
    other.vtable_ = &null_vtable;
    return *this;
  }

  void operator()(Arg arg) {
    const auto* const vtable = vtable_;
    vtable_ = &null_vtable;
    vtable->call(&env_, std::forward<Arg>(arg));
  }

  bool empty() const { return vtable_ == &null_vtable; }

  static Callback Ignored() {
    return Callback([](const Arg&) {});
  }

  static Callback Unimplemented() {
    return Callback([](const Arg&) { abort(); });
  }

 private:
  static void NullVTableNotCalled(void*) {}
  static void NullVTableCall(void*, Arg&&) { abort(); }

  static const VTable null_vtable;

  template <class F>
  class SmallFunctor {
   public:
    static const VTable vtable;
    static void InitEnv(void* env, F&& f) { new (env) F(std::forward<F>(f)); }

   private:
    static void Call(void* env, Arg&& arg) {
      (*static_cast<F*>(env))(std::forward<Arg>(arg));
    }
    static void NotCalled(void* env) {
      Call(env, Arg(StatusCode::CANCELLED, __PRETTY_FUNCTION__));
    }
  };

  const VTable* vtable_;
  std::aligned_storage_t<kMaxPayload> env_;
};

typedef Callback<Status> StatusCallback;
template <class T>
using StatusOrCallback = Callback<StatusOr<T>>;

template <class Arg, size_t kMaxPayload>
const typename Callback<Arg, kMaxPayload>::VTable
    Callback<Arg, kMaxPayload>::null_vtable = {NullVTableCall,
                                               NullVTableNotCalled};

template <class Arg, size_t kMaxPayload>
template <class F>
const typename Callback<Arg, kMaxPayload>::VTable
    Callback<Arg, kMaxPayload>::SmallFunctor<F>::vtable = {Call, NotCalled};

}  // namespace overnet
