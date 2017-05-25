// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/view_manager/view_registry1.h"

#include "apps/mozart/src/view_manager/view_impl1.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/strings/string_printf.h"

namespace view_manager {

ViewRegistry1::ViewRegistry1(app::ApplicationContext* application_context,
                             mozart::CompositorPtr compositor)
    : ViewRegistry(application_context), compositor_(std::move(compositor)) {}

void ViewRegistry1::CreateScene(ViewState* view_state,
                                fidl::InterfaceRequest<mozart::Scene> scene) {
  FTL_DCHECK(IsViewStateRegisteredDebug(view_state));
  FTL_DCHECK(scene.is_pending());
  FTL_VLOG(1) << "CreateScene: view=" << view_state;

  compositor_->CreateScene(std::move(scene), view_state->label(), [
    this, weak = view_state->GetWeakPtr()
  ](mozart::SceneTokenPtr scene_token) {
    if (weak)
      OnViewSceneTokenAvailable(weak, std::move(scene_token));
  });
}

void ViewRegistry1::AttachResolvedViewAndNotify(ViewStub* view_stub,
                                                ViewState* view_state) {
  FTL_DCHECK(view_stub);
  FTL_DCHECK(IsViewStateRegisteredDebug(view_state));
  FTL_VLOG(2) << "AttachViewStubAndNotify: view=" << view_state;

  // Create the scene and get its token asynchronously.
  // TODO(jeffbrown): It would be really nice to have a way to pipeline
  // getting the scene token.
  mozart::ScenePtr stub_scene;
  compositor_->CreateScene(
      stub_scene.NewRequest(),
      ftl::StringPrintf("*%s", view_state->label().c_str()),
      [ this,
        weak = view_stub->GetWeakPtr() ](mozart::SceneTokenPtr scene_token) {
        if (weak)
          OnStubSceneTokenAvailable(weak, std::move(scene_token));
      });

  // Hijack the view from its current container, if needed.
  HijackView(view_state);

  // Attach the view.
  view_state->ReleaseOwner();  // don't need the ViewOwner pipe anymore
  view_stub->AttachView(view_state, std::move(stub_scene));
  ScheduleViewInvalidation(view_state, ViewState::INVALIDATION_PARENT_CHANGED);
}

std::unique_ptr<ViewImpl> ViewRegistry1::CreateViewImpl() {
  return std::make_unique<ViewImpl1>(this);
}

}  // namespace view_manager
