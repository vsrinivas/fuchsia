// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/chipset/chipset_interfaces.h"

namespace wlan {
namespace brcmfmac {

RegisterWindowProviderInterface::~RegisterWindowProviderInterface() = default;

RegisterWindowProviderInterface::RegisterWindow::~RegisterWindow() = default;

}  // namespace brcmfmac
}  // namespace wlan
