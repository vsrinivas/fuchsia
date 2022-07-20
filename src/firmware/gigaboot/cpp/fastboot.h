// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_FIRMWARE_GIGABOOT_CPP_FASTBOOT_H_
#define SRC_FIRMWARE_GIGABOOT_CPP_FASTBOOT_H_

#include <lib/fastboot/fastboot_base.h>

namespace gigaboot {

class Fastboot : public fastboot::FastbootBase {
 public:
 private:
  zx::status<> ProcessCommand(std::string_view cmd, fastboot::Transport *transport) override;
  void DoClearDownload() override;
  zx::status<void *> GetDownloadBuffer(size_t total_download_size) override;
};

}  // namespace gigaboot

#endif  // SRC_FIRMWARE_GIGABOOT_CPP_FASTBOOT_H_
