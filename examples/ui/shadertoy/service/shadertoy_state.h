// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_UI_SHADERTOY_SERVICE_SHADERTOY_STATE_H_
#define GARNET_EXAMPLES_UI_SHADERTOY_SERVICE_SHADERTOY_STATE_H_

#include <fuchsia/examples/shadertoy/cpp/fidl.h>
#include <fuchsia/images/cpp/fidl.h>
#include <fuchsia/ui/viewsv1token/cpp/fidl.h>
#include "garnet/examples/ui/shadertoy/service/glm_hack.h"
#include "lib/escher/escher.h"
#include "lib/escher/resources/resource.h"
#include "lib/escher/util/stopwatch.h"
#include "lib/fxl/memory/ref_counted.h"
#include "lib/fxl/memory/weak_ptr.h"

namespace shadertoy {

class Compiler;
class Pipeline;
class Renderer;
class App;
using PipelinePtr = fxl::RefPtr<Pipeline>;

// Core implementation of the Shadertoy API.  Subclasses must provide some
// functionality, such as the method for obtaining a framebuffer to render into.
class ShadertoyState : public escher::Resource {
 public:
  // Factory constructor.
  static fxl::RefPtr<ShadertoyState> NewForImagePipe(
      App* app, ::fidl::InterfaceHandle<fuchsia::images::ImagePipe> image_pipe);

  // Factory constructor.
  static fxl::RefPtr<ShadertoyState> NewForView(
      App* app,
      ::fidl::InterfaceRequest<::fuchsia::ui::viewsv1token::ViewOwner>
          view_owner_request,
      bool handle_input_events);

  virtual ~ShadertoyState();

  void SetPaused(bool paused);

  void SetShaderCode(
      fidl::StringPtr glsl,
      fuchsia::examples::shadertoy::Shadertoy::SetShaderCodeCallback callback);

  void SetResolution(uint32_t width, uint32_t height);

  void SetMouse(glm::vec4 i_mouse);

  void SetImage(uint32_t channel,
                ::fidl::InterfaceRequest<fuchsia::images::ImagePipe> request);

 protected:
  explicit ShadertoyState(App* app);

  // Tell the app to close the connection to this Shadertoy, and destroy it.
  void Close();

  // Subclasses must call this from DrawFrame().
  void OnFramePresented(fuchsia::images::PresentationInfo info);

  uint32_t width() const { return width_; }
  uint32_t height() const { return height_; }
  escher::Escher* escher() const { return escher_.get(); }
  Renderer* renderer() const { return renderer_; }
  const PipelinePtr& pipeline() const { return pipeline_; }
  escher::Texture* channel0() const { return nullptr; }
  escher::Texture* channel1() const { return nullptr; }
  escher::Texture* channel2() const { return nullptr; }
  escher::Texture* channel3() const { return nullptr; }
  glm::vec4 i_mouse() const { return i_mouse_; }
  fxl::WeakPtrFactory<ShadertoyState>* weak_ptr_factory() {
    return &weak_ptr_factory_;
  }

 private:
  // Subclasses must implement this, and call OnFramePresented() from it.
  virtual void DrawFrame(uint64_t presentation_time, float animation_time) = 0;

  // Requests a frame to be drawn.
  void RequestFrame(uint64_t presentation_time);

  // Subclasses must implement this to react when the resolution changes.
  virtual void OnSetResolution() = 0;

  static constexpr uint32_t kMaxWidth = 2048;
  static constexpr uint32_t kMaxHeight = 2048;

  App* const app_;
  const escher::EscherWeakPtr escher_;
  Compiler* const compiler_;
  Renderer* const renderer_;
  fxl::WeakPtrFactory<ShadertoyState> weak_ptr_factory_;
  PipelinePtr pipeline_;
  uint32_t width_ = 0;
  uint32_t height_ = 0;
  glm::vec4 i_mouse_ = {0, 0, 0, 0};
  bool is_paused_ = true;
  bool is_drawing_ = false;
  bool is_closed_ = false;
  escher::Stopwatch stopwatch_;
};

}  // namespace shadertoy

#endif  // GARNET_EXAMPLES_UI_SHADERTOY_SERVICE_SHADERTOY_STATE_H_
