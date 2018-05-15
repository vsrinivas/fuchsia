// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/hub_holder.h"

#include <fbl/ref_ptr.h>
#include <fs/pseudo-file.h>
#include <fs/vnode.h>

namespace component {

HubHolder::HubHolder(fbl::RefPtr<fs::PseudoDir> root) : root_dir_(root) {}

zx_status_t HubHolder::AddEntry(fbl::String name, fbl::RefPtr<fs::Vnode> vn) {
  return root_dir_->AddEntry(fbl::move(name), fbl::move(vn));
}

zx_status_t HubHolder::AddEntry(fbl::String name, fbl::String value) {
  return root_dir_->AddEntry(
      fbl::move(name), fbl::move(fbl::AdoptRef(new fs::UnbufferedPseudoFile(
                           [value](fbl::String* output) {
                             *output = value;
                             return ZX_OK;
                           }))));
}

}  // namespace component
