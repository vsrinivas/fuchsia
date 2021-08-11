// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/appmgr/util.h"

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/channel.h>
#include <lib/zx/clock.h>

#include <string>

#include "src/lib/storage/vfs/cpp/vfs.h"
#include "src/lib/storage/vfs/cpp/vnode.h"

namespace component {

std::string Util::GetLabelFromURL(const std::string& url) {
  size_t last_slash = url.rfind('/');
  if (last_slash == std::string::npos || last_slash + 1 == url.length())
    return url;
  return url.substr(last_slash + 1);
}

ExportedDirChannels Util::BindDirectory(fuchsia::sys::LaunchInfo* launch_info) {
  zx::channel exported_dir_server, exported_dir_client;
  zx_status_t status = zx::channel::create(0u, &exported_dir_server, &exported_dir_client);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to create channel for service directory: status=" << status;
    return {zx::channel(), zx::channel()};
  }

  auto client_request = std::move(launch_info->directory_request);
  launch_info->directory_request = std::move(exported_dir_server);
  return {std::move(exported_dir_client), std::move(client_request)};
}

std::string Util::GetArgsString(const ::fidl::VectorPtr<::std::string>& arguments) {
  std::string args = "";
  if (arguments.has_value() && !arguments->empty()) {
    std::ostringstream buf;
    std::copy(arguments->begin(), arguments->end() - 1,
              std::ostream_iterator<std::string>(buf, " "));
    buf << *arguments->rbegin();
    args = buf.str();
  }
  return args;
}

zx::channel Util::OpenAsDirectory(fs::FuchsiaVfs* vfs, fbl::RefPtr<fs::Vnode> node) {
  zx::channel h1, h2;
  if (zx::channel::create(0, &h1, &h2) != ZX_OK) {
    return zx::channel();
  }
  if (vfs->ServeDirectory(std::move(node), std::move(h1), fs::Rights::ReadWrite()) != ZX_OK) {
    return zx::channel();
  }
  return h2;
}

}  // namespace component
