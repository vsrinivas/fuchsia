// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_APPMGR_CONSTANTS_H_
#define SRC_SYS_APPMGR_CONSTANTS_H_

namespace component {

// The appmgr directory for all isolated persistent storage.
// Storage watchdog will measure and react to this directory.
constexpr char kRootDataDir[] = "/data";

// The appmgr directory for all isolated cache storage.
// Storage watchdog will clear this when disk is full.
constexpr char kRootCacheDir[] = "/data/cache";

// The appmgr directory for all isolated temp storage.
constexpr char kRootTempDir[] = "/tmp";

}  // namespace component

#endif  // SRC_SYS_APPMGR_CONSTANTS_H_
