// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_BRINGUP_BIN_DEVICE_NAME_PROVIDER_ARGS_H_
#define SRC_BRINGUP_BIN_DEVICE_NAME_PROVIDER_ARGS_H_

#include <lib/zx/channel.h>

#include <string>

struct DeviceNameProviderArgs {
  // This is the string value of `netsvc.interface`.
  // It is overriden by the string value of `--interface` on the binary commandline.
  std::string interface;
  // This is the string value of `zircon.nodename`.
  // It is overriden by the string value of `--nodename` on the binary commandline.
  std::string nodename;
  // This defaults to "/dev/class/ethernet/"
  // BUT it overriden by `--ethdir` on the binary commandline.
  std::string ethdir;
};

// Parses DeviceNameProviderArgs via the kernel commandline and the binary commandline (argv).
// If ParseArgs returns < 0, an error string will be returned in |error|.
int ParseArgs(int argc, char** argv, const zx::channel& svc_root, const char** error,
              DeviceNameProviderArgs* out);

#endif  // SRC_BRINGUP_BIN_DEVICE_NAME_PROVIDER_ARGS_H_
