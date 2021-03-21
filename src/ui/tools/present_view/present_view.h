// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_TOOLS_PRESENT_VIEW_PRESENT_VIEW_H_
#define SRC_UI_TOOLS_PRESENT_VIEW_PRESENT_VIEW_H_

#include <fuchsia/io/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/ui/app/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/sys/cpp/component_context.h>
#include <zircon/types.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace present_view {

// This structure encapsulates the configuration for a View that will be displayed via |Present|.
struct ViewInfo {
  // The fuchsia component URI of the component to run.
  std::string url;
  // The command line arguments passed to the started component.
  std::vector<std::string> arguments;
  // An optional locale if locale properties need to be served.
  std::string locale;
};

// This class encapsulates the business logic for an instance of the `present_view` component.
class PresentView {
 public:
  // |PresentView| fires this callback when any of the services it connects to, including the
  // |fuchsia::ui::views::View|, experiences an error condition.
  //
  // The 2 parameters are a |PresentView|-supplied error description and the kernel-supplied status
  // code, respectively.
  using ViewErrorCallback = std::function<void(std::string, zx_status_t)>;

  PresentView(std::unique_ptr<sys::ComponentContext> context, ViewErrorCallback on_view_error);
  PresentView(const PresentView&) = delete;
  PresentView& operator=(const PresentView&) = delete;
  ~PresentView();

  // Launch a component using the specified info and ask it to be presented.
  bool Present(ViewInfo view_info);

  // Terminate a previously-launched component.
  void Kill();

 private:
  // Launches a server for `fuchsia.intl.PropertyProvider`, serving the passed-in nonempty
  // |locale|, using the |directory_request|.
  void RunIntlService(const std::string& locale,
                      fidl::InterfaceRequest<fuchsia::io::Directory> directory_request,
                      fuchsia::sys::Launcher* launcher);

  std::unique_ptr<sys::ComponentContext> context_;

  fuchsia::sys::ComponentControllerPtr intl_controller_;
  fuchsia::sys::ComponentControllerPtr view_controller_;
  fuchsia::ui::policy::PresenterPtr presenter_;
  fuchsia::ui::app::ViewProviderPtr legacy_view_provider_;
  fuchsia::ui::views::ViewPtr view_;

  ViewErrorCallback view_error_callback_;
};

}  // namespace present_view

#endif  // SRC_UI_TOOLS_PRESENT_VIEW_PRESENT_VIEW_H_
