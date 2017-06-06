// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/mozart/src/scene/resources/link.h"
#include "escher/escher.h"
#include "escher/impl/gpu_uploader.h"
#include "escher/renderer/image_factory.h"
#include "escher/resources/resource_life_preserver.h"

namespace mozart {
namespace scene {

class Session;

// Interface that describes the ways that a |Session| communicates with its
// environment.
class SessionContext {
 public:
  SessionContext() = default;
  virtual ~SessionContext() = default;

  // Register a resource so that it can be imported into a different session
  // via ImportResourceOp.  Return true if successful, and false if the params
  // are invalid.
  virtual bool ExportResource(ResourcePtr resource,
                              const mozart2::ExportResourceOpPtr& op) = 0;

  // Return a new resource in the importing session that acts as a proxy for
  // a resource that was exported by another session.  Return nullptr if the
  // params are invalid.
  virtual ResourcePtr ImportResource(
      Session* session,
      const mozart2::ImportResourceOpPtr& op) = 0;

  // TODO: deprecated; use ImportResource() instead.
  virtual LinkPtr CreateLink(Session* session,
                             ResourceId node_id,
                             const mozart2::LinkPtr& args) = 0;

  virtual void OnSessionTearDown(Session* session) = 0;

  virtual vk::Device vk_device() = 0;
  virtual escher::ResourceLifePreserver* escher_resource_life_preserver() = 0;
  virtual escher::ImageFactory* escher_image_factory() = 0;
  virtual escher::impl::GpuUploader* escher_gpu_uploader() = 0;
};

}  // namespace scene
}  // namespace mozart
