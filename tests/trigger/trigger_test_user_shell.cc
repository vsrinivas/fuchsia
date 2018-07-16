// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/ui/views_v1_token/cpp/fidl.h>
#include <lib/component/cpp/connect.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/callback/scoped_callback.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fxl/logging.h>
#include <lib/fxl/macros.h>

#include "peridot/lib/testing/component_base.h"
#include "peridot/lib/testing/reporting.h"
#include "peridot/lib/testing/testing.h"
#include "peridot/tests/common/defs.h"
#include "peridot/tests/trigger/defs.h"

using ::modular::testing::Await;
using ::modular::testing::TestPoint;

namespace {

class TestApp
    : public modular::testing::ComponentBase<fuchsia::modular::UserShell> {
 public:
  TestApp(component::StartupContext* const startup_context)
      : ComponentBase(startup_context), weak_ptr_factory_(this) {
    TestInit(__FILE__);
  }

  ~TestApp() override = default;

 private:
  using TestPoint = modular::testing::TestPoint;

  TestPoint initialize_{"Initialize()"};
  TestPoint story_create_{"Created story."};

  // |fuchsia::modular::UserShell|
  void Initialize(fidl::InterfaceHandle<fuchsia::modular::UserShellContext>
                      user_shell_context) override {
    initialize_.Pass();

    user_shell_context_.Bind(std::move(user_shell_context));
    user_shell_context_->GetStoryProvider(story_provider_.NewRequest());

    story_provider_->CreateStory(kModuleUrl, [this](fidl::StringPtr story_id) {
      story_create_.Pass();
      StartStory(story_id);
    });
    async::PostDelayedTask(
        async_get_default_dispatcher(),
        callback::MakeScoped(weak_ptr_factory_.GetWeakPtr(),
                             [this] { user_shell_context_->Logout(); }),
        zx::msec(kTimeoutMilliseconds));
  }

  TestPoint got_queue_token_{"Got message queue token."};
  TestPoint module_finished_{"Trigger test module finished work."};
  TestPoint story_was_deleted_{"Story was deleted."};
  TestPoint agent_executed_delete_task_{
      "fuchsia::modular::Agent executed message queue task."};
  void StartStory(const std::string& story_id) {
    story_provider_->GetController(story_id, story_controller_.NewRequest());
    story_controller_.set_error_handler([this, story_id] {
      FXL_LOG(ERROR) << "Story controller for story " << story_id
                     << " died. Does this story exist?";
    });

    story_controller_->Start(story_view_.NewRequest());

    // Retrieve the message queue token for the messsage queue that the module
    // created.
    modular::testing::GetStore()->Get(
        "trigger_test_module_queue_token",
        [this, story_id](fidl::StringPtr value) {
          got_queue_token_.Pass();
          // Wait for the module to finish its test cases for communicating
          // with the agent.
          Await("trigger_test_module_done", [this, story_id, value] {
            module_finished_.Pass();
            // Delete the story to trigger the deletion of the message
            // queue that the module created.
            story_provider_->DeleteStory(story_id, [this, value]() {
              story_was_deleted_.Pass();
              // Verify that the agent task was triggered, by checking
              // that the agent wrote the message queue token to the
              // test store.
              Await(value, [this] {
                agent_executed_delete_task_.Pass();
                user_shell_context_->Logout();
              });
            });
          });
        });
  }

  fuchsia::modular::UserShellContextPtr user_shell_context_;
  fuchsia::modular::StoryProviderPtr story_provider_;
  fuchsia::modular::StoryControllerPtr story_controller_;

  fidl::InterfaceHandle<fuchsia::ui::views_v1_token::ViewOwner> story_view_;

  fxl::WeakPtrFactory<TestApp> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TestApp);
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  modular::testing::ComponentMain<TestApp>();
  return 0;
}
