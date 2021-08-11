// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_APPMGR_UTIL_H_
#define SRC_SYS_APPMGR_UTIL_H_

#include <fuchsia/sys/cpp/fidl.h>

#include <string>

#include "src/lib/storage/vfs/cpp/fuchsia_vfs.h"

namespace component {

struct ExportedDirChannels {
  // The client side of the channel serving connected application's exported
  // dir.
  zx::channel exported_dir;

  // The server side of our client's
  // |fuchsia::sys::LaunchInfo.directory_request|.
  zx::channel client_request;
};

class Util {
 public:
  static std::string GetLabelFromURL(const std::string& url);

  static ExportedDirChannels BindDirectory(fuchsia::sys::LaunchInfo* launch_info);

  static std::string GetArgsString(const ::fidl::VectorPtr<::std::string>& arguments);

  static zx::channel OpenAsDirectory(fs::FuchsiaVfs* vfs, fbl::RefPtr<fs::Vnode> node);
};

}  // namespace component

#endif  // SRC_SYS_APPMGR_UTIL_H_
