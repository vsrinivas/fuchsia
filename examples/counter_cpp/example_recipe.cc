// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A Module that serves as the recipe in the example story, i.e. that
// creates other Modules in the story.

#include "lib/app/cpp/connect.h"
#include "lib/component/fidl/component_context.fidl.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fsl/vmo/strings.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"
#include "lib/ledger/fidl/ledger.fidl.h"
#include "lib/module/fidl/module.fidl.h"
#include "lib/module/fidl/module_context.fidl.h"
#include "lib/user/fidl/device_map.fidl.h"
#include "peridot/examples/counter_cpp/calculator.fidl.h"
#include "peridot/examples/counter_cpp/store.h"
#include "peridot/lib/fidl/array_to_string.h"
#include "peridot/lib/fidl/single_service_app.h"

namespace {

using modular::examples::Adder;
using modular::to_array;

// JSON data
constexpr char kInitialJson[] =
    "{     \"@type\" : \"http://schema.domokit.org/PingPongPacket\","
    "      \"http://schema.domokit.org/counter\" : 0,"
    "      \"http://schema.org/sender\" : \"RecipeImpl\""
    "}";

constexpr char kJsonSchema[] =
    "{"
    "  \"$schema\": \"http://json-schema.org/draft-04/schema#\","
    "  \"type\": \"object\","
    "  \"properties\": {"
    "    \"counters\": {"
    "      \"type\": \"object\","
    "      \"properties\": {"
    "        \"http://google.com/id/dc7cade7-7be0-4e23-924d-df67e15adae5\": {"
    "          \"type\": \"object\","
    "          \"properties\": {"
    "            \"@type\": {"
    "              \"type\": \"string\""
    "            },"
    "            \"http://schema.domokit.org/counter\": {"
    "              \"type\": \"integer\""
    "            },"
    "            \"http://schema.org/sender\": {"
    "              \"type\": \"string\""
    "            }"
    "          },"
    "          \"additionalProperties\" : false,"
    "          \"required\": ["
    "            \"@type\","
    "            \"http://schema.domokit.org/counter\","
    "            \"http://schema.org/sender\""
    "          ]"
    "        }"
    "      },"
    "      \"additionalProperties\" : false,"
    "      \"required\": ["
    "        \"http://google.com/id/dc7cade7-7be0-4e23-924d-df67e15adae5\""
    "      ]"
    "    }"
    "  },"
    "  \"additionalProperties\" : false,"
    "  \"required\": ["
    "    \"counters\""
    "  ]"
    "}";

// Ledger keys
constexpr char kLedgerCounterKey[] = "counter_key";

// Implementation of the LinkWatcher service that forwards each document
// changed in one Link instance to a second Link instance.
class LinkForwarder : modular::LinkWatcher {
 public:
  LinkForwarder(modular::Link* const src, modular::Link* const dst)
      : src_binding_(this), src_(src), dst_(dst) {
    src_->Watch(src_binding_.NewBinding());
  }

  void Notify(const fidl::String& json) override {
    // We receive an initial update when the Link initializes. It's "null"
    // (meaning the value of the json string is the four letters n-u-l-l)
    // if this is a new session, or it has json data if it's a restored session.
    // In either case, it should be ignored, otherwise we can get multiple
    // messages traveling at the same time.
    if (!initial_update_ && json.size() > 0) {
      dst_->Set(nullptr, json);
    }
    initial_update_ = false;
  }

 private:
  fidl::Binding<modular::LinkWatcher> src_binding_;
  modular::Link* const src_;
  modular::Link* const dst_;
  bool initial_update_ = true;

  FXL_DISALLOW_COPY_AND_ASSIGN(LinkForwarder);
};

class ModuleMonitor : modular::ModuleWatcher {
 public:
  ModuleMonitor(modular::ModuleController* const module_client,
                modular::ModuleContext* const module_context)
      : binding_(this), module_context_(module_context) {
    module_client->Watch(binding_.NewBinding());
  }

  void OnStateChange(modular::ModuleState new_state) override {
    if (new_state == modular::ModuleState::DONE) {
      FXL_LOG(INFO) << "RecipeImpl DONE";
      module_context_->Done();
    }
  }

 private:
  fidl::Binding<modular::ModuleWatcher> binding_;
  modular::ModuleContext* const module_context_;
  FXL_DISALLOW_COPY_AND_ASSIGN(ModuleMonitor);
};

class AdderImpl : public modular::examples::Adder {
 public:
  AdderImpl() = default;

 private:
  // |Adder| impl:
  void Add(int32_t a, int32_t b, const AddCallback& result) override {
    result(a + b);
  }

  FXL_DISALLOW_COPY_AND_ASSIGN(AdderImpl);
};

// Module implementation that acts as a recipe. There is one instance
// per application; the story runner creates new application instances
// to run more module instances.
class RecipeApp : modular::SingleServiceApp<modular::Module> {
 public:
  RecipeApp() = default;
  ~RecipeApp() override = default;

