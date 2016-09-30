// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_EXAMPLES_NOODLES_NOODLES_VIEW_H_
#define APPS_MOZART_EXAMPLES_NOODLES_NOODLES_VIEW_H_

#include <memory>
#include <mutex>

#include "apps/mozart/lib/view_framework/base_view.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/memory/ref_counted.h"
#include "lib/mtl/tasks/message_loop.h"
#include "lib/mtl/threading/create_thread.h"
#include "third_party/skia/include/core/SkRefCnt.h"

class SkPicture;

namespace examples {

class Frame;
class Rasterizer;

class NoodlesView : public mozart::BaseView {
 public:
  NoodlesView(mojo::InterfaceHandle<mojo::ApplicationConnector> app_connector,
              mojo::InterfaceRequest<mozart::ViewOwner> view_owner_request);

  ~NoodlesView() override;

 private:
  // Frame queue, held by a std::shared_ptr.
  // This object acts as a shared fifo between both threads.
  class FrameQueue {
   public:
    FrameQueue();
    ~FrameQueue();

    // Puts a pending frame into the queue, drops existing frames if needed.
    // Returns true if the queue was previously empty.
    bool PutFrame(std::unique_ptr<Frame> frame);

    // Takes a pending frame from the queue.
    std::unique_ptr<Frame> TakeFrame();

   private:
    std::mutex mutex_;
    std::unique_ptr<Frame> next_frame_;  // guarded by |mutex_|

    FTL_DISALLOW_COPY_AND_ASSIGN(FrameQueue);
  };

  // Wrapper around state which is only accessible by the rasterizer thread.
  class RasterizerDelegate {
   public:
    explicit RasterizerDelegate(const std::shared_ptr<FrameQueue>& frame_queue);
    ~RasterizerDelegate();

    void CreateRasterizer(
        mojo::InterfaceHandle<mojo::ApplicationConnector> connector_info,
        mojo::InterfaceHandle<mozart::Scene> scene_info);

    void PublishNextFrame();

   private:
    std::shared_ptr<FrameQueue> frame_queue_;
    std::unique_ptr<Rasterizer> rasterizer_;

    FTL_DISALLOW_COPY_AND_ASSIGN(RasterizerDelegate);
  };

  // |BaseView|:
  void OnDraw() override;

  void UpdateFrame();
  sk_sp<SkPicture> CreatePicture();

  std::shared_ptr<FrameQueue> frame_queue_;

  std::unique_ptr<RasterizerDelegate> rasterizer_delegate_;
  std::thread rasterizer_thread_;
  ftl::RefPtr<ftl::TaskRunner> rasterizer_task_runner_;

  double alpha_ = 0.0;
  int wx_ = 2;
  int wy_ = 3;

  FTL_DISALLOW_COPY_AND_ASSIGN(NoodlesView);
};

}  // namespace examples

#endif  // APPS_MOZART_EXAMPLES_NOODLES_NOODLES_VIEW_H_
