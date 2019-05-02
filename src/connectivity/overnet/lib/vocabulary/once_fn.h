// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <assert.h>
#include <stddef.h>
#include <cstddef>
#include <memory>

namespace overnet {

template <class F, size_t kStorage = sizeof(void*)>
class OnceFn;

namespace once_fn_detail {

// Use a (manually constructed) vtable to encode what to do when a callback is
// made (or not)
// This has been observed to generate more efficient code than writing out a
// C++ style vtable
template <class R, class... Arg>
struct VTable {
  // Returns the size of the stored value
  size_t (*move)(void* dst, void* src, size_t dst_size);
  R (*call)(void* env, Arg&&... arg);
  void (*not_called)(void* env);
};

template <class F, class R, class... Arg>
class SmallFunctor {
 public:
  static const VTable<R, Arg...> vtable;
  static void InitEnv(void* env, F&& f) { new (env) F(std::forward<F>(f)); }

 private:
  static size_t Move(void* dst, void* src, size_t dst_size) {
    assert(dst_size >= sizeof(F));
    F* f = static_cast<F*>(src);
    new (dst) F(std::move(*f));
    f->~F();
    return sizeof(F);
  }
  static R Call(void* env, Arg&&... arg) {
    F* f = static_cast<F*>(env);
    R r = (*f)(std::forward<Arg>(arg)...);
    f->~F();
    return r;
  }
  static void NotCalled(void* env) {
    F* f = static_cast<F*>(env);
    f->~F();
  }
};

template <class F, class... Arg>
class SmallFunctor<F, void, Arg...> {
 public:
  static const VTable<void, Arg...> vtable;
  static void InitEnv(void* env, F&& f) { new (env) F(std::forward<F>(f)); }

 private:
  static size_t Move(void* dst, void* src, size_t dst_size) {
    assert(dst_size >= sizeof(F));
    F* f = static_cast<F*>(src);
    new (dst) F(std::move(*f));
    f->~F();
    return sizeof(F);
  }
  static void Call(void* env, Arg&&... arg) {
    F* f = static_cast<F*>(env);
    (*f)(std::forward<Arg>(arg)...);
    f->~F();
  }
  static void NotCalled(void* env) {
    F* f = static_cast<F*>(env);
    f->~F();
  }
};

template <class F, class R, class... Arg>
const VTable<R, Arg...> SmallFunctor<F, R, Arg...>::vtable = {Move, Call,
                                                              NotCalled};

template <class F, class... Arg>
const VTable<void, Arg...> SmallFunctor<F, void, Arg...>::vtable = {Move, Call,
                                                                    NotCalled};

template <class F, class A, class R, class... Arg>
class SmallMustCall {
 public:
  typedef std::tuple<F, A> Rep;

  static const VTable<R, Arg...> vtable;
  static void InitEnv(void* env, F&& f, A&& a) {
    new (env) Rep(std::forward<F>(f), std::forward<A>(a));
  }

 private:
  static size_t Move(void* dst, void* src, size_t dst_size) {
    assert(dst_size >= sizeof(Rep));
    Rep* rep = static_cast<Rep*>(src);
    new (dst) Rep(std::move(*rep));
    rep->~Rep();
    return sizeof(Rep);
  }
  static R Call(void* env, Arg&&... arg) {
    Rep* rep = static_cast<Rep*>(env);
    R r = std::get<0>(*rep)(std::forward<Arg>(arg)...);
    rep->~Rep();
    return r;
  }
  static void NotCalled(void* env) {
    Rep* rep = static_cast<Rep*>(env);
    std::apply(std::get<0>(*rep), std::get<1>(*rep)());
    rep->~Rep();
  }
};

template <class F, class A, class... Arg>
class SmallMustCall<F, A, void, Arg...> {
 public:
  typedef std::tuple<F, A> Rep;

  static const VTable<void, Arg...> vtable;
  static void InitEnv(void* env, F&& f, A&& a) {
    new (env) Rep(std::forward<F>(f), std::forward<A>(a));
  }

