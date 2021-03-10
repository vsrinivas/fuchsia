// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "trait_updater.h"

namespace nl::Weave::DeviceLayer {

TraitUpdaterImpl TraitUpdaterImpl::sInstance;

WEAVE_ERROR TraitUpdaterImpl::Init() {
  WEAVE_ERROR err = delegate_->Init();
  if (err != WEAVE_NO_ERROR) {
    return err;
  }

  return PlatformMgrImpl().AddEventHandler(TrampolineEvent, reinterpret_cast<intptr_t>(this));
}

void TraitUpdaterImpl::TrampolineEvent(const WeaveDeviceEvent* event, intptr_t arg) {
  TraitUpdaterImpl* self = reinterpret_cast<TraitUpdaterImpl*>(arg);
  self->HandleWeaveDeviceEvent(event);
}

void TraitUpdaterImpl::HandleWeaveDeviceEvent(const WeaveDeviceEvent* event) {
  delegate_->HandleWeaveDeviceEvent(event);
}

void TraitUpdaterImpl::SetDelegate(std::unique_ptr<Delegate> delegate) {
  FX_CHECK(!(delegate && delegate_)) << "Attempt to set an already set delegate. Must explicitly "
                                        "clear the existing delegate first.";
  delegate_ = std::move(delegate);
  if (delegate_) {
    delegate_->SetTraitUpdaterImpl(this);
  }
}

TraitUpdaterImpl::Delegate* TraitUpdaterImpl::GetDelegate() { return delegate_.get(); }

}  // namespace nl::Weave::DeviceLayer
