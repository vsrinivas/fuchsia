// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "llvm-fuzzer.h"

#include <zircon/errors.h>

#include "libfuzzer.h"

namespace fuzzing {

// Public methods

LlvmFuzzerImpl::LlvmFuzzerImpl() : binding_(this) {}

LlvmFuzzerImpl::~LlvmFuzzerImpl() {}

zx_status_t LlvmFuzzerImpl::Configure(DataProviderPtr data_provider) {
  data_provider_.Unbind();
  if (!data_provider.is_bound()) {
    return ZX_ERR_INVALID_ARGS;
  }
  data_provider_ = std::move(data_provider);

  zx_status_t status;
  zx::vmo vmo;
  if ((status = input_.Create()) != ZX_OK || (status = input_.Share(&vmo)) != ZX_OK) {
    return status;
  }

  data_provider_->Configure(binding_.NewBinding(), std::move(vmo), GetDataConsumerLabels(),
                            []() {});
  return ZX_OK;
}

void LlvmFuzzerImpl::TestOneInput(TestOneInputCallback callback) {
  callback(LLVMFuzzerTestOneInput(input_.data(), input_.size()));
}

}  // namespace fuzzing
