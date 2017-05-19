// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <unordered_map>

#include "apps/mozart/services/composer/composer.fidl.h"
#include "apps/mozart/services/composer/session.fidl.h"
#include "apps/mozart/src/composer/resources/link.h"
#include "apps/mozart/src/composer/session/session.h"
#include "apps/mozart/src/composer/session/session_handler.h"
#include "escher/forward_declarations.h"
#include "escher/renderer/simple_image_factory.h"
#include "lib/mtl/tasks/message_loop.h"
#include "lib/mtl/threading/thread.h"

namespace mozart {
namespace composer {

class Renderer;

class ComposerImpl : public mozart2::Composer, public SessionContext {
 public:
  ComposerImpl(vk::Device vk_device,
               escher::ResourceLifePreserver* life_preserver,
               escher::GpuAllocator* allocator,
               escher::impl::GpuUploader* uploader);
  ComposerImpl();
  ~ComposerImpl() override;

  // mozart2::Composer interface methods.
  void CreateSession(
      ::fidl::InterfaceRequest<mozart2::Session> request,
      ::fidl::InterfaceHandle<mozart2::SessionListener> listener) override;

  // SessionContext interface methods.
  LinkPtr CreateLink(Session* session,
                     ResourceId node_id,
                     const mozart2::LinkPtr& args) override;
  void OnSessionTearDown(Session* session) override;

  size_t GetSessionCount() { return session_count_; }

  // Gets the VkDevice that is used with the renderer.
  // TODO: Should this belong in Renderer, or something like a
  // SessionResourceFactory?
  vk::Device vk_device() override { return vk_device_; }
  escher::ResourceLifePreserver* escher_resource_life_preserver() override {
    return life_preserver_;
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
  escher::ResourceLifePreserver* life_preserver_;
  std::unique_ptr<escher::SimpleImageFactory> image_factory_;
  escher::impl::GpuUploader* gpu_uploader_;

  // Placeholders for Links and the Renderer. These will be instantiated
  // differently in the future.
  std::vector<LinkPtr> links_;
  std::unique_ptr<Renderer> renderer_;

  SessionId next_session_id_ = 1;
};

}  // namespace composer
}  // namespace mozart
