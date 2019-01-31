// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/app_driver/cpp/module_driver.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/component/cpp/connect.h>
#include <lib/fsl/vmo/strings.h>
#include <lib/fxl/functional/make_copyable.h>
#include <test/peridot/tests/trigger/cpp/fidl.h>

#include "peridot/lib/entity/entity_watcher_impl.h"
#include "peridot/lib/rapidjson/rapidjson.h"
#include "peridot/public/lib/integration_testing/cpp/reporting.h"
#include "peridot/public/lib/integration_testing/cpp/testing.h"
#include "peridot/tests/module_context/defs.h"

using ::modular::testing::Await;
using ::modular::testing::Signal;
using ::modular::testing::TestPoint;
using ::test::peridot::tests::trigger::TriggerTestServicePtr;

namespace {

// Cf. README.md for what this test does and how.
class TestApp {
 public:
  TestPoint initialized_{"Entity module initialized"};
  TestPoint created_entity_{"Created entity"};
  TestPoint entity_data_correct_{"Entity data correct"};
  TestPoint entity_data_correct_after_resolution_{
      "Entity data correct after entity resolution"};
  TestPoint watch_data_correct_{"Entity watch returned correct data"};
  TestApp(modular::ModuleHost* const module_host,
          fidl::InterfaceRequest<
              fuchsia::ui::app::ViewProvider> /*view_provider_request*/)
      : module_context_(module_host->module_context()),
        entity_watcher_binding_(&entity_watcher_) {
    modular::testing::Init(module_host->startup_context(), __FILE__);
    initialized_.Pass();

    const char kTestString[] = "test";
    const char kTestType[] = "com.fuchsia.test";
    const char kUpdatedString[] = "updated";
    fsl::SizedVmo vmo;
    fsl::VmoFromString(kTestString, &vmo);
    module_context_->CreateEntity(
        kTestType, std::move(vmo).ToTransport(), entity_.NewRequest(),
        fxl::MakeCopyable([this](fidl::StringPtr entity_reference) mutable {
          if (!entity_reference || entity_reference->empty()) {
            modular::testing::Fail("Failed to create entity.");
            return;
          } else {
            created_entity_.Pass();
          }
        }));

    // Register a watcher and make sure it is notified with the correct data
    // when the entity value is updated.
    entity_watcher_.SetOnUpdated(
        [this, kUpdatedString](std::unique_ptr<fuchsia::mem::Buffer> value) {
          if (value) {
            std::string data_string;
            fsl::StringFromVmo(*value, &data_string);

            if (data_string == kUpdatedString) {
              watch_data_correct_.Pass();
              Signal(kEntityModuleDoneSecondTask);
            }
          }
        });

    entity_->Watch(kTestType, entity_watcher_binding_.NewBinding());

    // Fetch the data and verify that it matches the data used to create the
    // entity.
    entity_->GetData(
        kTestType,
        fxl::MakeCopyable([this, kTestString](fuchsia::mem::BufferPtr data) {
          if (data) {
            std::string data_string;
            fsl::StringFromVmo(*data, &data_string);
            if (data_string == kTestString) {
              entity_data_correct_.Pass();
            }
          }
          Signal(kEntityModuleDoneFirstTask);
        }));

    // Fetch the reference from the entity to verify the round-trip
    // resolution.
    entity_->GetReference(
        fxl::MakeCopyable([this, kTestString, kTestType,
                           kUpdatedString](fidl::StringPtr entity_reference) {
          // Grab the entity resolver from the component context.
          fuchsia::modular::EntityResolverPtr entity_resolver;
          fuchsia::modular::ComponentContextPtr component_context;
          module_context_->GetComponentContext(component_context.NewRequest());
          component_context->GetEntityResolver(entity_resolver.NewRequest());

          // Resolve the entity and verify the data is correct.
          fuchsia::modular::EntityPtr resolved_entity;
          entity_resolver->ResolveEntity(entity_reference,
                                         resolved_entity.NewRequest());
          resolved_entity->GetData(
              kTestType,
              fxl::MakeCopyable([this, kTestString, kTestType, kUpdatedString,
                                 resolved_entity = std::move(resolved_entity)](
                                    fuchsia::mem::BufferPtr data) {
                if (data) {
                  std::string data_string;
                  fsl::StringFromVmo(*data, &data_string);
                  if (data_string == kTestString) {
                    entity_data_correct_after_resolution_.Pass();
                  }
                }

                fsl::SizedVmo vmo;
                fsl::VmoFromString(kUpdatedString, &vmo);
                entity_->WriteData(
                    kTestType, std::move(vmo).ToTransport(),
                    [](fuchsia::modular::EntityWriteStatus status) {});
              }));
        }));
  }

  TestApp(modular::ModuleHost* const module_host,
          fidl::InterfaceRequest<
              fuchsia::ui::viewsv1::ViewProvider> /*view_provider_request*/)
      : TestApp(
            module_host,
            fidl::InterfaceRequest<fuchsia::ui::app::ViewProvider>(nullptr)) {}

  TestPoint stopped_{"Entity module stopped"};
  void Terminate(const std::function<void()>& done) {
    stopped_.Pass();
    modular::testing::Done(done);
  }

 private:
  fuchsia::modular::EntityPtr entity_;
  std::string module_name_ = "";
  fuchsia::modular::OngoingActivityPtr ongoing_activity_;
  fuchsia::modular::ModuleContext* module_context_;
  modular::EntityWatcherImpl entity_watcher_;
  fidl::Binding<fuchsia::modular::EntityWatcher> entity_watcher_binding_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TestApp);
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  auto context = component::StartupContext::CreateFromStartupInfo();
  modular::ModuleDriver<TestApp> driver(context.get(),
                                        [&loop] { loop.Quit(); });
  loop.Run();
  return 0;
}
