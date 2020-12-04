// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FSHOST_FSHOST_OPTIONS_H_
#define SRC_STORAGE_FSHOST_FSHOST_OPTIONS_H_

namespace devmgr {

struct FshostOptions {
  // Identifies that only partition containers should be initialized.
  bool netboot = false;

  // Identifies that filesystems should be verified before being mounted.
  bool check_filesystems = false;

  // Identifies that the block watcher should wait for a "data" partition to appear before choosing
  // to launch pkgfs.
  bool wait_for_data = false;
};

}  // namespace devmgr

#endif  // SRC_STORAGE_FSHOST_FSHOST_OPTIONS_H_
