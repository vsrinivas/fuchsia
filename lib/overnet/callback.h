// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdlib.h>
#include <string.h>
#include <memory>
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
    void (*move)(void* dst, void* src);
    void (*call)(void* env, Arg&& arg);
    void (*not_called)(void* env);
  };

  Callback() : vtable_(&null_vtable) {}
  ~Callback() { vtable_->not_called(&env_); }

  template <class F,
            typename = typename std::enable_if<sizeof(F) <= kMaxPayload>::type>
  Callback(F&& f) {
    vtable_ = &SmallFunctor<F>::vtable;
    SmallFunctor<F>::InitEnv(&env_, std::forward<F>(f));
  }

  template <class F>
  Callback(AllocatedCallback, F&& f)
      : Callback([pf = new F(std::forward<F>(f))](Arg&& arg) {
          auto fn = pf;
          (*fn)(std::forward<Arg>(arg));
          delete fn;
        }){};

  Callback(const Callback&) = delete;
  Callback& operator=(const Callback&) = delete;

  Callback(Callback&& other) {
    vtable_ = other.vtable_;
    other.vtable_ = &null_vtable;
    vtable_->move(&env_, &other.env_);
  }

  Callback& operator=(Callback&& other) {
    vtable_->not_called(&env_);
    vtable_ = other.vtable_;
    other.vtable_ = &null_vtable;
    vtable_->move(&env_, &other.env_);
    return *this;
  }

  void operator()(Arg arg) {
    const auto* const vtable = vtable_;
    vtable_ = &null_vtable;
    vtable->call(&env_, std::forward<Arg>(arg));
  }

  bool empty() const { return vtable_ == &null_vtable; }

  static Callback Ignored() {
    return [](const Arg&) {};
  }

  static Callback Unimplemented() {
    return [](const Arg&) { abort(); };
  }

  static Callback MustSucceed() {
    return [](const Arg& arg) { assert(arg.is_ok()); };
  }

 private:
  static void NullVTableMove(void*, void*) {}
  static void NullVTableNotCalled(void*) {}
  static void NullVTableCall(void*, Arg&&) { abort(); }

  static const VTable null_vtable;

  template <class F>
  class SmallFunctor {
   public:
    static const VTable vtable;
    static void InitEnv(void* env, F&& f) { new (env) F(std::forward<F>(f)); }

   private:
    static void Move(void* dst, void* src) {
      F* f = static_cast<F*>(src);
      new (dst) F(std::move(*f));
      f->~F();
    }
    static void Call(void* env, Arg&& arg) {
      F* f = static_cast<F*>(env);
      (*f)(std::forward<Arg>(arg));
      f->~F();
    }
    static void NotCalled(void* env) {
      Call(env, Arg(StatusCode::CANCELLED, __PRETTY_FUNCTION__));
    }
  };

  const VTable* vtable_;
  typename std::aligned_storage<kMaxPayload>::type env_;
};

typedef Callback<Status> StatusCallback;
template <class T>
using StatusOrCallback = Callback<StatusOr<T>>;

template <class Arg, size_t kMaxPayload>
const typename Callback<Arg, kMaxPayload>::VTable
    Callback<Arg, kMaxPayload>::null_vtable = {NullVTableMove, NullVTableCall,
                                               NullVTableNotCalled};

template <class Arg, size_t kMaxPayload>
template <class F>
const typename Callback<Arg, kMaxPayload>::VTable
    Callback<Arg, kMaxPayload>::SmallFunctor<F>::vtable = {Move, Call,
                                                           NotCalled};

template <size_t kMaxPayload>
class Callback<void, kMaxPayload> {
 public:
  // Use a (manually constructed) vtable to encode what to do when a callback is
  // made (or not)
  // This has been observed to generate more efficient code than writing out a
  // C++ style vtable
  struct VTable {
    void (*move)(void* dst, void* src);
    void (*call)(void* env);
    void (*not_called)(void* env);
  };

  Callback() : vtable_(&null_vtable) {}
  ~Callback() { vtable_->not_called(&env_); }

  template <class F,
            typename = typename std::enable_if<sizeof(F) <= kMaxPayload>::type>
  Callback(F&& f) {
    vtable_ = &SmallFunctor<F>::vtable;
    SmallFunctor<F>::InitEnv(&env_, std::forward<F>(f));
  }

  template <class F>
  Callback(AllocatedCallback, F&& f)
      : Callback([pf = new F(std::forward<F>(f))]() {
          (*pf)();
          delete pf;
        }){};

  Callback(const Callback&) = delete;
  Callback& operator=(const Callback&) = delete;

  Callback(Callback&& other) {
    vtable_ = other.vtable_;
    other.vtable_ = &null_vtable;
    vtable_->move(&env_, &other.env_);
  }

  Callback& operator=(Callback&& other) {
    vtable_->not_called(&env_);
    vtable_ = other.vtable_;
    other.vtable_ = &null_vtable;
    vtable_->move(&env_, &other.env_);
    return *this;
  }

  void operator()() {
    const auto* const vtable = vtable_;
    vtable_ = &null_vtable;
    vtable->call(&env_);
  }

  bool empty() const { return vtable_ == &null_vtable; }

  static Callback Ignored() {
    return []() {};
  }

  static Callback Unimplemented() {
    return []() { abort(); };
  }

 private:
  static void NullVTableMove(void*, void*) {}
  static void NullVTableNotCalled(void*) {}
  static void NullVTableCall(void*) { abort(); }

  static const VTable null_vtable;

  template <class F>
  class SmallFunctor {
   public:
    static const VTable vtable;
    static void InitEnv(void* env, F&& f) { new (env) F(std::forward<F>(f)); }

   private:
    static void Move(void* dst, void* src) {
      F* f = static_cast<F*>(src);
      new (dst) F(std::move(*f));
      f->~F();
    }
    static void Call(void* env) {
      F* f = static_cast<F*>(env);
      (*f)();
      f->~F();
    }
    static void NotCalled(void* env) { Call(env); }
  };

  const VTable* vtable_;
  typename std::aligned_storage<kMaxPayload>::type env_;
};

template <size_t kMaxPayload>
const typename Callback<void, kMaxPayload>::VTable
    Callback<void, kMaxPayload>::null_vtable = {NullVTableMove, NullVTableCall,
                                                NullVTableNotCalled};

template <size_t kMaxPayload>
template <class F>
const typename Callback<void, kMaxPayload>::VTable
    Callback<void, kMaxPayload>::SmallFunctor<F>::vtable = {Move, Call,
                                                            NotCalled};

}  // namespace overnet
