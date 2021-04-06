// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BLOCK_BIN_SDIO_SDIO_H_
#define SRC_DEVICES_BLOCK_BIN_SDIO_SDIO_H_

#include <fuchsia/hardware/sdio/llcpp/fidl.h>

namespace sdio {

using namespace fuchsia_hardware_sdio;
using SdioClient = fidl::WireSyncClient<fuchsia_hardware_sdio::Device>;

std::string GetTxnStats(const zx::duration duration, const uint64_t bytes);
void PrintUsage();
void PrintVersion();
int RunSdioTool(SdioClient client, int argc, const char** argv);

}  // namespace sdio

#endif  // SRC_DEVICES_BLOCK_BIN_SDIO_SDIO_H_
