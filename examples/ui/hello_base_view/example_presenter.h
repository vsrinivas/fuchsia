// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_UI_HELLO_BASE_VIEW_EXAMPLE_PRESENTER_H_
#define GARNET_EXAMPLES_UI_HELLO_BASE_VIEW_EXAMPLE_PRESENTER_H_

#include <fuchsia/ui/policy/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>

#include "lib/ui/scenic/cpp/resources.h"
#include "lib/ui/scenic/cpp/session.h"

namespace hello_base_view {

// This is a Presenter2 that is used to present a ShadertoyEmbedderView.  For
// simplicity we don't run it in a separate process and connect to it via FIDL.
// Instead, the example directly creates a pair of zx::eventpairs that are used
// to create a ViewHolder/View pair; the ExamplePresenter gets one and the
// ShadertoyEmbedderView gets the other.  See main().
class ExamplePresenter : private fuchsia::ui::policy::Presenter2 {
 public:
  ExamplePresenter(fuchsia::ui::scenic::Scenic* scenic);
  ~ExamplePresenter() = default;

  // |Presenter2|
  void PresentView(zx::eventpair view_holder_token,
                   ::fidl::InterfaceRequest<fuchsia::ui::policy::Presentation>
                       ignored) override;

  void PresentLayer(zx::eventpair layer_import_token) override {
    FXL_CHECK(false) << "Not implemented, not called.";
  }

  void Init(float width, float height);

 private:
  class Presentation {
   public:
    Presentation(scenic::Session* session, zx::eventpair view_holder_token);

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

}  // namespace hello_base_view

#endif  // GARNET_EXAMPLES_UI_HELLO_BASE_VIEW_EXAMPLE_PRESENTER_H_
