// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A Module that serves as the recipe in the example story, i.e. that
// creates other Modules in the story.

#include <fuchsia/modular/cpp/fidl.h>

#include <fuchsia/modular/cpp/fidl.h>
#include "lib/app/cpp/connect.h"
#include "lib/app/cpp/startup_context.h"
#include "lib/app_driver/cpp/app_driver.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fidl/cpp/interface_request.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fsl/vmo/strings.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"
#include "peridot/examples/counter_cpp/store.h"
#include "peridot/lib/fidl/array_to_string.h"
#include "peridot/lib/fidl/single_service_app.h"

namespace {

using fuchsia::modular::to_array;

// JSON data
constexpr char kInitialJson[] =
    "{     \"@type\" : \"http://schema.domokit.org/PingPongPacket\","
    "      \"http://schema.domokit.org/counter\" : 0,"
    "      \"http://schema.org/sender\" : \"RecipeImpl\""
    "}";

// Ledger keys
constexpr char kLedgerCounterKey[] = "counter_key";

// Implementation of the LinkWatcher service that forwards each document
// changed in one Link instance to a second Link instance.
class LinkForwarder : fuchsia::modular::LinkWatcher {
 public:
  LinkForwarder(fuchsia::modular::Link* const src,
                fuchsia::modular::Link* const dst)
      : src_binding_(this), src_(src), dst_(dst) {
    src_->Watch(src_binding_.NewBinding());
  }

  void Notify(fidl::StringPtr json) override {
    // We receive an initial update when the Link initializes. It's "null"
    // (meaning the value of the json string is the four letters n-u-l-l)
    // if this is a new session, or it has json data if it's a restored session.
    // In either case, it should be ignored, otherwise we can get multiple
    // messages traveling at the same time.
    if (!initial_update_ && json->size() > 0) {
      dst_->Set(nullptr, json);
    }
    initial_update_ = false;
  }

 private:
  fidl::Binding<fuchsia::modular::LinkWatcher> src_binding_;
  fuchsia::modular::Link* const src_;
  fuchsia::modular::Link* const dst_;
  bool initial_update_ = true;

  FXL_DISALLOW_COPY_AND_ASSIGN(LinkForwarder);
};

class ModuleMonitor : fuchsia::modular::ModuleWatcher {
 public:
  ModuleMonitor(fuchsia::modular::ModuleController* const module_client)
      : binding_(this) {
    module_client->Watch(binding_.NewBinding());
  }

  void OnStateChange(fuchsia::modular::ModuleState new_state) override {
    FXL_LOG(INFO) << "RecipeImpl " << new_state;
  }

 private:
  fidl::Binding<fuchsia::modular::ModuleWatcher> binding_;
  FXL_DISALLOW_COPY_AND_ASSIGN(ModuleMonitor);
};

class DeviceMapMonitor : fuchsia::modular::DeviceMapWatcher {
 public:
  DeviceMapMonitor(fuchsia::modular::DeviceMap* const device_map,
                   std::vector<fuchsia::modular::DeviceMapEntry> devices)
      : binding_(this), devices_(std::move(devices)) {
    device_map->WatchDeviceMap(binding_.NewBinding());
  }

  void OnDeviceMapChange(fuchsia::modular::DeviceMapEntry entry) override {
    FXL_LOG(INFO) << "OnDeviceMapChange() " << entry.name << " "
                  << entry.profile;
    for (const auto& device : devices_) {
      if (entry.device_id == device.device_id)
        return;
    }
    FXL_CHECK(false);
  }

