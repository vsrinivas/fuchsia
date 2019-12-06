// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_EXAMPLES_SIMPLEST_EMBEDDER_EXAMPLE_PRESENTER_H_
#define SRC_UI_EXAMPLES_SIMPLEST_EMBEDDER_EXAMPLE_PRESENTER_H_

#include <fuchsia/ui/gfx/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/ui/scenic/cpp/resources.h>
#include <lib/ui/scenic/cpp/session.h>
#include <lib/zx/eventpair.h>

namespace simplest_embedder {

// This is a |Presenter| that is used to present a ShadertoyEmbedderView.  For
// simplicity we don't run it in a separate process and connect to it via FIDL.
// Instead, the example directly creates a pair of tokens that are used to
// create a ViewHolder/View pair; the ExamplePresenter gets one and the
// ShadertoyEmbedderView gets the other.  See main().
//
// NB: This Presenter is currently *not* set up to receive input events from
// Zircon.  It is the Presenter's responsibility to convey input events to
// Scenic for further dispatch.  See HelloInput for an example of how to do it.
class ExamplePresenter : private fuchsia::ui::policy::Presenter {
 public:
  ExamplePresenter(fuchsia::ui::scenic::Scenic* scenic);
  ~ExamplePresenter() = default;

  // |Presenter|
  void PresentView(
      fuchsia::ui::views::ViewHolderToken view_holder_token,
      fidl::InterfaceRequest<fuchsia::ui::policy::Presentation> presentation_request) override;
  void PresentOrReplaceView(
      fuchsia::ui::views::ViewHolderToken view_holder_token,
      fidl::InterfaceRequest<fuchsia::ui::policy::Presentation> presentation_request) override;
  void HACK_SetRendererParams(bool enable_clipping,
                              std::vector<::fuchsia::ui::gfx::RendererParam> params) override{};

  void Init(float width, float height);

 private:
  class Presentation {
   public:
    Presentation(scenic::Session* session, fuchsia::ui::views::ViewHolderToken view_holder_token);

    void SetSize(float width, float height);

    const scenic::Layer& layer() const { return layer_; }

   private:
    scenic::Layer layer_;
    scenic::EntityNode view_holder_node_;
    scenic::ViewHolder view_holder_;
  };

  void MaybeSetPresentationSize();
  void ScenicSessionPresent();

  scenic::Session session_;
  std::unique_ptr<scenic::DisplayCompositor> compositor_;
  scenic::LayerStack layers_;

  std::unique_ptr<Presentation> presentation_;
  float width_ = 0.f;
  float height_ = 0.f;
};

}  // namespace simplest_embedder

#endif  // SRC_UI_EXAMPLES_SIMPLEST_EMBEDDER_EXAMPLE_PRESENTER_H_
