// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <utils/type_support.h>

namespace utils {

template <typename T>
class RefPtr;

template <typename T>
RefPtr<T> AdoptRef(T* ptr);

// RefPtr<T> holds a reference to an intrusively-refcounted object of type T.
//
// T should be a subclass of utils::RefCounted<>, or something that adheres to
// the
// same contract for AddRef() and Release().
//
// Except for initial construction (see below), this generally adheres to a
// subset of the interface for std::shared_ptr<>. Unlike std::shared_ptr<> this
// type does not support conversions between different pointer types, vending
// weak pointers, introspecting the reference count, or any operations that
// would result in allocating memory (unless T::AddRef or T::Release allocate
// memory).
//
// Construction:  To create a RefPtr around a freshly created object, use the
// AdoptRef free function at the bottom of this header. To construct a RefPtr to
// hold a reference to an object that already exists use the copy or move
// constructor or assignment operator.
template <typename T>
class RefPtr final {
public:
    // Constructors
    constexpr RefPtr() : ptr_(nullptr) {}
    constexpr RefPtr(decltype(nullptr)) : RefPtr() {}
    // Constructs a RefPtr from a pointer that has already been adopted. See
    // AdoptRef() below for constructing the very first RefPtr to an object.
    explicit RefPtr(T* p) : ptr_(p) {
        if (ptr_) ptr_->AddRef();
    }

    // Copy
    RefPtr(const RefPtr& r) : ptr_(r.ptr_) {
        if (ptr_) ptr_->AddRef();
    }

    // Assignment
    RefPtr& operator=(const RefPtr& r) {
        // Ref first so self-assignments work.
        if (r.ptr_) r.ptr_->AddRef();
        T* old = ptr_;
        ptr_ = r.ptr_;
        if (old && old->Release()) delete static_cast<T*>(old);
        return *this;
    }

    // Move
    RefPtr(RefPtr&& r) : ptr_(r.ptr_) {
        r.ptr_ = nullptr;
    }
    // Move assignment
    RefPtr& operator=(RefPtr&& r) {
        utils::move(r).swap(*this);
        return *this;
    }

    ~RefPtr() {
        if (ptr_ && ptr_->Release()) delete static_cast<T*>(ptr_);
    }

    void reset(T* ptr = nullptr) {
        RefPtr(ptr).swap(*this);
    }

    void swap(RefPtr& r) {
        T* p = ptr_;
        ptr_ = r.ptr_;
        r.ptr_ = p;
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

    bool operator ==(const RefPtr<T>& other) const { return ptr_ == other.ptr_; }
    bool operator !=(const RefPtr<T>& other) const { return ptr_ != other.ptr_; }

private:
    template <typename U>
    friend class RefPtr;

    friend RefPtr<T> AdoptRef<T>(T*);

    enum AdoptTag { ADOPT };
    RefPtr(T* ptr, AdoptTag) : ptr_(ptr) {
#if (LK_DEBUGLEVEL > 0)
        ptr_->Adopt();
#endif
    }
    T* ptr_;
};

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

}  // namespace utils
