// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_VIEW_MANAGER_VIEW_STATE_H_
#define GARNET_BIN_UI_VIEW_MANAGER_VIEW_STATE_H_

#include <memory>
#include <string>

#include <fuchsia/ui/viewsv1/cpp/fidl.h>

#include "garnet/bin/ui/view_manager/view_container_state.h"
#include "lib/fidl/cpp/binding.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/memory/weak_ptr.h"
#include "lib/ui/scenic/cpp/resources.h"
#include "lib/ui/views/cpp/formatting.h"

namespace view_manager {

class ViewRegistry;
class ViewImpl;
class ViewState;
class ViewStub;

// Describes the state of a particular view.
// This object is owned by the ViewRegistry that created it.
class ViewState : public ViewContainerState {
 public:
  ViewState(ViewRegistry* registry, uint32_t view_token,
            fidl::InterfaceRequest<::fuchsia::ui::viewsv1::View> view_request,
            ::fuchsia::ui::viewsv1::ViewListenerPtr view_listener,
            zx::eventpair scenic_view_token, zx::eventpair parent_export_token,
            fuchsia::ui::scenic::Scenic* scenic, const std::string& label);
  ~ViewState() override;

  fxl::WeakPtr<ViewState> GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

  // Gets the token used to refer to this view globally.
  // Caller does not obtain ownership of the token.
  uint32_t view_token() const { return view_token_; }

  void ReleaseScenicResources();

  // Gets the view listener interface, never null.
  // Caller does not obtain ownership of the view listener.
  const ::fuchsia::ui::viewsv1::ViewListenerPtr& view_listener() const {
    return view_listener_;
  }

  // Gets the view's attachment point.
  scenic::EntityNode& top_node() { return *top_node_; }

  // Gets or sets the view stub which links this view into the
  // view hierarchy, or null if the view isn't linked anywhere.
  ViewStub* view_stub() const { return view_stub_; }
  void set_view_stub(ViewStub* view_stub) { view_stub_ = view_stub; }

  ViewState* AsViewState() override;

  const std::string& label() const { return label_; }
  const std::string& FormattedLabel() const override;

  fuchsia::sys::ServiceProvider* GetServiceProviderIfSupports(
      std::string service_name);

  void SetServiceProvider(
      fidl::InterfaceHandle<fuchsia::sys::ServiceProvider> service_provider,
      std::vector<std::string> service_names);

 private:
  void OnScenicEvent(std::vector<fuchsia::ui::scenic::Event> events);

  ViewRegistry* const registry_;
  uint32_t view_token_;
  ::fuchsia::ui::viewsv1::ViewListenerPtr view_listener_;

  const std::string label_;
  mutable std::string formatted_label_cache_;

  scenic::Session session_;

  std::optional<scenic::EntityNode> top_node_;
  std::optional<scenic::View> scenic_view_;

  std::unique_ptr<ViewImpl> impl_;
  fidl::Binding<::fuchsia::ui::viewsv1::View> view_binding_;
  ViewLinker::ImportLink owner_link_;
  ViewStub* view_stub_ = nullptr;

  fuchsia::sys::ServiceProviderPtr service_provider_;
  std::vector<std::string> service_names_;

  fxl::WeakPtrFactory<ViewState> weak_factory_;  // must be last

  FXL_DISALLOW_COPY_AND_ASSIGN(ViewState);
};

}  // namespace view_manager

#endif  // GARNET_BIN_UI_VIEW_MANAGER_VIEW_STATE_H_
