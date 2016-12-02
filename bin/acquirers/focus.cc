// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/src/acquirers/focus.h"

#include "apps/maxwell/services/context/client.fidl.h"
#include "apps/modular/lib/app/application_context.h"
#include "apps/modular/services/user/focus.fidl.h"

#include "lib/mtl/tasks/message_loop.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fidl/cpp/bindings/array.h"

#include "lib/ftl/logging.h"
#include "lib/ftl/time/time_delta.h"

using maxwell::acquirers::FocusAcquirer;

constexpr char FocusAcquirer::kLabel[];
constexpr char FocusAcquirer::kSchema[];

namespace {

class FocusAcquirerApp : public modular::FocusListener,
                         public maxwell::ContextPublisherController {
 public:
  FocusAcquirerApp()
      : app_ctx_(modular::ApplicationContext::CreateFromStartupInfo()),
        ctl_(this),
        focus_listener_(this) {
    srand(time(NULL));

    auto cx =
        app_ctx_->ConnectToEnvironmentService<maxwell::ContextPublisher>();

    auto focus_controller_handle =
        app_ctx_->ConnectToEnvironmentService<modular::FocusController>();
    fidl::InterfaceHandle<modular::FocusListener> focus_listener_handle;
    focus_listener_.Bind(&focus_listener_handle);
    focus_controller_handle->Watch(std::move(focus_listener_handle));

    fidl::InterfaceHandle<maxwell::ContextPublisherController> ctl_handle;
    ctl_.Bind(&ctl_handle);

    cx->Publish(FocusAcquirer::kLabel, FocusAcquirer::kSchema,
                std::move(ctl_handle), out_.NewRequest());
    PublishFocusState();
  }

  void OnFocusChanged(fidl::Array<fidl::String> ids) override {
    focused_story_ids_.clear();
    for (std::string str : ids) {
      focused_story_ids_.push_back(str);
    }

    PublishFocusState();
    FTL_LOG(INFO) << "Focus changed -- there are now "
                  << focused_story_ids_.size() << " active story ids.";
  }

  void OnHasSubscribers() override {
    FTL_LOG(INFO) << "Focus acquirer has subscribers";
  }

  void OnNoSubscribers() override {
    FTL_LOG(INFO) << "Focus acquirer subscribers lost.";
  }

 private:
  void PublishFocusState() {
    // TODO(afergan): Since right now we are not doing anything with the
    // focused stories, we just change the modular_state to reflect if there
    // are any focused stories. If we need to keep track of the actual story
    // ids, publish the vector to the context service.

    int modular_state = focused_story_ids_.size() ? 1 : 0;

    out_->Update(std::to_string(modular_state));
    FTL_VLOG(1) << ": " << modular_state;
  }

  std::unique_ptr<modular::ApplicationContext> app_ctx_;

  fidl::Binding<maxwell::ContextPublisherController> ctl_;
  maxwell::ContextPublisherLinkPtr out_;
  std::vector<std::string> focused_story_ids_;
  fidl::Binding<modular::FocusListener> focus_listener_;
};

}  // namespace

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;
  FocusAcquirerApp app;
  loop.Run();
  return 0;
}
