// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fastboot.h"

#include <lib/fastboot/fastboot_base.h>

namespace gigaboot {

zx::status<> Fastboot::ProcessCommand(std::string_view cmd, fastboot::Transport *transport) {
  return zx::error(ZX_ERR_NOT_SUPPORTED);
}

void Fastboot::DoClearDownload() {}

zx::status<void *> Fastboot::GetDownloadBuffer(size_t total_download_size) {
  return zx::error(ZX_ERR_NOT_SUPPORTED);
}

}  // namespace gigaboot
