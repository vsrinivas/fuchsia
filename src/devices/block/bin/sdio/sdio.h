// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_UAPP_SDIO_SDIO_H_
#define ZIRCON_SYSTEM_UAPP_SDIO_SDIO_H_

#include <fuchsia/hardware/sdio/llcpp/fidl.h>

namespace sdio {

using namespace ::llcpp::fuchsia::hardware::sdio;
using SdioClient = ::llcpp::fuchsia::hardware::sdio::Device::SyncClient;

std::string GetTxnStats(const zx::duration duration, const uint64_t bytes);
void PrintUsage();
void PrintVersion();
int RunSdioTool(SdioClient client, int argc, const char** argv);

}  // namespace sdio

#endif  // ZIRCON_SYSTEM_UAPP_SDIO_SDIO_H_