 private:
  fidl::Binding<DeviceMapWatcher> binding_;
  std::vector<fuchsia::modular::DeviceMapEntry> devices_;
  FXL_DISALLOW_COPY_AND_ASSIGN(DeviceMapMonitor);
};

// Module implementation that acts as a recipe. There is one instance
// per application; the story runner creates new application instances
// to run more module instances.
class RecipeApp : public fuchsia::modular::ViewApp {
 public:
  RecipeApp(fuchsia::sys::StartupContext* const startup_context)
      : ViewApp(startup_context) {
    startup_context->ConnectToEnvironmentService(module_context_.NewRequest());
    module_context_->GetLink(nullptr, link_.NewRequest());

    // Read initial Link data. We expect the shell to tell us what it
    // is.
    link_->Get(nullptr, [this](const fidl::StringPtr& json) {
      rapidjson::Document doc;
      doc.Parse(json);
      if (doc.HasParseError()) {
        FXL_LOG(ERROR) << "Recipe Module Link has invalid JSON: " << json;
      } else {
        FXL_LOG(INFO) << "Recipe Module Link: "
                      << fuchsia::modular::JsonValueToPrettyString(doc);
      }
    });

    constexpr char kModule1Link[] = "module1";
    constexpr char kModule2Link[] = "module2";
    module_context_->GetLink(kModule1Link, module1_link_.NewRequest());
    module_context_->GetLink(kModule2Link, module2_link_.NewRequest());

    fuchsia::modular::Intent intent;
    intent.action.handler = "example_module1";
    fuchsia::modular::IntentParameterData parameter_data;
    parameter_data.set_link_name(kModule1Link);
    fuchsia::modular::IntentParameter parameter;
    parameter.name = "theOneLink";
    parameter.data = std::move(parameter_data);
    intent.parameters.push_back(std::move(parameter));
    module_context_->StartModule(
        "module1", std::move(intent), module1_.NewRequest(), nullptr,
        [](const fuchsia::modular::StartModuleStatus&) {});

    intent = fuchsia::modular::Intent();
    intent.action.handler = "example_module2";
    parameter_data = fuchsia::modular::IntentParameterData();
    parameter_data.set_link_name(kModule2Link);
    parameter = fuchsia::modular::IntentParameter();
    parameter.name = "theOneLink";
    parameter.data = std::move(parameter_data);
    intent.parameters.push_back(std::move(parameter));
    fuchsia::sys::ServiceProviderPtr services_from_module2;
    module_context_->StartModule(
        "module2", std::move(intent), module2_.NewRequest(), nullptr,
        [](const fuchsia::modular::StartModuleStatus&) {});

    connections_.emplace_back(
        new LinkForwarder(module1_link_.get(), module2_link_.get()));
    connections_.emplace_back(
        new LinkForwarder(module2_link_.get(), module1_link_.get()));

    // Also connect with the root link, to create change notifications
    // the user shell can react on.
    connections_.emplace_back(
        new LinkForwarder(module1_link_.get(), link_.get()));
    connections_.emplace_back(
        new LinkForwarder(module2_link_.get(), link_.get()));

    module_monitors_.emplace_back(new ModuleMonitor(module1_.get()));
    module_monitors_.emplace_back(new ModuleMonitor(module2_.get()));

    module1_link_->Get(nullptr, [this](const fidl::StringPtr& json) {
      if (json == "null") {
        // This must come last, otherwise LinkConnection gets a
        // notification of our own write because of the "send
        // initial values" code.
        std::vector<fidl::StringPtr> segments{modular_example::kJsonSegment,
                                              modular_example::kDocId};
        module1_link_->Set(fidl::VectorPtr<fidl::StringPtr>(segments),
                           kInitialJson);
      } else {
        link_->Get(nullptr, [this](const fidl::StringPtr& json) {
          // There is a possiblity that on re-inflation we start with a
          // deadlocked state such that neither of the child modules make
          // progress. This can happen because there is no synchronization
          // between |LinkForwarder| and |ModuleMonitor|. So we ensure that
          // ping-pong can re-start.
          module1_link_->Set(nullptr, json);
          module2_link_->Set(nullptr, json);
        });
      }
    });

    // This snippet of code demonstrates using the module's Ledger. Each time
    // this module is initialized, it updates a counter in the root page.
    // 1. Get the module's ledger.
    module_context_->GetComponentContext(component_context_.NewRequest());
    component_context_->GetLedger(
        module_ledger_.NewRequest(), [this](ledger::Status status) {
          FXL_CHECK(status == ledger::Status::OK);
          // 2. Get the root page of the ledger.
          module_ledger_->GetRootPage(
              module_root_page_.NewRequest(), [this](ledger::Status status) {
                FXL_CHECK(status == ledger::Status::OK);
                // 3. Get a snapshot of the root page.
                module_root_page_->GetSnapshot(
                    page_snapshot_.NewRequest(),
                    fidl::VectorPtr<uint8_t>::New(0), nullptr,
                    [this](ledger::Status status) {
                      FXL_CHECK(status == ledger::Status::OK);
                      // 4. Read the counter from the root page.
                      page_snapshot_->Get(
                          to_array(kLedgerCounterKey),
                          [this](ledger::Status status,
                                 fuchsia::mem::BufferPtr value) {
                            // 5. If counter doesn't exist, initialize.
                            // Otherwise, increment.
                            if (status == ledger::Status::KEY_NOT_FOUND) {
                              FXL_LOG(INFO) << "No counter in root page. "
                                               "Initializing to 1.";
                              fidl::VectorPtr<uint8_t> data;
                              data.push_back(1);
                              module_root_page_->Put(
                                  to_array(kLedgerCounterKey), std::move(data),
                                  [](ledger::Status status) {
                                    FXL_CHECK(status == ledger::Status::OK);
                                  });
                            } else {
                              FXL_CHECK(status == ledger::Status::OK);
                              std::string counter_data;
                              bool conversion =
                                  fsl::StringFromVmo(*value, &counter_data);
                              FXL_DCHECK(conversion);
                              FXL_LOG(INFO)
                                  << "Retrieved counter from root page: "
                                  << static_cast<uint32_t>(counter_data[0])
                                  << ". Incrementing.";
                              counter_data[0]++;
                              module_root_page_->Put(
                                  to_array(kLedgerCounterKey),
                                  to_array(counter_data),
                                  [](ledger::Status status) {
                                    FXL_CHECK(status == ledger::Status::OK);
                                  });
                            }
                          });
                    });
              });
        });

    startup_context->ConnectToEnvironmentService(device_map_.NewRequest());
    device_map_->Query(
        [this](fidl::VectorPtr<fuchsia::modular::DeviceMapEntry> devices) {
          FXL_LOG(INFO) << "Devices from device_map_->Query():";
          for (fuchsia::modular::DeviceMapEntry device : devices.take()) {
            FXL_LOG(INFO) << " - " << device.name;
            device_map_entries_.emplace_back(std::move(device));
          }

          device_map_monitor_.reset(new DeviceMapMonitor(
              device_map_.get(), std::move(device_map_entries_)));
          device_map_->SetCurrentDeviceProfile("5");
        });
  }

