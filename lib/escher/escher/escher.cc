// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/escher.h"
#include "escher/impl/escher_impl.h"
#include "escher/util/cplusplus.h"

namespace escher {

Escher::Escher(const VulkanContext& context, const VulkanSwapchain& swapchain)
    : impl_(make_unique<impl::EscherImpl>(context, swapchain)) {}

Escher::~Escher() {}

void Escher::SetSwapchain(const VulkanSwapchain& swapchain) {
  impl_->SetSwapchain(swapchain);
}

Status Escher::Render(const Stage& stage, const Model& model) {
  return impl_->Render(stage, model);
}

}  // namespace escher
