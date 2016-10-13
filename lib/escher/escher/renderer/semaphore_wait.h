// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vulkan/vulkan.hpp>

#include "ftl/macros.h"
#include "ftl/memory/ref_counted.h"

namespace escher {

class Semaphore;
typedef ftl::RefPtr<Semaphore> SemaphorePtr;

// TODO: rename file.
// TODO: perhaps return semaphores to a pool instead of destroying them.
class Semaphore : public ftl::RefCountedThreadSafe<Semaphore> {
 public:
  explicit Semaphore(vk::Device device);
  ~Semaphore();

  // Convenient.
  static SemaphorePtr New(vk::Device device);

  vk::Semaphore value() const { return value_; }

 private:
  vk::Device device_;
  vk::Semaphore value_;

  FTL_DISALLOW_COPY_AND_ASSIGN(Semaphore);
};

}  // namespace escher
