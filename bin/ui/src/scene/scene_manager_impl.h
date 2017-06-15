// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <unordered_map>

#include "apps/mozart/services/scene/scene_manager.fidl.h"
#include "apps/mozart/services/scene/session.fidl.h"
#include "apps/mozart/src/scene/resources/link.h"
#include "apps/mozart/src/scene/session/session.h"
#include "apps/mozart/src/scene/session/session_handler.h"
#include "escher/forward_declarations.h"
#include "escher/renderer/simple_image_factory.h"
#include "lib/mtl/tasks/message_loop.h"
#include "lib/mtl/threading/thread.h"

namespace mozart {
namespace scene {

class Renderer;

class SceneManagerImpl : public mozart2::SceneManager, public SessionContext {
 public:
  SceneManagerImpl(vk::Device vk_device,
                   escher::ResourceRecycler* resource_recycler,
                   escher::GpuAllocator* allocator,
                   escher::impl::GpuUploader* uploader);
  SceneManagerImpl();
  ~SceneManagerImpl() override;

  // mozart2::SceneManager interface methods.
  void CreateSession(
      ::fidl::InterfaceRequest<mozart2::Session> request,
      ::fidl::InterfaceHandle<mozart2::SessionListener> listener) override;

  // SessionContext interface methods.
  bool ExportResource(ResourcePtr resource,
                      const mozart2::ExportResourceOpPtr& op) override;
  ResourcePtr ImportResource(Session* session,
                             const mozart2::ImportResourceOpPtr& op) override;
  LinkPtr CreateLink(Session* session,
                     ResourceId node_id,
                     const mozart2::LinkPtr& args) override;
  void OnSessionTearDown(Session* session) override;

  size_t GetSessionCount() { return session_count_; }

  // Gets the VkDevice that is used with the renderer.
  // TODO: Should this belong in Renderer, or something like a
  // SessionResourceFactory?
  vk::Device vk_device() override { return vk_device_; }
  escher::ResourceRecycler* escher_resource_recycler() override {
    return resource_recycler_;
  }
  escher::ImageFactory* escher_image_factory() override {
    return image_factory_.get();
  }
  escher::impl::GpuUploader* escher_gpu_uploader() override {
    return gpu_uploader_;
  }

  const std::vector<LinkPtr>& links() const { return links_; }

  Renderer* renderer() const { return renderer_.get(); }

  SessionHandler* FindSession(SessionId id);

 private:
  friend class SessionHandler;
  void ApplySessionUpdate(std::unique_ptr<SessionUpdate> update);

  void TearDownSession(SessionId id);

  // Allow overriding to support tests.
  virtual std::unique_ptr<SessionHandler> CreateSessionHandler(
      SessionId id,
      ::fidl::InterfaceRequest<mozart2::Session> request,
      ::fidl::InterfaceHandle<mozart2::SessionListener> listener);

  std::unordered_map<SessionId, std::unique_ptr<SessionHandler>> sessions_;
  std::atomic<size_t> session_count_;

  vk::Device vk_device_;
  escher::ResourceRecycler* resource_recycler_;
  std::unique_ptr<escher::SimpleImageFactory> image_factory_;
  escher::impl::GpuUploader* gpu_uploader_;

  // Placeholders for Links and the Renderer. These will be instantiated
  // differently in the future.
  std::vector<LinkPtr> links_;
  std::unique_ptr<Renderer> renderer_;

  SessionId next_session_id_ = 1;
};

}  // namespace scene
}  // namespace mozart