 private:
  static size_t Move(void* dst, void* src, size_t dst_size) {
    assert(dst_size >= sizeof(Rep));
    Rep* rep = static_cast<Rep*>(src);
    new (dst) Rep(std::move(*rep));
    rep->~Rep();
    return sizeof(Rep);
  }
  static void Call(void* env, Arg&&... arg) {
    Rep* rep = static_cast<Rep*>(env);
    std::get<0> (*rep)(std::forward<Arg>(arg)...);
    rep->~Rep();
  }
  static void NotCalled(void* env) {
    Rep* rep = static_cast<Rep*>(env);
    std::apply(std::get<0>(*rep), std::get<1>(*rep)());
    rep->~Rep();
  }
};

template <class F, class A, class R, class... Arg>
const VTable<R, Arg...> SmallMustCall<F, A, R, Arg...>::vtable = {Move, Call,
                                                                  NotCalled};

template <class F, class A, class... Arg>
const VTable<void, Arg...> SmallMustCall<F, A, void, Arg...>::vtable = {
    Move, Call, NotCalled};

template <class R, class... Arg>
struct NullVTable {
  static size_t Move(void*, void*, size_t) { return 0; }
  static void NotCalled(void*) {}
  static R Call(void*, Arg&&...) { abort(); }

  static const VTable<R, Arg...> vtable;
};

template <class R, class... Arg>
const VTable<R, Arg...> NullVTable<R, Arg...>::vtable = {Move, Call, NotCalled};

}  // namespace once_fn_detail

// Marker to designate a function that *must* be called
enum MustCall { MUST_CALL };

// Function that is called at most once
template <class R, class... Arg, size_t kStorage>
class OnceFn<R(Arg...), kStorage> {
 public:
  OnceFn() : vtable_(&once_fn_detail::NullVTable<R, Arg...>::vtable) {}
  ~OnceFn() { vtable_->not_called(&env_); }

  template <class F,
            typename = typename std::enable_if<sizeof(F) <= kStorage>::type>
  OnceFn(F&& f) {
    vtable_ = &once_fn_detail::SmallFunctor<F, R, Arg...>::vtable;
    once_fn_detail::SmallFunctor<F, R, Arg...>::InitEnv(&env_,
                                                        std::forward<F>(f));
  }

  template <class F, class A,
            typename = typename std::enable_if<sizeof(std::tuple<F, A>) <=
                                               kStorage>::type>
  OnceFn(MustCall, F&& f, A&& a) {
    vtable_ = &once_fn_detail::SmallMustCall<F, A, R, Arg...>::vtable;
    once_fn_detail::SmallMustCall<F, A, R, Arg...>::InitEnv(
        &env_, std::forward<F>(f), std::forward<A>(a));
  }

  template <class F>
  OnceFn(MustCall, F&& f)
      : OnceFn(MUST_CALL, std::forward<F>(f),
               []() { return std::tuple<Arg...>(); }) {}

  OnceFn(const OnceFn&) = delete;
  OnceFn& operator=(const OnceFn&) = delete;

  OnceFn(OnceFn&& other) {
    vtable_ = other.vtable_;
    other.vtable_ = &once_fn_detail::NullVTable<R, Arg...>::vtable;
    vtable_->move(&env_, &other.env_, kStorage);
  }

  OnceFn& operator=(OnceFn&& other) {
    vtable_->not_called(&env_);
    vtable_ = other.vtable_;
    other.vtable_ = &once_fn_detail::NullVTable<R, Arg...>::vtable;
    vtable_->move(&env_, &other.env_, kStorage);
    return *this;
  }

  R operator()(Arg... arg) {
    const auto* const vtable = vtable_;
    vtable_ = &once_fn_detail::NullVTable<R, Arg...>::vtable;
    return vtable->call(&env_, std::forward<Arg>(arg)...);
  }

  bool empty() const {
    return vtable_ == &once_fn_detail::NullVTable<R, Arg...>::vtable;
  }

  // A mutator is a functor of the form:
  // mutator(fn, args...) -> result
  // where fn(args...) -> result is the current function
  // The addition of a mutator occurs in place, and abort()s if env_ is not
  // sufficiently large for the new combined functor
  template <class Mutator>
  void AddMutator(Mutator mutator);

 private:
  const once_fn_detail::VTable<R, Arg...>* vtable_;
  typename std::aligned_storage<kStorage>::type env_;
};

namespace once_fn_detail {

template <size_t kStorage, class F, class R, class... Arg>
struct MutatedEnv {
  static const VTable<R, Arg...> vtable;
  struct alignas(alignof(std::max_align_t)) Header {
    const VTable<R, Arg...>* wrapped_vtable;
    size_t wrapped_size;
    F fn;
  };
  Header hdr;
  typename std::aligned_storage<kStorage - sizeof(Header)>::type storage;

