// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <zircon/types.h>

#include <map>
#include <memory>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/chipset/chipset_interfaces.h"

namespace wlan {
namespace brcmfmac {

// This class is a fake RegisterWindowProviderInterface implementation, that keeps a fake register
// mapping in an std::map<>.
class FakeRegisterWindowProviderInterface : public RegisterWindowProviderInterface {
 public:
  explicit FakeRegisterWindowProviderInterface(size_t ram_size, size_t window_size);
  ~FakeRegisterWindowProviderInterface() override;

  virtual zx_status_t Read(uint32_t offset, uint32_t* value);
  virtual zx_status_t Write(uint32_t offset, uint32_t value);
  virtual zx_status_t GetRegisterWindow(
      uint32_t offset, size_t size,
      std::unique_ptr<RegisterWindowProviderInterface::RegisterWindow>* out_register_window)
      override;

  // Used to pre-populate the RAM map.
  template <typename Iterator>
  void Fill(uint32_t offset, Iterator begin, Iterator end) {
    for (; begin != end; ++begin) {
      ram_map_[offset] = *begin;
      offset += 4;
    }
  }

 private:
  class RegisterWindow;

  std::map<uint32_t, uint32_t> ram_map_;
  const size_t ram_size_ = 0;
  const size_t window_size_ = 0;
  uint32_t window_base_ = 0;
  int window_count_ = 0;
};

}  // namespace brcmfmac
}  // namespace wlan
