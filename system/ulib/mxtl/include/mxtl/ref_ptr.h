// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/assert.h>
#include <magenta/compiler.h>
#include <mxtl/deleter.h>
#include <mxtl/type_support.h>

namespace mxtl {

template <typename T, typename Deleter = default_delete<T>>
class RefPtr;

template <typename T, typename Deleter = default_delete<T>>
RefPtr<T, Deleter> AdoptRef(T* ptr);

template <typename T, typename Deleter = default_delete<T>>
RefPtr<T, Deleter> WrapRefPtr(T* ptr);

namespace internal {
template <typename T, typename Deleter = default_delete<T>>
RefPtr<T, Deleter> MakeRefPtrNoAdopt(T* ptr);
} // namespace internal

// RefPtr<T> holds a reference to an intrusively-refcounted object of type
// T that uses the Deleter to clean up when the refcount drops to 0.
//
// T should be a subclass of mxtl::RefCounted<>, or something that adheres to
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
template <typename T, typename Deleter>
class RefPtr final {
public:
    using ObjType = T;
    using DeleterType = Deleter;

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
    template <typename U, typename U_Deleter,
              typename = typename enable_if<is_convertible_pointer<U*, T*>::value>::type>
    RefPtr(const RefPtr<U, U_Deleter>& r) : RefPtr(r.ptr_) {
        static_assert((is_class<T>::value == is_class<U>::value) &&
                     (!is_class<T>::value || has_virtual_destructor<T>::value),
                "Cannot convert unique_ptr<U> to unique_ptr<T> unless neither T "
                "nor U are class/struct types, or T has a virtual destructor");

        static_assert(is_same<U_Deleter, default_delete<U>>::value,
                "Cannot convert RefPtr<U> to RefPtr<T, ...> unless RefPtr<U, ...> is "
                "using default_delete<U>.");

        static_assert(is_same<Deleter, default_delete<T>>::value,
                "Cannot convert RefPtr<U> to RefPtr<T, ...> unless RefPtr<T, ...> is "
                "using default_delete<T>.");
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
            Deleter()(old);
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
    template <typename U, typename U_Deleter,
              typename = typename enable_if<is_convertible_pointer<U*, T*>::value>::type>
    RefPtr(RefPtr<U, U_Deleter>&& r) : ptr_(r.ptr_) {
        static_assert((is_class<T>::value == is_class<U>::value) &&
                     (!is_class<T>::value || has_virtual_destructor<T>::value),
                "Cannot convert unique_ptr<U> to unique_ptr<T> unless neither T "
                "nor U are class/struct types, or T has a virtual destructor");

        static_assert(is_same<U_Deleter, default_delete<U>>::value,
                "Cannot convert RefPtr<U> to RefPtr<T, ...> unless RefPtr<U, ...> is "
                "using default_delete<U>.");

        static_assert(is_same<Deleter, default_delete<T>>::value,
                "Cannot convert RefPtr<U> to RefPtr<T, ...> unless RefPtr<T, ...> is "
                "using default_delete<T>.");

        r.ptr_ = nullptr;
    }

    // Move assignment
    RefPtr& operator=(RefPtr&& r) {
        mxtl::move(r).swap(*this);
        return *this;
    }

    // Construct via explicit downcast.
    // ptr must be the same object as base.ptr_.
    template <typename BaseType, typename BaseDeleter = default_delete<BaseType>>
    RefPtr(T* ptr, RefPtr<BaseType, BaseDeleter>&& base)
        : ptr_(ptr) {
        ASSERT(static_cast<BaseType*>(ptr_) == base.ptr_);
        base.ptr_ = nullptr;
    }

    // Downcast via static method invocation.  Depeding on use case, the syntax
    // should look something like...
    //
    // mxtl::RefPtr<MyBase> foo = MakeBase();
    // auto bar_copy = mxtl::RefPtr<MyDerived>::Downcast(foo);
    // auto bar_move = mxtl::RefPtr<MyDerived>::Downcast(mxtl::move(foo));
    // auto baz_copy = mxtl::RefPtr<MyDerived, CustomDeleter>::Downcast(foo);
    // auto baz_move = mxtl::RefPtr<MyDerived, CustomDeleter>::Downcast(mxtl::move(foo));
    //
    template <typename BaseRefPtr>
    static RefPtr Downcast(BaseRefPtr base) {
        // Make certain that BaseRefPtr is some form of RefPtr<T, Deleter>
        static_assert(is_same<BaseRefPtr, RefPtr<typename BaseRefPtr::ObjType,
                                                 typename BaseRefPtr::DeleterType>>::value,
                     "BaseRefPtr must be a RefPtr<T, Deleter>!");

        if (base != nullptr)
            return internal::MakeRefPtrNoAdopt<T, Deleter>(static_cast<T*>(base.leak_ref()));

        return nullptr;
    }

    ~RefPtr() {
        if (ptr_ && ptr_->Release()) {
            Deleter()(ptr_);
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

    bool operator==(const RefPtr<T, Deleter>& other) const { return ptr_ == other.ptr_; }
    bool operator!=(const RefPtr<T, Deleter>& other) const { return ptr_ != other.ptr_; }

private:
    template <typename U, typename D>
    friend class RefPtr;
    friend RefPtr<T, Deleter> AdoptRef<T, Deleter>(T*);
    friend RefPtr<T, Deleter> AdoptRef<T, Deleter>(T*);
    friend RefPtr<T, Deleter> internal::MakeRefPtrNoAdopt<T, Deleter>(T*);

    enum AdoptTag { ADOPT };
    enum NoAdoptTag { NO_ADOPT };

    RefPtr(T* ptr, AdoptTag)
        : ptr_(ptr) {
#if (LK_DEBUGLEVEL > 1)
        ptr_->Adopt();
#endif
    }

    RefPtr(T* ptr, NoAdoptTag)
        : ptr_(ptr) {}

    T* ptr_;
};

// Comparison against nullptr operator (of the form, nullptr == myptr)
template <typename T, typename Deleter>
static inline bool operator==(decltype(nullptr), const RefPtr<T, Deleter>& ptr) {
    return (ptr.get() == nullptr);
}

template <typename T, typename Deleter>
static inline bool operator!=(decltype(nullptr), const RefPtr<T, Deleter>& ptr) {
    return (ptr.get() != nullptr);
}

// Constructs a RefPtr from a fresh object that has not been referenced before.
// Use like:
//
//   RefPtr<Happy> h = AdoptRef(new Happy);
//   if (!h)
//      // Deal with allocation failure here
//   h->DoStuff();
template <typename T, typename Deleter>
inline RefPtr<T, Deleter> AdoptRef(T* ptr) {
    return RefPtr<T, Deleter>(ptr, RefPtr<T, Deleter>::ADOPT);
}

// Convenience wrappers to construct a RefPtr with argument type deduction.
template <typename T, typename Deleter>
inline RefPtr<T, Deleter> WrapRefPtr(T* ptr) {
    return RefPtr<T, Deleter>(ptr);
}

namespace internal {
// Constructs a RefPtr from a T* without attempt to either AddRef or Adopt the
// pointer.  Used by the internals of some intrusive container classes to store
// sentinels (special invalid pointers) in RefPtr<>s.
template <typename T, typename Deleter>
inline RefPtr<T, Deleter> MakeRefPtrNoAdopt(T* ptr) {
    return RefPtr<T, Deleter>(ptr, RefPtr<T, Deleter>::NO_ADOPT);
}
} // namespace internal

} // namespace mxtl
