// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/scene/session/session_context.h"

namespace mozart {
namespace scene {

SessionContext::SessionContext() = default;

SessionContext::SessionContext(escher::Escher* escher)
    : vk_device_(escher->vulkan_context().device),
      resource_recycler_(escher->resource_recycler()),
      image_factory_(std::make_unique<escher::SimpleImageFactory>(
          resource_recycler_,
          escher->gpu_allocator())),
      gpu_uploader_(escher->gpu_uploader()),
      rounded_rect_factory_(
          std::make_unique<escher::RoundedRectFactory>(escher)) {}

SessionContext::~SessionContext() = default;

ResourceLinker& SessionContext::GetResourceLinker() {
  return resource_linker_;
}

bool SessionContext::ExportResource(ResourcePtr resource,
                                    mx::eventpair endpoint) {
  return resource_linker_.ExportResource(std::move(resource),
                                         std::move(endpoint));
}

void SessionContext::ImportResource(ProxyResourcePtr proxy,
                                    mozart2::ImportSpec spec,
                                    const mx::eventpair& endpoint) {
  // The proxy is not captured in the OnImportResolvedCallback because we don't
  // want the reference in the bind to prevent the proxy from being collected.
  // However, when the proxy dies, its handle is collected which will cause the
  // resource to expire within the resource linker. In that case, we will never
  // receive the callback with |ResolutionResult::kSuccess|.
  ResourceLinker::OnImportResolvedCallback import_resolved_callback =
      std::bind(&SessionContext::OnImportResolvedForResource,  // method
                this,                                          // target
                proxy.get(),  // the proxy that will be resolved by the linker
                std::placeholders::_1,  // the acutal object to link to proxy
                std::placeholders::_2   // result of the linking
                );
  resource_linker_.ImportResource(spec, endpoint, import_resolved_callback);
}

void SessionContext::OnImportResolvedForResource(
    ProxyResource* proxy,
    ResourcePtr actual,
    ResourceLinker::ResolutionResult resolution_result) {
  if (resolution_result == ResourceLinker::ResolutionResult::kSuccess) {
    actual->BindToProxy(proxy);
  }
}

ScenePtr SessionContext::CreateScene(Session* session,
                                     ResourceId node_id,
                                     const mozart2::ScenePtr& args) {
  // TODO: Create a SceneHolder class that takes args.token and destroys
  // links if that gets signalled
  FTL_DCHECK(args);
  // For now, just create a dumb list of scenes.
  auto scene = ftl::MakeRefCounted<Scene>(session, node_id);
  scenes_.push_back(scene);
  return scene;
}

void SessionContext::OnSessionTearDown(Session* session) {
  auto predicate = [session](const ScenePtr& l) {
    return l->session() == session;
  };
  // Remove all scenes for this session.
  scenes_.erase(std::remove_if(scenes_.begin(), scenes_.end(), predicate),
                scenes_.end());
}

}  // namespace scene
}  // namespace mozart
