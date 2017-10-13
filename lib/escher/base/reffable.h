// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstdint>

#include "escher/base/make.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/memory/ref_ptr.h"

namespace escher {

// Reffable is a non-threadsafe ref-counted base class that is suitable for use
// with fxl::RefPtr.  It provides a virtual OnZeroRefCount() method that
// subclasses can use to avoid immediate destruction when their ref-count
// becomes zero.
//
// Use this class similarly to fxl::RefCountedThreadSafe.  For example, instead
// of:
//    class Foo : public RefCountedThreadSafe<Foo> { ...
// simply say:
//    class Foo : public Reffable { ...
//
// Other than thread-safety, the main difference from RefCountedThreadSafe is
// that Reffable allows subclasses to defer destruction by overriding
// OnZeroRefCount(); see below.
class Reffable {
 public:
  Reffable() = default;
  virtual ~Reffable();

  // Return the number of references to this object.
  uint32_t ref_count() const { return ref_count_; }

 protected:
  // Return true if the object should be destroyed immediately, or false if its
  // destruction should be deferred.  Subclass that override this method to
  // return false are responsible for ensuring that the object is eventually
  // destroyed.
  virtual bool OnZeroRefCount() { return true; }

 private:
  template <typename T>
  friend class fxl::RefPtr;

  // Called by fxl::RefPtr.
  void Release() {
    if (--ref_count_ == 0) {
      if (OnZeroRefCount()) {
        delete this;
      }
    }
  }

  // Called by fxl::RefPtr.
  void AddRef() const {
#ifndef NDEBUG
    FXL_DCHECK(!adoption_required_);
#endif
    ++ref_count_;
  }

  mutable uint32_t ref_count_ = 1;

#ifndef NDEBUG
  // Called by fxl::RefPtr, but only in debug builds.
  template <typename U>
  friend fxl::RefPtr<U> fxl::AdoptRef(U*);
  void Adopt();
  bool adoption_required_ = true;
#endif

  FXL_DISALLOW_COPY_AND_ASSIGN(Reffable);
};

}  // namespace escher
