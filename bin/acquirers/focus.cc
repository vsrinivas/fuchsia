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
                         public maxwell::context::PublisherController {
 public:
  FocusAcquirerApp()
      : app_ctx_(modular::ApplicationContext::CreateFromStartupInfo()),
        ctl_(this) {
    srand(time(NULL));

    auto cx = app_ctx_->ConnectToEnvironmentService<
        maxwell::context::ContextAcquirerClient>();

    fidl::InterfaceHandle<maxwell::context::PublisherController> ctl_handle;
    ctl_.Bind(&ctl_handle);

    cx->Publish(FocusAcquirer::kLabel, FocusAcquirer::kSchema,
                std::move(ctl_handle), GetProxy(&out_));
  }

  void OnFocusChanged(fidl::Array<fidl::String> ids) override {
    focused_story_ids.clear();
    for (std::string str : ids) {
      focused_story_ids.push_back(str);
    }
    PublishFocusState();
    FTL_LOG(INFO) << "Focus changed -- there are now "
                  << focused_story_ids.size() << " active story ids.";
  }

  void OnHasSubscribers() override {
    FTL_LOG(INFO) << "Focus acquirer has subscribers";
  }

  void OnNoSubscribers() override {
    FTL_LOG(INFO) << "Focus acquirer subscribers lost.";
  }

 private:
  inline void PublishFocusState() {
    // TODO(afergan): Since right now we are not doing anything with the
    // focused stories, we just change the modular_state to reflect if there
    // are any focused stories. If we need to keep track of the actual story
    // ids, publish the vector to the context service.

    std::ostringstream json;
    json << "{ \"modular_state\": " << (focused_story_ids.size() ? 1 : 0)
         << " }";
    FTL_VLOG(1) << ": " << json.str();

    out_->Update(json.str());
  }

  std::unique_ptr<modular::ApplicationContext> app_ctx_;

  fidl::Binding<maxwell::context::PublisherController> ctl_;
  maxwell::context::PublisherLinkPtr out_;
  std::vector<std::string> focused_story_ids;
};

}  // namespace

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;
  FocusAcquirerApp app;
  loop.Run();
  return 0;
}
