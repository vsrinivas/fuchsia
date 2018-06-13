// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_APPMGR_HUB_HUB_INFO_H_
#define GARNET_BIN_APPMGR_HUB_HUB_INFO_H_

#include <fbl/string.h>
#include <fs/pseudo-dir.h>

namespace component {

class HubInfo {
 public:
  HubInfo(fbl::String label, fbl::String koid,
          fbl::RefPtr<fs::PseudoDir> hub_dir);
  ~HubInfo();

  const fbl::String& label() const { return label_; }
  const fbl::String& koid() const { return koid_; }
  const fbl::RefPtr<fs::PseudoDir>& hub_dir() const { return hub_dir_; }

 private:
  fbl::String label_;
  fbl::String koid_;
  fbl::RefPtr<fs::PseudoDir> hub_dir_;
};

}  // namespace component

#endif  // GARNET_BIN_APPMGR_HUB_HUB_INFO_H_