  MutatedEnv(const VTable<R, Arg...>* vtable, size_t size, F fn)
      : hdr{vtable, size, std::move(fn)} {}

  static size_t Move(void* dst, void* src, size_t dst_size) {
    MutatedEnv* s = static_cast<MutatedEnv*>(src);
    MutatedEnv* d = static_cast<MutatedEnv*>(dst);
    new (&d->hdr) Header(std::move(s->hdr));
    s->hdr.~Header();
    return sizeof(Header) + s->hdr.wrapped_vtable->move(
                                &d->storage, &s->storage, sizeof(s->storage));
  }

  static R Call(void* env, Arg&&... arg) {
    MutatedEnv* e = static_cast<MutatedEnv*>(env);
    auto vt = e->hdr.wrapped_vtable;
    e->hdr.wrapped_vtable = &NullVTable<R, Arg...>::vtable;
    auto r = e->hdr.fn(
        [e, vt](Arg&&... args) mutable {
          auto v = vt;
          vt = &NullVTable<R, Arg...>::vtable;
          return v->call(&e->storage, std::forward<Arg>(args)...);
        },
        std::forward<Arg>(arg)...);
    e->hdr.~Header();
    return r;
  }

  static void NotCalled(void* env) {
    MutatedEnv* e = static_cast<MutatedEnv*>(env);
    e->hdr.wrapped_vtable->not_called(&e->storage);
    e->hdr.~Header();
  }
};

template <size_t kStorage, class F, class R, class... Arg>
const VTable<R, Arg...> MutatedEnv<kStorage, F, R, Arg...>::vtable = {
    Move, Call, NotCalled};

template <size_t kStorage, class F, class... Arg>
struct MutatedEnv<kStorage, F, void, Arg...> {
  static const VTable<void, Arg...> vtable;
  struct alignas(alignof(std::max_align_t)) Header {
    const VTable<void, Arg...>* wrapped_vtable;
    size_t wrapped_size;
    F fn;
  };
  Header hdr;
  typename std::aligned_storage<kStorage - sizeof(Header)>::type storage;

  MutatedEnv(const VTable<void, Arg...>* vtable, size_t size, F fn)
      : hdr{vtable, size, std::move(fn)} {}

  static size_t Move(void* dst, void* src, size_t dst_size) {
    MutatedEnv* s = static_cast<MutatedEnv*>(src);
    MutatedEnv* d = static_cast<MutatedEnv*>(dst);
    new (&d->hdr) Header(std::move(s->hdr));
    s->hdr.~Header();
    return sizeof(Header) + s->hdr.wrapped_vtable->move(
                                &d->storage, &s->storage, sizeof(s->storage));
  }

  static void Call(void* env, Arg&&... arg) {
    MutatedEnv* e = static_cast<MutatedEnv*>(env);
    auto vt = e->hdr.wrapped_vtable;
    e->hdr.wrapped_vtable = &NullVTable<void, Arg...>::vtable;
    e->hdr.fn(
        [e, vt](Arg&&... args) mutable {
          auto v = vt;
          vt = &NullVTable<void, Arg...>::vtable;
          v->call(&e->storage, std::forward<Arg>(args)...);
        },
        std::forward<Arg>(arg)...);
    e->hdr.~Header();
  }

  static void NotCalled(void* env) {
    MutatedEnv* e = static_cast<MutatedEnv*>(env);
    e->hdr.wrapped_vtable->not_called(&e->storage);
    e->hdr.~Header();
  }
};

template <size_t kStorage, class F, class... Arg>
const VTable<void, Arg...> MutatedEnv<kStorage, F, void, Arg...>::vtable = {
    Move, Call, NotCalled};

}  // namespace once_fn_detail

template <class R, class... Args, size_t kStorage>
template <class Mutator>
void OnceFn<R(Args...), kStorage>::AddMutator(Mutator mutator) {
  typename std::aligned_storage<kStorage>::type temp;
  size_t size = vtable_->move(&temp, &env_, kStorage);
  using MEnv = once_fn_detail::MutatedEnv<kStorage, Mutator, R, Args...>;
  static_assert(sizeof(MEnv) <= kStorage, "Math wrong for MEnv storage");
  MEnv* p = new (&env_) MEnv(vtable_, size, std::move(mutator));
  vtable_->move(&p->storage, &temp, sizeof(p->storage));
  vtable_ = &MEnv::vtable;
}

}  // namespace overnet