  ~RecipeApp() override = default;

 private:
  fuchsia::modular::LinkPtr link_;
  fuchsia::modular::ModuleContextPtr module_context_;

  // The following ledger interfaces are stored here to make life-time
  // management easier when chaining together lambda callbacks.
  fuchsia::modular::ComponentContextPtr component_context_;
  ledger::LedgerPtr module_ledger_;
  ledger::PagePtr module_root_page_;
  ledger::PageSnapshotPtr page_snapshot_;

  fuchsia::modular::ModuleControllerPtr module1_;
  fuchsia::modular::LinkPtr module1_link_;

  fuchsia::modular::ModuleControllerPtr module2_;
  fuchsia::modular::LinkPtr module2_link_;

  std::vector<std::unique_ptr<LinkForwarder>> connections_;
  std::vector<std::unique_ptr<ModuleMonitor>> module_monitors_;

  fuchsia::modular::DeviceMapPtr device_map_;
  std::vector<fuchsia::modular::DeviceMapEntry> device_map_entries_;
  std::unique_ptr<DeviceMapMonitor> device_map_monitor_;

  FXL_DISALLOW_COPY_AND_ASSIGN(RecipeApp);
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  fsl::MessageLoop loop;

  auto context = fuchsia::sys::StartupContext::CreateFromStartupInfo();
  fuchsia::modular::AppDriver<RecipeApp> driver(
      context->outgoing().deprecated_services(),
      std::make_unique<RecipeApp>(context.get()), [&loop] { loop.QuitNow(); });

  loop.Run();
  return 0;
}
