// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_RENDERER_SEMAPHORE_H_
#define SRC_UI_LIB_ESCHER_RENDERER_SEMAPHORE_H_

#include <vulkan/vulkan.hpp>

#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/memory/ref_counted.h"

namespace escher {

class Semaphore;
typedef fxl::RefPtr<Semaphore> SemaphorePtr;

// TODO: perhaps return semaphores to a pool instead of destroying them.
// TODO: make this a subclass of Reffable.
class Semaphore : public fxl::RefCountedThreadSafe<Semaphore> {
 public:
  explicit Semaphore(vk::Device device);
  Semaphore(vk::Device device, bool exportable);
  ~Semaphore();

  // Convenient.
  static SemaphorePtr New(vk::Device device);
  static SemaphorePtr NewExportableSem(vk::Device device);

  vk::Semaphore vk_semaphore() const { return value_; }

 private:
  vk::Device device_;
  vk::Semaphore value_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Semaphore);
};

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_RENDERER_SEMAPHORE_H_
