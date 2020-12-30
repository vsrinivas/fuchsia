// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_FLATLAND_ENGINE_TESTS_COMMON_H_
#define SRC_UI_SCENIC_LIB_FLATLAND_ENGINE_TESTS_COMMON_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async-testing/test_loop.h>
#include <lib/async/cpp/wait.h>
#include <lib/async/default.h>
#include <lib/fdio/directory.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/eventpair.h>

#include <limits>
#include <thread>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/lib/fsl/handles/object_info.h"
#include "src/lib/testing/loop_fixture/real_loop_fixture.h"
#include "src/ui/scenic/lib/flatland/engine/engine.h"
#include "src/ui/scenic/lib/flatland/flatland.h"
#include "src/ui/scenic/lib/flatland/global_image_data.h"
#include "src/ui/scenic/lib/flatland/global_matrix_data.h"
#include "src/ui/scenic/lib/flatland/global_topology_data.h"
#include "src/ui/scenic/lib/flatland/renderer/null_renderer.h"
#include "src/ui/scenic/lib/flatland/renderer/renderer.h"
#include "src/ui/scenic/lib/scheduling/frame_scheduler.h"
#include "src/ui/scenic/lib/scheduling/id.h"

#include <glm/gtx/matrix_transform_2d.hpp>

namespace flatland {

class EngineTestBase : public gtest::RealLoopFixture {
 public:
  fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> CreateToken() {
    zx::channel remote;
    zx::channel::create(0, &local_, &remote);
    fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token{std::move(remote)};
    return token;
  }

  void SetUp() override {
    gtest::RealLoopFixture::SetUp();
    uber_struct_system_ = std::make_shared<UberStructSystem>();
    link_system_ = std::make_shared<LinkSystem>(uber_struct_system_->GetNextInstanceId());
    async_set_default_dispatcher(dispatcher());
  }

  void TearDown() override {
    uber_struct_system_.reset();
    link_system_.reset();

    // Move the channel to a local variable which will go out of scope
    // and close when this function returns.
    zx::channel local(std::move(local_));
    gtest::RealLoopFixture::TearDown();
  }

  class FakeFlatlandSession {
   public:
    FakeFlatlandSession(const std::shared_ptr<UberStructSystem>& uber_struct_system,
                        const std::shared_ptr<LinkSystem>& link_system, EngineTestBase* harness)
        : uber_struct_system_(uber_struct_system),
          link_system_(link_system),
          harness_(harness),
          id_(uber_struct_system_->GetNextInstanceId()),
          graph_(id_),
          queue_(uber_struct_system_->AllocateQueueForSession(id_)) {}

    // Use the TransformGraph API to create and manage transforms and their children.
    TransformGraph& graph() { return graph_; }

    // Returns the link_origin for this session.
    TransformHandle GetLinkOrigin() const {
      EXPECT_TRUE(this->parent_link_.has_value());
      return this->parent_link_.value().parent_link.link_origin;
    }

    // Clears the ParentLink for this session, if one exists.
    void ClearParentLink() { parent_link_.reset(); }

    // Holds the ContentLink and LinkSystem::ChildLink objects since if they fall out of scope,
    // the LinkSystem will delete the link. Tests should add |child_link.link_handle| to their
    // TransformGraphs to use the ChildLink in a topology.
    struct ChildLink {
      fidl::InterfacePtr<fuchsia::ui::scenic::internal::ContentLink> content_link;
      LinkSystem::ChildLink child_link;

      // Returns the handle the parent should add as a child in its local topology to include the
      // link in the topology.
      TransformHandle GetLinkHandle() const { return child_link.link_handle; }
    };

    // Links this session to |parent_session| and returns the ChildLink, which should be used with
    // the parent session. If the return value drops out of scope, tests should call
    // ClearParentLink() on this session.
    ChildLink LinkToParent(FakeFlatlandSession& parent_session);

    // Allocates a new UberStruct with a local_topology rooted at |local_root|. If this session has
    // a ParentLink, the link_origin of that ParentLink will be used instead.
    std::unique_ptr<UberStruct> CreateUberStructWithCurrentTopology(TransformHandle local_root);

    // Pushes |uber_struct| to the UberStructSystem and updates the system so that it represents
    // this session in the InstanceMap.
    void PushUberStruct(std::unique_ptr<UberStruct> uber_struct);

   private:
    // Shared systems for all sessions.
    std::shared_ptr<UberStructSystem> uber_struct_system_;
    std::shared_ptr<LinkSystem> link_system_;

    // The test harness to give access to RunLoopUntilIdle().
    EngineTestBase* harness_;

    // Data specific to this session.
    scheduling::SessionId id_;
    TransformGraph graph_;
    std::shared_ptr<UberStructSystem::UberStructQueue> queue_;

    // Holds the GraphLink and LinkSystem::ParentLink objects since if they fall out of scope,
    // the LinkSystem will delete the link. When |parent_link_| has a value, the
    // |parent_link.link_origin| from this object is used as the root TransformHandle.
    struct ParentLink {
      fidl::InterfacePtr<fuchsia::ui::scenic::internal::GraphLink> graph_link;
      LinkSystem::ParentLink parent_link;
    };
    std::optional<ParentLink> parent_link_;
  };

  FakeFlatlandSession CreateSession() {
    return FakeFlatlandSession(uber_struct_system_, link_system_, this);
  }

 protected:
  const std::shared_ptr<UberStructSystem>& uber_struct_system() const {
    return uber_struct_system_;
  }

  const std::shared_ptr<LinkSystem>& link_system() const { return link_system_; }

 private:
  // Systems that are populated with data from Flatland instances.
  std::shared_ptr<UberStructSystem> uber_struct_system_;
  std::shared_ptr<LinkSystem> link_system_;
  zx::channel local_;
};

}  // namespace flatland

#endif  // SRC_UI_SCENIC_LIB_FLATLAND_ENGINE_TESTS_COMMON_H_
