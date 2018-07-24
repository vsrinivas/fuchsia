// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_VIEW_MANAGER_VIEW_STATE_H_
#define GARNET_BIN_UI_VIEW_MANAGER_VIEW_STATE_H_

#include <memory>
#include <string>

#include <fuchsia/ui/viewsv1/cpp/fidl.h>
#include "garnet/bin/ui/view_manager/internal/view_inspector.h"
#include "garnet/bin/ui/view_manager/view_container_state.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/weak_ptr.h"
#include "lib/ui/scenic/cpp/resources.h"
#include "lib/ui/views/cpp/formatting.h"

namespace view_manager {

class ViewRegistry;
class ViewImpl;

// Describes the state of a particular view.
// This object is owned by the ViewRegistry that created it.
class ViewState : public ViewContainerState {
 public:
  enum {
    // Properties may have changed and must be resolved.
    INVALIDATION_PROPERTIES_CHANGED = 1u << 0,

    // View's parent changed, may require resolving properties.
    INVALIDATION_PARENT_CHANGED = 1u << 1,

    // Next invalidation should carry all properties.
    INVALIDATION_RESEND_PROPERTIES = 1u << 2,

    // View invalidation is in progress, awaiting a reply.
    INVALIDATION_IN_PROGRESS = 1u << 3,

    // View invalidation was stalled because the view took too long to
    // respond before a subsequent invalidation was triggered so it must
    // be rescheduled.
    INVALIDATION_STALLED = 1u << 4,
  };

  ViewState(ViewRegistry* registry, ::fuchsia::ui::viewsv1token::ViewToken view_token,
            fidl::InterfaceRequest<::fuchsia::ui::viewsv1::View> view_request,
            ::fuchsia::ui::viewsv1::ViewListenerPtr view_listener,
            scenic::Session* session, const std::string& label);
  ~ViewState() override;

  fxl::WeakPtr<ViewState> GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

  // Gets the token used to refer to this view globally.
  // Caller does not obtain ownership of the token.
  const ::fuchsia::ui::viewsv1token::ViewToken& view_token() const { return view_token_; }

  // Gets the view listener interface, never null.
  // Caller does not obtain ownership of the view listener.
  const ::fuchsia::ui::viewsv1::ViewListenerPtr& view_listener() const {
    return view_listener_;
  }

  // Gets the view's attachment point.
  scenic::EntityNode& top_node() { return top_node_; }

  // Gets or sets the view stub which links this view into the
  // view hierarchy, or null if the view isn't linked anywhere.
  ViewStub* view_stub() const { return view_stub_; }
  void set_view_stub(ViewStub* view_stub) { view_stub_ = view_stub; }

  // Gets the properties the view was asked to apply, after applying
  // any inherited properties from the container, or null if none set.
  // This value is preserved across reparenting.
  const ::fuchsia::ui::viewsv1::ViewPropertiesPtr& issued_properties() const {
    return issued_properties_;
  }

  // Sets the requested properties.
  // Sets |issued_properties_valid()| to true if |properties| is not null.
  void IssueProperties(::fuchsia::ui::viewsv1::ViewPropertiesPtr properties);

  // Gets or sets flags describing the invalidation state of the view.
  uint32_t invalidation_flags() const { return invalidation_flags_; }
  void set_invalidation_flags(uint32_t value) { invalidation_flags_ = value; }

  // Binds the |ViewOwner| interface to the view which has the effect of
  // tying the view's lifetime to that of the owner's pipe.
  void BindOwner(
      fidl::InterfaceRequest<::fuchsia::ui::viewsv1token::ViewOwner> view_owner_request);

  // Unbinds the view from its owner.
  void ReleaseOwner();

  ViewState* AsViewState() override;

  const std::string& label() const { return label_; }
  const std::string& FormattedLabel() const override;

  const FocusChain* focus_chain() {
    // TODO(jpoichet) Focus chain should be built when view tree is modified
    // or by a focus chain management API.
    RebuildFocusChain();
    return focus_chain_.get();
  }

  fuchsia::sys::ServiceProvider* GetServiceProviderIfSupports(
      std::string service_name);

  void SetServiceProvider(
      fidl::InterfaceHandle<fuchsia::sys::ServiceProvider> service_provider,
      fidl::VectorPtr<fidl::StringPtr> service_names);

 private:
  void RebuildFocusChain();

  ::fuchsia::ui::viewsv1token::ViewToken view_token_;
  ::fuchsia::ui::viewsv1::ViewListenerPtr view_listener_;
  scenic::EntityNode top_node_;

  const std::string label_;
  mutable std::string formatted_label_cache_;

  std::unique_ptr<ViewImpl> impl_;
  fidl::Binding<::fuchsia::ui::viewsv1::View> view_binding_;
  fidl::Binding<::fuchsia::ui::viewsv1token::ViewOwner> owner_binding_;

  ViewStub* view_stub_ = nullptr;

  ::fuchsia::ui::viewsv1::ViewPropertiesPtr issued_properties_;

  uint32_t invalidation_flags_ = 0u;

  std::unique_ptr<FocusChain> focus_chain_;
  fuchsia::sys::ServiceProviderPtr service_provider_;
  fidl::VectorPtr<fidl::StringPtr> service_names_;

  fxl::WeakPtrFactory<ViewState> weak_factory_;  // must be last

  FXL_DISALLOW_COPY_AND_ASSIGN(ViewState);
};

}  // namespace view_manager

#endif  // GARNET_BIN_UI_VIEW_MANAGER_VIEW_STATE_H_
