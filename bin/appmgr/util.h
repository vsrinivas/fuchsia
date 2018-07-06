// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_APPMGR_UTIL_H_
#define GARNET_BIN_APPMGR_UTIL_H_

#include <fs/vfs.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/backoff/exponential_backoff.h>

#include <string>

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

  static ExportedDirChannels BindDirectory(
      fuchsia::sys::LaunchInfo* launch_info);

  static std::string GetArgsString(
      const ::fidl::VectorPtr<::fidl::StringPtr>& arguments);

  static zx::channel OpenAsDirectory(fs::Vfs* vfs, fbl::RefPtr<fs::Vnode> node);
};

// RestartBackOff wraps the functionality of computing sleep intervals for a
// crashing service under the following assumptions:
// 1) The service may fail to start due to transient issues, in which case we
// want to continue retrying until it starts.
// 2) The service may be permanently failing, in which case we want to wait at
// increasing intervals to avoid rapid crash looping.
// 3) The service may intermittently crash long into execution,
// in which case we want to restart as soon as possible to bring the system into
// a working state.
class RestartBackOff {
 public:
  // Construct a new backoff utility that computes exponentially increasing
  // sleep intervals between min_backoff and max_backoff.
  // If the duration between Start and a call to GetNext is greater than
  // alive_reset_limit, reset the backoff.
  RestartBackOff(zx::duration min_backoff, zx::duration max_backoff,
                 zx::duration alive_reset_limit);

  // Gets the next duration to sleep for.
  zx::duration GetNext();

  // Mark that the service was started at the current time.
  void Start();

 protected:
  virtual zx::time GetCurrentTime() const;

 private:
  backoff::ExponentialBackoff backoff_;
  const zx::duration alive_reset_limit_;
  zx::time start_time_;
};

}  // namespace component

#endif  // GARNET_BIN_APPMGR_UTIL_H_
