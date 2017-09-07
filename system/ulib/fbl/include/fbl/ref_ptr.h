// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/assert.h>
#include <magenta/compiler.h>
#include <fbl/recycler.h>
#include <fbl/type_support.h>

#if _KERNEL
// Need the full type for the kernel's fbl::Mutex::IsHeld().
#include <fbl/mutex.h>
#else
namespace fbl {
class Mutex;
}
#endif

namespace fbl {

template <typename T>
class RefPtr;

template <typename T>
RefPtr<T> AdoptRef(T* ptr);

template <typename T>
RefPtr<T> WrapRefPtr(T* ptr);

namespace internal {
template <typename T>
RefPtr<T> MakeRefPtrNoAdopt(T* ptr);

template <typename T>
RefPtr<T> MakeRefPtrUpgradeFromRaw(T* ptr, const Mutex& lock);
} // namespace internal

// RefPtr<T> holds a reference to an intrusively-refcounted object of type
// T that deletes the object when the refcount drops to 0.
//
// T should be a subclass of fbl::RefCounted<>, or something that adheres to
// the same contract for AddRef() and Release().
//
// Except for initial construction (see below), this generally adheres to a
// subset of the interface for std::shared_ptr<>. Unlike std::shared_ptr<> this
// type does not support vending weak pointers, introspecting the reference
// count, or any operations that would result in allocating memory (unless
// T::AddRef or T::Release allocate memory).
//
// Construction:  To create a RefPtr around a freshly created object, use the
// AdoptRef free function at the bottom of this header. To construct a RefPtr
// to hold a reference to an object that already exists use the copy or move
// constructor or assignment operator.
template <typename T>
class RefPtr final {
public:
    using ObjType = T;

    // Constructors
    constexpr RefPtr()
        : ptr_(nullptr) {}
    constexpr RefPtr(decltype(nullptr))
        : RefPtr() {}
    // Constructs a RefPtr from a pointer that has already been adopted. See
    // AdoptRef() below for constructing the very first RefPtr to an object.
    explicit RefPtr(T* p)
        : ptr_(p) {
        if (ptr_)
            ptr_->AddRef();
    }

    // Copy construction.
    RefPtr(const RefPtr& r)
        : RefPtr(r.ptr_) {}

    // Implicit upcast via copy construction.
    //
    // @see the notes in unique_ptr.h
    template <typename U,
              typename = typename enable_if<is_convertible_pointer<U*, T*>::value>::type>
    RefPtr(const RefPtr<U>& r) : RefPtr(r.ptr_) {
        static_assert((is_class<T>::value == is_class<U>::value) &&
                      (!is_class<T>::value ||
                       has_virtual_destructor<T>::value ||
                       is_same<T, const U>::value),
                "Cannot convert RefPtr<U> to RefPtr<T> unless neither T "
                "nor U are class/struct types, or T has a virtual destructor,"
                "or T == const U.");
    }

    // Assignment
    RefPtr& operator=(const RefPtr& r) {
        // Ref first so self-assignments work.
        if (r.ptr_) {
            r.ptr_->AddRef();
        }
        T* old = ptr_;
        ptr_ = r.ptr_;
        if (old && old->Release()) {
            recycle(old);
        }
        return *this;
    }

    // Move construction
    RefPtr(RefPtr&& r)
        : ptr_(r.ptr_) {
        r.ptr_ = nullptr;
    }

    // Implicit upcast via move construction.
    //
    // @see the notes in RefPtr.h
    template <typename U,
              typename = typename enable_if<is_convertible_pointer<U*, T*>::value>::type>
    RefPtr(RefPtr<U>&& r) : ptr_(r.ptr_) {
        static_assert((is_class<T>::value == is_class<U>::value) &&
                      (!is_class<T>::value ||
                       has_virtual_destructor<T>::value ||
                       is_same<T, const U>::value),
                "Cannot convert RefPtr<U> to RefPtr<T> unless neither T "
                "nor U are class/struct types, or T has a virtual destructor,"
                "or T == const U");

        r.ptr_ = nullptr;
    }

    // Move assignment
    RefPtr& operator=(RefPtr&& r) {
        fbl::move(r).swap(*this);
        return *this;
    }

    // Construct via explicit downcast.
    // ptr must be the same object as base.ptr_.
    template <typename BaseType>
    RefPtr(T* ptr, RefPtr<BaseType>&& base)
        : ptr_(ptr) {
        MX_ASSERT(static_cast<BaseType*>(ptr_) == base.ptr_);
        base.ptr_ = nullptr;
    }

    // Downcast via static method invocation.  Depending on use case, the syntax
    // should look something like...
    //
    // fbl::RefPtr<MyBase> foo = MakeBase();
    // auto bar_copy = fbl::RefPtr<MyDerived>::Downcast(foo);
    // auto bar_move = fbl::RefPtr<MyDerived>::Downcast(fbl::move(foo));
    //
    template <typename BaseRefPtr>
    static RefPtr Downcast(BaseRefPtr base) {
        // Make certain that BaseRefPtr is some form of RefPtr<T>
        static_assert(is_same<BaseRefPtr, RefPtr<typename BaseRefPtr::ObjType>>::value,
                      "BaseRefPtr must be a RefPtr<T>!");

        if (base != nullptr)
            return internal::MakeRefPtrNoAdopt<T>(static_cast<T*>(base.leak_ref()));

        return nullptr;
    }

    ~RefPtr() {
        if (ptr_ && ptr_->Release()) {
            recycle(ptr_);
        }
    }

    void reset(T* ptr = nullptr) {
        RefPtr(ptr).swap(*this);
    }

