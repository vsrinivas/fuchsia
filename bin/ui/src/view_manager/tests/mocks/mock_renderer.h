// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_SRC_VIEW_MANAGER_TESTS_MOCKS_MOCK_RENDERER_H_
#define APPS_MOZART_SRC_VIEW_MANAGER_TESTS_MOCKS_MOCK_RENDERER_H_

#include "application/lib/app/application_context.h"
#include "apps/mozart/services/composition/renderers.fidl.h"

namespace view_manager {
namespace test {

class MockRenderer : public mozart::Renderer,
                     public mozart::FrameScheduler,
                     public mozart::HitTester {
 public:
  MockRenderer();
  ~MockRenderer();

 private:
  // |Renderer|:
  void GetDisplayInfo(const GetDisplayInfoCallback& callback) override;
  void SetRootScene(mozart::SceneTokenPtr scene_token,
                    uint32_t scene_version,
                    mozart::RectPtr viewport) override;
  void ClearRootScene() override;
  void GetScheduler(fidl::InterfaceRequest<mozart::FrameScheduler>
                        scheduler_request) override;
  void GetHitTester(
      fidl::InterfaceRequest<mozart::HitTester> hit_tester_request) override;

  // |FrameScheduler|:
  void ScheduleFrame(const ScheduleFrameCallback& callback) override;

  // |HitTester|:
  void HitTest(mozart::PointFPtr point,
               const HitTestCallback& callback) override;

  mozart::SceneTokenPtr scene_token_;
  uint32_t scene_version_;
  mozart::RectPtr viewport_;

  fidl::BindingSet<mozart::FrameScheduler> scheduler_bindings_;
  fidl::BindingSet<mozart::HitTester> hit_tester_bindings;
};

}  // namespace test
}  // namespace view_manager

#endif  // APPS_MOZART_SRC_VIEW_MANAGER_TESTS_MOCKS_MOCK_RENDERER_H_
