// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_SVC_OUTGOING_H_
#define LIB_SVC_OUTGOING_H_

#include <fuchsia/io/llcpp/fidl.h>
#include <lib/zx/channel.h>

#include <fs/pseudo_dir.h>
#include <fs/service.h>
#include <fs/synchronous_vfs.h>

namespace svc {

class Outgoing {
 public:
  explicit Outgoing(async_dispatcher_t* dispatcher);
  ~Outgoing();

  const fbl::RefPtr<fs::PseudoDir>& root_dir() const { return root_dir_; }
  const fbl::RefPtr<fs::PseudoDir>& svc_dir() const { return svc_dir_; }

  // Start serving the root directory on the given channel.
  zx_status_t Serve(fidl::ServerEnd<::llcpp::fuchsia::io::Directory> dir_server_end);

  // Start serving the root directory on the channel provided to this process at
  // startup as PA_DIRECTORY_REQUEST.
  //
  // Takes ownership of the PA_DIRECTORY_REQUEST startup handle.
  zx_status_t ServeFromStartupInfo();

 private:
  fs::SynchronousVfs vfs_;
  fbl::RefPtr<fs::PseudoDir> root_dir_;
  fbl::RefPtr<fs::PseudoDir> svc_dir_;
};

}  // namespace svc

#endif  // LIB_SVC_OUTGOING_H_
