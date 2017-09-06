// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "escher/base/typed_reffable.h"

namespace escher {

template <typename OwnableT, typename TypeInfoT>
class Owner;

// An Ownable may optionally have an Owner; when |owner_| is non-null it is said
// to be "owned", otherwise it is "unowned".  If an Ownable is unowned when its
// ref-count becomes zero, it is immediately destroyed.  Otherwise, its Owner
// becomes responsible for the lifecycle of the Ownable.  Different owners will
// implement different strategies, e.g. one might defer destruction until a safe
// time, while another might recycle the object by returning it to a pool.
template <typename OwnableT, typename TypeInfoT>
class Ownable : public TypedReffable<TypeInfoT> {
 public:
  typedef TypeInfoT TypeInfo;

  ~Ownable() override {
    if (owner_) {
      owner_->DecrementOwnableCount();
    }
  }

  Owner<OwnableT, TypeInfoT>* owner() const { return owner_; }

 protected:
  Ownable() = default;

 private:
  friend class Owner<OwnableT, TypeInfoT>;
  void set_owner(Owner<OwnableT, TypeInfoT>* owner) { owner_ = owner; }

  // If |owner| is null, returns true so that the Ownable is immediately
  // destroyed.  Otherwise, returns false; destruction of the Ownable is now
  // the responsiblity of the |owner_|, which is notified via ReceiveOwnable().
  bool OnZeroRefCount() final override {
    if (owner_) {
      // FTL_DCHECK(this->IsKindOf<OwnableT>());
      owner_->OnReceiveOwnable(
          std::unique_ptr<OwnableT>(static_cast<OwnableT*>(this)));
      return false;
    } else {
      // No owner: destroy immediately.
      return true;
    }
  }

  Owner<OwnableT, TypeInfoT>* owner_ = nullptr;
};

}  // namespace escher
