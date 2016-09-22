// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_GFX_COMPOSITOR_BACKEND_FRAMEBUFFER_OUTPUT_H_
#define SERVICES_GFX_COMPOSITOR_BACKEND_FRAMEBUFFER_OUTPUT_H_

#include "apps/mozart/src/compositor/backend/output.h"
#include "apps/mozart/src/compositor/backend/vsync_scheduler.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/memory/ref_counted.h"
#include "lib/ftl/tasks/task_runner.h"
#include "mojo/services/framebuffer/interfaces/framebuffer.mojom.h"
#include "third_party/skia/include/core/SkSurface.h"

namespace compositor {

// Renderer backed by a Framebuffer.
// TODO(jeffbrown): This renderer doesn't do any pipelining.
class FramebufferOutput : public Output {
 public:
  FramebufferOutput(mojo::InterfaceHandle<mojo::Framebuffer> framebuffer,
                    mojo::FramebufferInfoPtr framebuffer_info,
                    const SchedulerCallbacks& scheduler_callbacks,
                    const ftl::Closure& error_callback);
  ~FramebufferOutput() override;

  Scheduler* GetScheduler() override;
  void SubmitFrame(const ftl::RefPtr<RenderFrame>& frame) override;

 private:
  void PostErrorCallback();

  mojo::FramebufferPtr framebuffer_;
  mojo::FramebufferInfoPtr framebuffer_info_;
  uintptr_t framebuffer_data_ = 0u;
  sk_sp<SkSurface> framebuffer_surface_;

  ftl::RefPtr<ftl::TaskRunner> compositor_task_runner_;
  ftl::RefPtr<VsyncScheduler> vsync_scheduler_;
  ftl::Closure error_callback_;

  FTL_DISALLOW_COPY_AND_ASSIGN(FramebufferOutput);
};

}  // namespace compositor

#endif  // SERVICES_GFX_COMPOSITOR_BACKEND_FRAMEBUFFER_OUTPUT_H_
