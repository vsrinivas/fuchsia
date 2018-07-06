// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/util.h"

#include <fs/vfs.h>
#include <fs/vnode.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <zx/channel.h>

#include <string>

#include "lib/fxl/logging.h"

namespace component {

std::string Util::GetLabelFromURL(const std::string& url) {
  size_t last_slash = url.rfind('/');
  if (last_slash == std::string::npos || last_slash + 1 == url.length())
    return url;
  return url.substr(last_slash + 1);
}

ExportedDirChannels Util::BindDirectory(fuchsia::sys::LaunchInfo* launch_info) {
  zx::channel exported_dir_server, exported_dir_client;
  zx_status_t status =
      zx::channel::create(0u, &exported_dir_server, &exported_dir_client);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create channel for service directory: status="
                   << status;
    return {zx::channel(), zx::channel()};
  }

  auto client_request = std::move(launch_info->directory_request);
  launch_info->directory_request = std::move(exported_dir_server);
  return {std::move(exported_dir_client), std::move(client_request)};
}

std::string Util::GetArgsString(
    const ::fidl::VectorPtr<::fidl::StringPtr>& arguments) {
  std::string args = "";
  if (!arguments->empty()) {
    std::ostringstream buf;
    std::copy(arguments->begin(), arguments->end() - 1,
              std::ostream_iterator<std::string>(buf, " "));
    buf << *arguments->rbegin();
    args = buf.str();
  }
  return args;
}

zx::channel Util::OpenAsDirectory(fs::Vfs* vfs, fbl::RefPtr<fs::Vnode> node) {
  zx::channel h1, h2;
  if (zx::channel::create(0, &h1, &h2) != ZX_OK)
    return zx::channel();
  if (vfs->ServeDirectory(std::move(node), std::move(h1)) != ZX_OK)
    return zx::channel();
  return h2;
}

RestartBackOff::RestartBackOff(zx::duration min_backoff,
                               zx::duration max_backoff,
                               zx::duration alive_reset_limit)
    : backoff_(min_backoff, 2u, max_backoff),
      alive_reset_limit_(alive_reset_limit),
      start_time_(0) {}

zx::duration RestartBackOff::GetNext() {
  if (GetCurrentTime() - start_time_ > alive_reset_limit_) {
    backoff_.Reset();
  }
  return backoff_.GetNext();
}

void RestartBackOff::Start() { start_time_ = GetCurrentTime(); }

zx::time RestartBackOff::GetCurrentTime() const {
  return zx::clock::get_monotonic();
}

}  // namespace component
