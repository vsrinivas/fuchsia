// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/mozart/examples/shadertoy/glm_hack.h"
#include "apps/mozart/examples/shadertoy/services/shadertoy.fidl.h"
#include "apps/mozart/services/images2/image_pipe.fidl.h"
#include "apps/mozart/services/views/view_token.fidl.h"
#include "escher/escher.h"
#include "lib/ftl/memory/ref_counted.h"
#include "lib/ftl/memory/weak_ptr.h"

class Compiler;
class Pipeline;
class Renderer;
class ShadertoyApp;
using PipelinePtr = ftl::RefPtr<Pipeline>;

// Core implementation of the Shadertoy API.  Subclasses must provide some
// functionality, such as the method for obtaining a framebuffer to render into.
class ShadertoyState : public ftl::RefCountedThreadSafe<ShadertoyState> {
 public:
  // Factory constructor.
  static ftl::RefPtr<ShadertoyState> NewForImagePipe(
      ShadertoyApp* app,
      ::fidl::InterfaceHandle<mozart2::ImagePipe> image_pipe);

  // Factory constructor.
  static ftl::RefPtr<ShadertoyState> NewForMaterial(ShadertoyApp* app,
                                                    mx::eventpair export_token);

  // Factory constructor.
  static ftl::RefPtr<ShadertoyState> NewForView(
      ShadertoyApp* app,
      ::fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
      bool handle_input_events);

  virtual ~ShadertoyState();

  void SetPaused(bool paused);

  void SetShaderCode(std::string glsl,
                     const Shadertoy::SetShaderCodeCallback& callback);

  void SetResolution(uint32_t width, uint32_t height);

  void SetMouse(glm::vec4 i_mouse);

  void SetImage(uint32_t channel,
                ::fidl::InterfaceRequest<mozart2::ImagePipe> request);

 protected:
  explicit ShadertoyState(ShadertoyApp* app);

  void DrawFrame(uint64_t presentation_time);
  void RequestFrame();

  // Tell the app to close the connection to this Shadertoy, and destroy it.
  void Close();

  virtual void OnSetResolution() = 0;
  virtual escher::Framebuffer* GetOutputFramebuffer() = 0;

  uint32_t width() const { return width_; }
  uint32_t height() const { return height_; }
  escher::Escher* escher() const { return escher_; }

 private:
  static constexpr uint32_t kMaxWidth = 2048;
  static constexpr uint32_t kMaxHeight = 2048;

  ShadertoyApp* const app_;
  escher::Escher* const escher_;
  Compiler* const compiler_;
  Renderer* const renderer_;
  ftl::WeakPtrFactory<ShadertoyState> weak_ptr_factory_;
  PipelinePtr pipeline_;
  uint32_t width_ = 0;
  uint32_t height_ = 0;
  glm::vec4 i_mouse_ = {0, 0, 0, 0};
};
