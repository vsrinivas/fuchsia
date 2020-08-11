// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_CHIPSET_CHIPSET_INTERFACES_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_CHIPSET_CHIPSET_INTERFACES_H_

#include <zircon/types.h>

#include <memory>

namespace wlan {
namespace brcmfmac {

// This interface represents the operations that a bus needs to provide a register window for
// communication with chipset registers behind the bus.
class RegisterWindowProviderInterface {
 public:
  // This class is an instance of a buscore register space window.  The window remains valid
  // throughout the lifetime of the RegisterWindow instance.
  class RegisterWindow {
   public:
    virtual ~RegisterWindow();

    virtual zx_status_t Read(uint32_t offset, uint32_t* value) = 0;
    virtual zx_status_t Write(uint32_t offset, uint32_t value) = 0;
  };

  virtual ~RegisterWindowProviderInterface();

  // Get a register space window from the buscore.
  virtual zx_status_t GetRegisterWindow(uint32_t offset, size_t size,
                                        std::unique_ptr<RegisterWindow>* out_register_window) = 0;
};

}  // namespace brcmfmac
}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_CHIPSET_CHIPSET_INTERFACES_H_
