// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_USERABI_USERBOOT_LOADER_SERVICE_H_
#define ZIRCON_KERNEL_LIB_USERABI_USERBOOT_LOADER_SERVICE_H_

#include <lib/zx/channel.h>
#include <lib/zx/debuglog.h>
#include <lib/zx/vmo.h>
#include <zircon/types.h>

#include <array>
#include <cstddef>
#include <string_view>

class Bootfs;

class LoaderService {
 public:
  LoaderService(zx::debuglog log, Bootfs* fs, std::string_view root)
      : log_(std::move(log)), fs_(fs), root_(root) {}

  // Handle loader-service RPCs on channel until there are no more.
  // Consumes the channel.
  void Serve(zx::channel);

 private:
  static constexpr std::string_view kLoadObjectFileDir = "lib";
  zx::debuglog log_;
  Bootfs* fs_;
  std::string_view root_;
  std::array<char, 32> subdir_;
  size_t subdir_len_ = 0;
  bool exclusive_ = false;

  bool HandleRequest(const zx::channel&);
  void Config(std::string_view string);
  zx::vmo LoadObject(std::string_view name);
  zx::vmo TryLoadObject(std::string_view name, bool use_subdir);
};

#endif  // ZIRCON_KERNEL_LIB_USERABI_USERBOOT_LOADER_SERVICE_H_
