// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_BINDING_HANDLE_H_
#define SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_BINDING_HANDLE_H_

#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
template <typename T>
class BindingHandle {
 public:
  explicit BindingHandle(fbl::RefPtr<T> ptr) : ptr_(std::move(ptr)) { ZX_DEBUG_ASSERT(ptr_); }
  BindingHandle(const BindingHandle<T>&) = delete;
  BindingHandle(BindingHandle<T>&& other) : ptr_(std::move(other.ptr_)) { other.ptr_ = nullptr; }
  ~BindingHandle() {
    if (ptr_) {
      ptr_->CloseChannel();
    }
  }
  T* get() const { return ptr_.get(); }
  T& operator*() const { return *ptr_; }
  T* operator->() const { return ptr_.get(); }

 private:
  fbl::RefPtr<T> ptr_;
};

#endif  // SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_BINDING_HANDLE_H_
