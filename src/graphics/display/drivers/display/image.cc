// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "image.h"

#include <lib/zx/handle.h>

#include <atomic>
#include <utility>

#include <ddk/debug.h>
#include <ddk/trace/event.h>
#include <fbl/string_printf.h>

#include "controller.h"

namespace display {

Image::Image(Controller* controller, const image_t& info, zx::vmo handle, uint32_t stride_px,
             inspect::Node* parent_node)
    : info_(info),
      stride_px_(stride_px),
      controller_(controller),
      capture_image_(false),
      vmo_(std::move(handle)) {
  InitializeInspect(parent_node);
}
Image::Image(Controller* controller, const image_t& info, inspect::Node* parent_node)
    : info_(info), controller_(controller), capture_image_(true) {
  InitializeInspect(parent_node);
}

Image::~Image() {
  if (!capture_image_) {
    ZX_ASSERT(!std::atomic_load(&in_use_));
    ZX_ASSERT(!list_in_list(&node.link));
    controller_->ReleaseImage(this);
  } else {
    controller_->ReleaseCaptureImage(this);
  }
}

void Image::InitializeInspect(inspect::Node* parent_node) {
  if (!parent_node)
    return;
  node_ = parent_node->CreateChild(fbl::StringPrintf("image-%p", this).c_str());
  node_.CreateBool("capture_image", capture_image_, &properties_);
  node_.CreateUint("width", info_.width, &properties_);
  node_.CreateUint("height", info_.height, &properties_);
  node_.CreateUint("pixel_format", info_.pixel_format, &properties_);
  node_.CreateUint("type", info_.type, &properties_);
  presenting_property_ = node_.CreateBool("presenting", false);
  retiring_property_ = node_.CreateBool("retiring", false);
}

mtx_t* Image::mtx() { return controller_->mtx(); }

void Image::PrepareFences(fbl::RefPtr<FenceReference>&& wait,
                          fbl::RefPtr<FenceReference>&& signal) {
  wait_fence_ = std::move(wait);
  signal_fence_ = std::move(signal);

  if (wait_fence_) {
    zx_status_t status = wait_fence_->StartReadyWait();
    if (status != ZX_OK) {
      zxlogf(ERROR, "Failed to start waiting %d", status);
      // Mark the image as ready. Displaying garbage is better than hanging or crashing.
      wait_fence_ = nullptr;
    }
  }
}

bool Image::OnFenceReady(FenceReference* fence) {
  if (wait_fence_.get() == fence) {
    wait_fence_ = nullptr;
  }
  return wait_fence_ == nullptr;
}

void Image::StartPresent() {
  ZX_DEBUG_ASSERT(wait_fence_ == nullptr);
  ZX_DEBUG_ASSERT(mtx_trylock(mtx()) == thrd_busy);
  TRACE_DURATION("gfx", "Image::StartPresent", "id", id);
  TRACE_FLOW_BEGIN("gfx", "present_image", id);

  presenting_ = true;
  presenting_property_.Set(true);
}

void Image::EarlyRetire() {
  // A client may re-use an image as soon as signal_fence_ fires. Set in_use_ first.
  std::atomic_store(&in_use_, false);
  if (wait_fence_) {
    wait_fence_->SetImmediateRelease(std::move(signal_fence_));
    wait_fence_ = nullptr;
  } else if (signal_fence_) {
    signal_fence_->Signal();
    signal_fence_ = nullptr;
  }
}

void Image::RetireWithFence(fbl::RefPtr<FenceReference>&& fence) {
  // Retire and acquire are not synchronized, so set in_use_ before signaling so
  // that the image can be reused as soon as the event is signaled. We don't have
  // to worry about the armed signal fence being overwritten on reuse since it is
  // on set in StartRetire, which is called under the same lock as OnRetire.
  std::atomic_store(&in_use_, false);
  if (fence) {
    fence->Signal();
  }
}

void Image::StartRetire() {
  ZX_DEBUG_ASSERT(wait_fence_ == nullptr);
  ZX_DEBUG_ASSERT(mtx_trylock(mtx()) == thrd_busy);

  if (!presenting_) {
    RetireWithFence(std::move(signal_fence_));
  } else {
    retiring_ = true;
    retiring_property_.Set(true);
    armed_signal_fence_ = std::move(signal_fence_);
  }
}

void Image::OnRetire() {
  ZX_DEBUG_ASSERT(mtx_trylock(mtx()) == thrd_busy);

  presenting_ = false;
  presenting_property_.Set(false);

  if (retiring_) {
    RetireWithFence(std::move(armed_signal_fence_));
    retiring_ = false;
    retiring_property_.Set(false);
  }
}

void Image::DiscardAcquire() {
  ZX_DEBUG_ASSERT(wait_fence_ == nullptr);

  std::atomic_store(&in_use_, false);
}

bool Image::Acquire() { return !std::atomic_exchange(&in_use_, true); }

void Image::ResetFences() {
  if (wait_fence_) {
    wait_fence_->ResetReadyWait();
  }

  wait_fence_ = nullptr;
  armed_signal_fence_ = nullptr;
  signal_fence_ = nullptr;
}

}  // namespace display
