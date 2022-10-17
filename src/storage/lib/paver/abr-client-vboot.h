// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_LIB_PAVER_ABR_CLIENT_VBOOT_H_
#define SRC_STORAGE_LIB_PAVER_ABR_CLIENT_VBOOT_H_

#include <lib/zx/status.h>
#include <zircon/errors.h>

#include <memory>

#include <gpt/cros.h>
#include <gpt/gpt.h>

#include "src/storage/lib/paver/abr-client.h"
#include "src/storage/lib/paver/chromebook-x64.h"

namespace abr {
class VbootClient : public Client {
 public:
  static zx::result<std::unique_ptr<abr::VbootClient>> Create(
      std::unique_ptr<paver::CrosDevicePartitioner> gpt);

  explicit VbootClient(std::unique_ptr<paver::CrosDevicePartitioner> gpt)
      : Client(true), gpt_(std::move(gpt)) {}

 private:
  std::unique_ptr<paver::CrosDevicePartitioner> gpt_;

  zx::result<> Read(uint8_t* buffer, size_t size) override {
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  zx::result<> Write(const uint8_t* buffer, size_t size) override {
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  zx::result<> ReadCustom(AbrSlotData* a, AbrSlotData* b, uint8_t* one_shot_recovery) override;
  zx::result<> WriteCustom(const AbrSlotData* a, const AbrSlotData* b,
                           uint8_t one_shot_recovery) override;

  zx::result<> Flush() const override { return zx::make_result(gpt_->GetGpt()->Sync()); }
};
}  // namespace abr

#endif  // SRC_STORAGE_LIB_PAVER_ABR_CLIENT_VBOOT_H_
