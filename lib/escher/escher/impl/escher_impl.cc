// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/impl/escher_impl.h"
#include "escher/util/cplusplus.h"

namespace escher {
namespace impl {

EscherImpl::EscherImpl(const VulkanContext& context,
                       const VulkanSwapchain& swapchain)
    : render_context_(context) {
  render_context_.Initialize(swapchain);
  FTL_CHECK(swapchain.swapchain);
}

EscherImpl::~EscherImpl() {}

Status EscherImpl::Render(const Stage& stage, const Model& model) {
  // Once the device is lost, the Escher instance must be recreated.
  if (device_lost_)
    return Status::kDeviceLost;

  vk::Result result = render_context_.Render(stage, model);
  switch (result) {
    case vk::Result::eSuccess:
      return Status::kOk;
    case vk::Result::eTimeout:
      return Status::kTimeout;
    case vk::Result::eNotReady:
      return Status::kNotReady;
    case vk::Result::eErrorOutOfHostMemory:
      return Status::kOutOfHostMemory;
    case vk::Result::eErrorOutOfDeviceMemory:
      return Status::kOutOfDeviceMemory;
    case vk::Result::eErrorDeviceLost:
      device_lost_ = true;
      return Status::kDeviceLost;
    default:
      FTL_LOG(ERROR) << "EscherImpl::Render() unexpected failure result: "
                     << to_string(result);
      return Status::kInternalError;
  }
}

void EscherImpl::SetSwapchain(const VulkanSwapchain& swapchain) {
  render_context_.SetSwapchain(swapchain);
}

}  // namespace impl
}  // namespace escher
