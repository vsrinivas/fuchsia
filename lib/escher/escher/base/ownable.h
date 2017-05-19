// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "escher/base/typed_reffable.h"

namespace escher {

template <typename TypeInfoT>
class Owner;

// An Ownable may have an Owner.  If the Owner is non-null when the Ownable's
// ref-count becomes zero, it becomes responsible for eventually destroying the
// object (see OnZeroRefCount(), below).
template <typename TypeInfoT>
class Ownable : public TypedReffable<TypeInfoT> {
 public:
  typedef TypeInfoT TypeInfo;

  ~Ownable() override {
    if (owner_) {
      owner_->DecrementOwnableCount();
    }
  }

  Owner<TypeInfo>* owner() const { return owner_; }

 protected:
  Ownable() = default;

 private:
  friend class Owner<TypeInfo>;
  void set_owner(Owner<TypeInfo>* owner) { owner_ = owner; }

  // If |owner| is null, returns true so that the Ownable is immediately
  // destroyed.  Otherwise, returns false; destruction of the Ownable is now
  // the responsiblity of the |owner_|, which is notified via ReceiveOwnable().
  bool OnZeroRefCount() final override {
    if (owner_) {
      owner_->OnReceiveOwnable(std::unique_ptr<Ownable<TypeInfo>>(this));
      return false;
    } else {
      // No owner: destroy immediately.
      return true;
    }
  }

  Owner<TypeInfo>* owner_ = nullptr;
};

}  // namespace escher
