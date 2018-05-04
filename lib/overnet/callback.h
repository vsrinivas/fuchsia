// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdlib.h>
#include "status.h"

namespace overnet {

// Completion callback function: a move-only functor that ensures that the
// underlying callback is called once and only once
//
// Typically constructed with one of the constructor templates below...

template <class Arg>
class Callback {
 public:
  // Use a (manually constructed) vtable to encode what to do when a callback is
  // made (or not)
  // This has been observed to generate more efficient code than writing out a
  // C++ style vtable
  struct VTable {
    void (*call)(void* env, const Arg& arg);
    void (*not_called)(void* env);
  };

  explicit Callback(const VTable* vtable, void* env)
      : vtable_(vtable), env_(env){};
  Callback() : vtable_(&null_vtable), env_(nullptr) {}
  ~Callback() { vtable_->not_called(env_); }

  Callback(const Callback&) = delete;
  Callback& operator=(const Callback&) = delete;

  Callback(Callback&& other) {
    vtable_ = other.vtable_;
    env_ = other.env_;
    other.vtable_ = &null_vtable;
  }

  Callback& operator=(Callback&& other) {
    vtable_->not_called(env_);
    vtable_ = other.vtable_;
    env_ = other.env_;
    other.vtable_ = &null_vtable;
    return *this;
  }

  void operator()(const Arg& arg) {
    vtable_->call(env_, arg);
    vtable_ = &null_vtable;
  }

 private:
  static void NullVTableNotCalled(void*) {}
  static void NullVTableCall(void*, const Arg&) { abort(); }

  static const VTable null_vtable;

  const VTable* vtable_;
  void* env_;
};

typedef Callback<Status> StatusCallback;
template <class T>
using StatusOrCallback = Callback<StatusOr<T>>;

template <class Arg>
const typename Callback<Arg>::VTable Callback<Arg>::null_vtable = {
    NullVTableCall, NullVTableNotCalled};

// Construct a completion callback from an instance pointer and member function
// Assumes a Ref() and Unref() on the underlying class - these are used to
// ensure the object is alive still when the callback needs to be made
template <class T, class Arg, void (T::*callback)(const Arg& arg)>
class CallbackFromMemberFunction {
 public:
  static Callback<Arg> UponInstance(T* instance) {
    instance->Ref();
    return Callback<Arg>(&vtable_, instance);
  }

 private:
  static void Call(void* env, const Arg& status) {
    auto* instance = static_cast<T*>(env);
    (instance->*callback)(status);
    instance->Unref();
  }
  static void NotCalled(void* env) {
    Call(env, Arg(StatusCode::CANCELLED, __PRETTY_FUNCTION__));
  }

  static const typename Callback<Arg>::VTable vtable_;
};

template <class T, class Arg, void (T::*callback)(const Arg& status)>
const typename Callback<Arg>::VTable
    CallbackFromMemberFunction<T, Arg, callback>::vtable_ = {Call, NotCalled};

template <class T, void (T::*callback)(const Status& status)>
using StatusCallbackFromMemberFunction =
    CallbackFromMemberFunction<T, Status, callback>;

template <class T, class Arg, void (T::*callback)(const StatusOr<Arg>& status)>
using StatusOrCallbackFromMemberFunction =
    CallbackFromMemberFunction<T, StatusOr<Arg>, callback>;

// Construct a completion callback from an instance pointer and member function
// Assumes the callback will keep the underlying object alive itself
template <class T, class Arg, void (T::*callback)(const Arg& arg)>
class CallbackFromMemberFunctionNoRef {
 public:
  static Callback<Arg> UponInstance(T* instance) {
    return Callback<Arg>(&vtable_, instance);
  }

 private:
  static void Call(void* env, const Arg& status) {
    auto* instance = static_cast<T*>(env);
    (instance->*callback)(status);
  }
  static void NotCalled(void* env) {
    Call(env, Arg(StatusCode::CANCELLED, __PRETTY_FUNCTION__));
  }

  static const typename Callback<Arg>::VTable vtable_;
};

template <class T, class Arg, void (T::*callback)(const Arg& status)>
const typename Callback<Arg>::VTable
    CallbackFromMemberFunctionNoRef<T, Arg, callback>::vtable_ = {Call,
                                                                  NotCalled};

template <class T, void (T::*callback)(const Status& status)>
using StatusCallbackFromMemberFunctionNoRef =
    CallbackFromMemberFunctionNoRef<T, Status, callback>;

template <class T, class Arg, void (T::*callback)(const StatusOr<Arg>& status)>
using StatusOrCallbackFromMemberFunctionNoRef =
    CallbackFromMemberFunctionNoRef<T, StatusOr<Arg>, callback>;

}  // namespace overnet
