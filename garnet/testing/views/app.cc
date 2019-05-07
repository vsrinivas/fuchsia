// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <functional>
#include <map>
#include <memory>
#include <ostream>
#include <sstream>

#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl/cpp/binding_set.h>
#include <src/lib/fxl/command_line.h>
#include <src/lib/fxl/log_settings_command_line.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <lib/zx/eventpair.h>

#include "garnet/testing/views/background_view.h"
#include "garnet/testing/views/coordinate_test_view.h"
#include "garnet/testing/views/rotated_square_view.h"
#include "garnet/testing/views/test_view.h"

namespace {

using ViewFactory =
    std::function<std::unique_ptr<scenic::TestView>(scenic::ViewContext)>;

template <class T>
ViewFactory ViewFactoryImpl() {
  return [](scenic::ViewContext view_context) {
    return std::make_unique<T>(std::move(view_context));
  };
}

const std::map<std::string, ViewFactory> kViews{
    {"background_view", ViewFactoryImpl<scenic::BackgroundView>()},
    {"rotated_square_view", ViewFactoryImpl<scenic::RotatedSquareView>()},
    {"coordinate_test_view", ViewFactoryImpl<scenic::CoordinateTestView>()}};

class App : public fuchsia::ui::views::View {
 public:
  App(component::StartupContext* context, ViewFactory view_factory)
      : context_(context), view_factory_(std::move(view_factory)) {}

 private:
  // |fuchsia::ui::view::View|
  void Present2(fuchsia::ui::views::ViewToken view_token) override {
    auto scenic =
        context_->ConnectToEnvironmentService<fuchsia::ui::scenic::Scenic>();
    scenic::ViewContext view_context = {
        .session_and_listener_request =
        scenic::CreateScenicSessionPtrAndListenerRequest(scenic.get()),
        .view_token = std::move(view_token),
    };

    view_ = view_factory_(std::move(view_context));
  }

  component::StartupContext* context_;
  ViewFactory view_factory_;
  std::unique_ptr<scenic::TestView> view_;
};

void DumpSupportedViews(std::ostringstream* msg) {
  *msg << std::endl << "Choose from:";
  for (const auto& entry : kViews) {
    *msg << std::endl << "    " << entry.first;
  }
}

}  // namespace

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);

  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(command_line))
    return 1;

  if (command_line.positional_args().empty()) {
    std::ostringstream msg("Missing view argument.");
    DumpSupportedViews(&msg);
    FXL_LOG(ERROR) << msg.str();
    return 1;
  }

  std::string view_name = command_line.positional_args().front();
  auto view_factory_it = kViews.find(view_name);
  if (view_factory_it == kViews.end()) {
    std::ostringstream msg;
    msg << "Unsupported view " << view_name << ".";
    DumpSupportedViews(&msg);
    FXL_LOG(ERROR) << msg.str();
    return 1;
  }

  auto context = component::StartupContext::CreateFromStartupInfo();
  App app(context.get(), view_factory_it->second);
  fidl::BindingSet<fuchsia::ui::views::View> view_bindings_;
  context->outgoing().AddPublicService(view_bindings_.GetHandler(&app));

  loop.Run();
  return 0;
}