    void swap(RefPtr& r) {
        T* p = ptr_;
        ptr_ = r.ptr_;
        r.ptr_ = p;
    }

    T* leak_ref() __WARN_UNUSED_RESULT {
        T* p = ptr_;
        ptr_ = nullptr;
        return p;
    }

    T* get() const {
        return ptr_;
    }
    T& operator*() const {
        return *ptr_;
    }
    T* operator->() const {
        return ptr_;
    }
    explicit operator bool() const {
        return !!ptr_;
    }

    // Comparison against nullptr operators (of the form, myptr == nullptr).
    bool operator==(decltype(nullptr)) const { return (ptr_ == nullptr); }
    bool operator!=(decltype(nullptr)) const { return (ptr_ != nullptr); }

    bool operator==(const RefPtr<T>& other) const { return ptr_ == other.ptr_; }
    bool operator!=(const RefPtr<T>& other) const { return ptr_ != other.ptr_; }

private:
    template <typename U>
    friend class RefPtr;
    friend RefPtr<T> AdoptRef<T>(T*);
    friend RefPtr<T> internal::MakeRefPtrNoAdopt<T>(T*);
    friend RefPtr<T> internal::MakeRefPtrUpgradeFromRaw<T>(T*, const Mutex&);

    enum AdoptTag { ADOPT };
    enum NoAdoptTag { NO_ADOPT };

    RefPtr(T* ptr, AdoptTag)
        : ptr_(ptr) {
        if (ptr_) {
            ptr_->Adopt();
        }
    }

    RefPtr(T* ptr, NoAdoptTag)
        : ptr_(ptr) {}

    RefPtr(T* ptr, const Mutex& dtor_lock)
        : ptr_(ptr->AddRefMaybeInDestructor() ? ptr : nullptr) {
#if _KERNEL
        MX_DEBUG_ASSERT_MSG(dtor_lock.IsHeld(), "Lock must be held while using this ctor\n");
#endif
    }

    static void recycle(T* ptr) {
        if (::fbl::internal::has_fbl_recycle<T>::value) {
            ::fbl::internal::recycler<T>::recycle(ptr);
        } else {
            delete ptr;
        }
    }

    T* ptr_;
};

// Comparison against nullptr operator (of the form, nullptr == myptr)
template <typename T>
static inline bool operator==(decltype(nullptr), const RefPtr<T>& ptr) {
    return (ptr.get() == nullptr);
}

template <typename T>
static inline bool operator!=(decltype(nullptr), const RefPtr<T>& ptr) {
    return (ptr.get() != nullptr);
}

// Constructs a RefPtr from a fresh object that has not been referenced before.
// Use like:
//
//   RefPtr<Happy> h = AdoptRef(new Happy);
//   if (!h)
//      // Deal with allocation failure here
//   h->DoStuff();
template <typename T>
inline RefPtr<T> AdoptRef(T* ptr) {
    return RefPtr<T>(ptr, RefPtr<T>::ADOPT);
}

// Convenience wrappers to construct a RefPtr with argument type deduction.
template <typename T>
inline RefPtr<T> WrapRefPtr(T* ptr) {
    return RefPtr<T>(ptr);
}

namespace internal {
// Constructs a RefPtr from a T* without attempt to either AddRef or Adopt the
// pointer.  Used by the internals of some intrusive container classes to store
// sentinels (special invalid pointers) in RefPtr<>s.
template <typename T>
inline RefPtr<T> MakeRefPtrNoAdopt(T* ptr) {
    return RefPtr<T>(ptr, RefPtr<T>::NO_ADOPT);
}

// Constructs a RefPtr from a raw T* which is being held alive by RefPtr
// with the caveat that the existing RefPtr might be in the process of
// destructing the T object. When the T object is in the destructor, the
// resulting RefPtr is null, otherwise the resulting RefPtr points to T*
// with the updated reference count.
//
// The only way for this to be a valid pattern is that the call is made
// while holding |lock| and that the same lock also is used to protect the
// value of T* .
//
// This pattern is needed in collaborating objects which cannot hold a
// RefPtr to each other because it would cause a reference cycle. Instead
// there is a raw pointer from one to the other and a RefPtr in the
// other direction. When needed the raw pointer can be upgraded via
// MakeRefPtrUpgradeFromRaw() and operated outside |lock|.
//
// For example:
//
//  class Holder: public RefCounted<Holder> {
//  public:
//      void add_client(Client* c) {
//          Autolock al(&lock_);
//          client_ = c;
//      }
//
//      void remove_client() {
//          Autolock al(&lock_);
//          client_ = nullptr;
//      }
//
//      void PassClient(Bar* bar) {
//          Autolock al(&lock_);
//          if (client_) {
//              auto rc_client = fbl::internal::MakeRefPtrUpgradeFromRaw(client_, lock_);
//              if (rc_client)
//                  bar->Client(move(rc_client));  // Bar might keep a ref to client.
//              else
//                  bar->OnNoClient();
//          }
//      }
//
//  private:
//      fbl::Mutex lock_;
//      Client* client_;
//  };
//
//  class Client: public RefCounted<Client> {
//  public:
//      Client(RefPtr<Holder> holder) : holder_(move(holder)) {
//          holder_->add_client(this);
//      }
//
//      ~Client() {
//          holder_->remove_client();
//      }
//  private:
//      RefPtr<Holder> holder_;
//  };
//
//
template <typename T>
inline RefPtr<T> MakeRefPtrUpgradeFromRaw(T* ptr, const Mutex& lock) {
    return RefPtr<T>(ptr, lock);
}

} // namespace internal

} // namespace fbl
