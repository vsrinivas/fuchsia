// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/impl/escher_impl.h"

#include "escher/impl/gpu_allocator.h"
#include "escher/impl/mesh_manager.h"
#include "escher/impl/render_context.h"
#include "escher/util/cplusplus.h"

namespace escher {
namespace impl {

EscherImpl::EscherImpl(const VulkanContext& context,
                       const VulkanSwapchain& swapchain)
    : device_(context.device),
      allocator_(make_unique<GpuAllocator>(context)),
      mesh_manager_(make_unique<MeshManager>(context, allocator_.get())),
      render_context_(make_unique<RenderContext>(context, mesh_manager_.get())),
      renderer_count_(0) {
  render_context_->Initialize(swapchain);
  FTL_CHECK(swapchain.swapchain);
}

EscherImpl::~EscherImpl() {
  FTL_DCHECK(renderer_count_ == 0);

  device_.waitIdle();
  render_context_.reset();
  mesh_manager_.reset();
  allocator_.reset();
}

Status EscherImpl::Render(const Stage& stage, const Model& model) {
  // Once the device is lost, the Escher instance must be recreated.
  if (device_lost_)
    return Status::kDeviceLost;

  vk::Result result = render_context_->Render(stage, model);
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
  render_context_->SetSwapchain(swapchain);
}

MeshManager* EscherImpl::GetMeshManager() {
  return mesh_manager_.get();
}

}  // namespace impl
}  // namespace escher