 private:
  // |Module|
  void Initialize(
      fidl::InterfaceHandle<modular::ModuleContext> module_context,
      fidl::InterfaceHandle<app::ServiceProvider> /*incoming_services*/,
      fidl::InterfaceRequest<app::ServiceProvider> /*outgoing_services*/)
      override {
    module_context_.Bind(std::move(module_context));
    module_context_->GetLink(nullptr, link_.NewRequest());

    // Read initial Link data. We expect the shell to tell us what it
    // is.
    link_->Get(nullptr, [this](const fidl::String& json) {
      rapidjson::Document doc;
      doc.Parse(json);
      if (doc.HasParseError()) {
        FXL_LOG(ERROR) << "Recipe Module Link has invalid JSON: " << json;
      } else {
        FXL_LOG(INFO) << "Recipe Module Link: "
                      << modular::JsonValueToPrettyString(doc);
      }
    });

    constexpr char kModule1Link[] = "module1";
    constexpr char kModule2Link[] = "module2";
    module_context_->GetLink(kModule1Link, module1_link_.NewRequest());
    module_context_->GetLink(kModule2Link, module2_link_.NewRequest());
    module1_link_->SetSchema(kJsonSchema);
    module2_link_->SetSchema(kJsonSchema);

    // Provide services for Module 1.
    app::ServiceProviderPtr services_for_module1;
    outgoing_services_.AddBinding(services_for_module1.NewRequest());
    outgoing_services_.AddService<modular::examples::Adder>(
        [this](fidl::InterfaceRequest<modular::examples::Adder> req) {
          adder_clients_.AddBinding(&adder_service_, std::move(req));
        });

    app::ServiceProviderPtr services_from_module1;
    module_context_->StartModuleInShell(
        "module1", "file:///system/apps/example_module1", kModule1Link,
        std::move(services_for_module1), services_from_module1.NewRequest(),
        module1_.NewRequest(), nullptr, true);

    // Consume services from Module 1.
    auto multiplier_service =
        app::ConnectToService<modular::examples::Multiplier>(
            services_from_module1.get());
    multiplier_service.set_connection_error_handler([] {
      FXL_CHECK(false)
          << "Uh oh, Connection to Multiplier closed by the module 1.";
    });
    multiplier_service->Multiply(
        4, 4,
        fxl::MakeCopyable([multiplier_service =
                               std::move(multiplier_service)](int32_t result) {
          FXL_CHECK(result == 16);
          FXL_LOG(INFO) << "Incoming Multiplier service: 4 * 4 is 16.";
        }));

    module_context_->StartModuleInShell(
        "module2", "file:///system/apps/example_module2", kModule2Link, nullptr,
        nullptr, module2_.NewRequest(), nullptr, true);

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

    module_monitors_.emplace_back(
        new ModuleMonitor(module1_.get(), module_context_.get()));
    module_monitors_.emplace_back(
        new ModuleMonitor(module2_.get(), module_context_.get()));

    // TODO(mesch): Good illustration of the remaining issue to
    // restart a story: Here is how does this code look like when
    // the Story is not new, but already contains existing Modules
    // and Links from the previous execution that is continued here.
    // Is that really enough?
    module1_link_->Get(nullptr, [this](const fidl::String& json) {
      if (json == "null") {
        // This must come last, otherwise LinkConnection gets a
        // notification of our own write because of the "send
        // initial values" code.
        std::vector<std::string> segments{modular_example::kJsonSegment,
                                          modular_example::kDocId};
        module1_link_->Set(fidl::Array<fidl::String>::From(segments),
                           kInitialJson);
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
                    page_snapshot_.NewRequest(), nullptr, nullptr,
                    [this](ledger::Status status) {
                      FXL_CHECK(status == ledger::Status::OK);
                      // 4. Read the counter from the root page.
                      page_snapshot_->Get(
                          to_array(kLedgerCounterKey),
                          [this](ledger::Status status, zx::vmo value) {
                            // 5. If counter doesn't exist, initialize.
                            // Otherwise, increment.
                            if (status == ledger::Status::KEY_NOT_FOUND) {
                              FXL_LOG(INFO) << "No counter in root page. "
                                               "Initializing to 1.";
                              fidl::Array<uint8_t> data;
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
                                  fsl::StringFromVmo(value, &counter_data);
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

    device_map_ = application_context()
                      ->ConnectToEnvironmentService<modular::DeviceMap>();

    device_map_->Query([](fidl::Array<modular::DeviceMapEntryPtr> devices) {
      FXL_LOG(INFO) << "Known devices:";
      for (modular::DeviceMapEntryPtr& device : devices) {
        FXL_LOG(INFO) << " - " << device->name;
      }
    });
  }

  // |Lifecycle|
  void Terminate() override {
    // TODO(mesch): This is tentative. Not sure what the right amount
    // of cleanup it is to ask from a module implementation, but this
    // disconnects all the watchers and thus prevents any further
    // state change of the module.
    connections_.clear();
    module_monitors_.clear();
    fsl::MessageLoop::GetCurrent()->QuitNow();
  }

  modular::LinkPtr link_;
  modular::ModuleContextPtr module_context_;

  // This is a ServiceProvider we expose to one of our child modules, to
  // demonstrate the use of a service exchange.
  fidl::BindingSet<modular::examples::Adder> adder_clients_;
  AdderImpl adder_service_;
  app::ServiceNamespace outgoing_services_;

  // The following ledger interfaces are stored here to make life-time
  // management easier when chaining together lambda callbacks.
  modular::ComponentContextPtr component_context_;
  ledger::LedgerPtr module_ledger_;
  ledger::PagePtr module_root_page_;
  ledger::PageSnapshotPtr page_snapshot_;

  modular::ModuleControllerPtr module1_;
  modular::LinkPtr module1_link_;

  modular::ModuleControllerPtr module2_;
  modular::LinkPtr module2_link_;

  std::vector<std::unique_ptr<LinkForwarder>> connections_;
  std::vector<std::unique_ptr<ModuleMonitor>> module_monitors_;

  modular::DeviceMapPtr device_map_;

  FXL_DISALLOW_COPY_AND_ASSIGN(RecipeApp);
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  fsl::MessageLoop loop;
  RecipeApp app;
  loop.Run();
  return 0;
}
