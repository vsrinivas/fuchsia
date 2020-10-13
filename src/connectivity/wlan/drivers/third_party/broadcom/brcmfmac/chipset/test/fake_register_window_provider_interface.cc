// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/chipset/test/fake_register_window_provider_interface.h"

#include <zircon/errors.h>

namespace wlan {
namespace brcmfmac {

class FakeRegisterWindowProviderInterface::RegisterWindow
    : public RegisterWindowProviderInterface::RegisterWindow {
 public:
  explicit RegisterWindow(FakeRegisterWindowProviderInterface* parent, uint32_t offset,
                          size_t size);
  ~RegisterWindow() override;

  // RegisterWindowProviderInterface::RegisterWindow implemnetation.
  zx_status_t Read(uint32_t offset, uint32_t* value) override;
  zx_status_t Write(uint32_t offset, uint32_t value) override;

 private:
  FakeRegisterWindowProviderInterface* const parent_ = nullptr;
  const uint32_t offset_ = 0;
  const size_t size_ = 0;
};

FakeRegisterWindowProviderInterface::RegisterWindow::RegisterWindow(
    FakeRegisterWindowProviderInterface* parent, uint32_t offset, size_t size)
    : parent_(parent), offset_(offset), size_(size) {
  ++parent_->window_count_;
}

FakeRegisterWindowProviderInterface::RegisterWindow::~RegisterWindow() { --parent_->window_count_; }

zx_status_t FakeRegisterWindowProviderInterface::RegisterWindow::Read(uint32_t offset,
                                                                      uint32_t* value) {
  if (offset + sizeof(*value) > size_) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  return parent_->Read(offset_ + offset, value);
}

zx_status_t FakeRegisterWindowProviderInterface::RegisterWindow::Write(uint32_t offset,
                                                                       uint32_t value) {
  if (offset + sizeof(value) > size_) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  return parent_->Write(offset_ + offset, value);
}

FakeRegisterWindowProviderInterface::FakeRegisterWindowProviderInterface(size_t ram_size,
                                                                         size_t window_size)
    : ram_size_(ram_size), window_size_(window_size) {}

FakeRegisterWindowProviderInterface::~FakeRegisterWindowProviderInterface() = default;

zx_status_t FakeRegisterWindowProviderInterface::Read(uint32_t offset, uint32_t* value) {
  if (offset + sizeof(*value) > ram_size_) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  *value = ram_map_[offset];
  return ZX_OK;
}

zx_status_t FakeRegisterWindowProviderInterface::Write(uint32_t offset, uint32_t value) {
  if (offset + sizeof(value) > ram_size_) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  ram_map_[offset] = value;
  return ZX_OK;
}

zx_status_t FakeRegisterWindowProviderInterface::GetRegisterWindow(
    uint32_t offset, size_t size,
    std::unique_ptr<RegisterWindowProviderInterface::RegisterWindow>* out_register_window) {
  const uint32_t window_offset = offset % window_size_;
  const uint32_t window_base = offset - window_offset;
  if (offset + size > window_base + window_size_) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  if (window_count_ > 0 && window_base != window_base_) {
    return ZX_ERR_ALREADY_BOUND;
  }

  *out_register_window = std::make_unique<RegisterWindow>(this, offset, size);
  return ZX_OK;
}

}  // namespace brcmfmac
}  // namespace wlan
