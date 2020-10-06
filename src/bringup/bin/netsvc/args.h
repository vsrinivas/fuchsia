// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_BRINGUP_BIN_NETSVC_ARGS_H_
#define SRC_BRINGUP_BIN_NETSVC_ARGS_H_

#include <lib/zx/channel.h>

#include <string>

struct NetsvcArgs {
  // This is true if `netsvc.disable` is on the kernel commandline.
  bool disable = false;
  // This is true if `netsvc.netboot` is on the kernel commandline
  // OR if '--netboot' is on the binary commandline.
  bool netboot = false;
  // This is true if `zircon.nodename` is on the kernel commandline
  // OR if `--nodenamde` is on the binary commandline.
  bool nodename = false;
  // This is true if `netsvc.advertise` is on the kernel commandline
  // OR if `--advertise` is on the binary commandline.
  bool advertise = false;
  // This is true if `netsvc.all-features` is on the kernel commandlinne
  // OR if `--all-features` is on the binary commandline.
  bool all_features = false;
  // This is the string value of `netsvc.interface`.
  // It is overriden by the string value of `--interface` on the binary commandline.
  std::string interface;
};

// Parses NetsvcArgs via the kernel commandline and the binary commandline (argv).
// If ParseArgs returns < 0, an error string will be returned in |error|.
int ParseArgs(int argc, char** argv, const zx::channel& svc_root, const char** error,
              NetsvcArgs* out);

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

#endif  // SRC_BRINGUP_BIN_NETSVC_ARGS_H_
