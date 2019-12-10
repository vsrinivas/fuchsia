// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_VK_IMAGE_LAYOUT_UPDATER_H_
#define SRC_UI_LIB_ESCHER_VK_IMAGE_LAYOUT_UPDATER_H_

#include <unordered_set>

#include "src/ui/lib/escher/escher.h"
#include "src/ui/lib/escher/forward_declarations.h"
#include "src/ui/lib/escher/vk/command_buffer.h"

namespace escher {

// Vulkan device images can be created only with layout eUndefined or
// ePreinitialized. ImageLayoutUpdater is used to update device images to
// the desired image layout.
class ImageLayoutUpdater {
 public:
  explicit ImageLayoutUpdater(EscherWeakPtr const escher);

  static std::unique_ptr<ImageLayoutUpdater> New(EscherWeakPtr escher) {
    return std::make_unique<ImageLayoutUpdater>(escher);
  }

  ~ImageLayoutUpdater();

  // Returns true if ImageLayoutUpdater needs a command buffer, i.e. it needs to
  // update layout of images, or it needs to wait on/signal semaphores.
  bool NeedsCommandBuffer() const {
    return !pending_image_layout_to_set_.empty() || !wait_semaphores_.empty() ||
           !signal_semaphores_.empty();
  }

  // Sets image initial layout. This updates both the |layout_| stored in
  // |escher::Image| object and sends |ImageBarrier| to command buffer.
  void ScheduleSetImageInitialLayout(const escher::ImagePtr& image, vk::ImageLayout new_layout);

  // Submits all the |ImageBarrier| commands to a new-created command buffer.
  void Submit(fit::function<void()> callback = nullptr,
              CommandBuffer::Type type = CommandBuffer::Type::kTransfer);

  // Generate image layout update commands to the command buffer for submission.
  //
  // After this function is called, the |pending_image_layout_to_set_|,
  // |images_to_set_| and all semaphores will be clear so that the image layout
  // updater will be reused again.
  void GenerateCommands(CommandBuffer* cmds);

  // Submit() will wait on all semaphores added by AddWaitSemaphore().
  void AddWaitSemaphore(SemaphorePtr sema, vk::PipelineStageFlags flags) {
    wait_semaphores_.push_back({std::move(sema), flags});
  }

  // Submit() will signal all semaphores added by AddSignalSemaphore().
  void AddSignalSemaphore(SemaphorePtr sema) { signal_semaphores_.push_back(std::move(sema)); }

 private:
  EscherWeakPtr escher_;

  std::vector<std::pair<ImagePtr, vk::ImageLayout>> pending_image_layout_to_set_;
  std::unordered_set<ImagePtr> images_to_set_;

  std::vector<std::pair<SemaphorePtr, vk::PipelineStageFlags>> wait_semaphores_;
  std::vector<SemaphorePtr> signal_semaphores_;
};

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_VK_IMAGE_LAYOUT_UPDATER_H_
