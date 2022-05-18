// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/examples/routing/echo/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/channel.h>
#include <zircon/processargs.h>

// [START imports]
#include <fuchsia/component/cpp/fidl.h>
#include <fuchsia/component/decl/cpp/fidl.h>
// [END imports]

// Controller instance to manage sequence of asynchronous FIDL operations
// using the fuchsia.component.Realm protocol.
class ChildRequestManager {
 public:
  explicit ChildRequestManager(std::unique_ptr<sys::ComponentContext> context, async::Loop* loop)
      : loop_(loop) {
    // Connect to the fuchsia.component.Realm framework protocol
    context->svc()->Connect(realm_proxy_.NewRequest());
  }

  // Start the process to create a dynamic child instance in the collection,
  // send a protocol request, then destroy the child instance.
  void StartChildRequest(const std::string& message) {
    FX_LOGS(INFO) << "Sending request: " << message;

    // Save the current message to send
    message_ = message;
    // Restart the loop
    loop_->ResetQuit();

    CreateDynamicChild();
  }

  // [START create_child]
  // Use the fuchsia.component.Realm protocol to create a dynamic
  // child instance in the collection.
  void CreateDynamicChild() {
    fuchsia::component::decl::CollectionRef collection_ref = {
        .name = "echo",
    };
    fuchsia::component::decl::Child child_decl;
    child_decl.set_name("lifecycle_dynamic");
    child_decl.set_url("#meta/echo_server.cm");
    child_decl.set_startup(fuchsia::component::decl::StartupMode::LAZY);

    realm_proxy_->CreateChild(std::move(collection_ref), std::move(child_decl),
                              fuchsia::component::CreateChildArgs(),
                              [&](fuchsia::component::Realm_CreateChild_Result result) {
                                ZX_ASSERT(!result.is_err());
                                FX_LOGS(INFO) << "Dynamic child instance created.";

                                ConnectDynamicChild();
                              });
  }
  // [END create_child]

  // [START destroy_child]
  // Use the fuchsia.component.Realm protocol to destroy the dynamic
  // child instance running in the collection.
  void DestroyDynamicChild() {
    fuchsia::component::decl::ChildRef child_ref = {
        .name = "lifecycle_dynamic",
        .collection = "echo",
    };

    realm_proxy_->DestroyChild(std::move(child_ref),
                               [&](fuchsia::component::Realm_DestroyChild_Result result) {
                                 ZX_ASSERT(!result.is_err());
                                 FX_LOGS(INFO) << "Dynamic child instance destroyed.";

                                 // Terminate the loop
                                 loop_->Quit();
                               });
  }
  // [END destroy_child]

  // [START connect_child]
  // Use the fuchsia.component.Realm protocol to open the exposed directory of
  // the dynamic child instance.
  void ConnectDynamicChild() {
    fuchsia::component::decl::ChildRef child_ref = {
        .name = "lifecycle_dynamic",
        .collection = "echo",
    };

    fidl::InterfaceHandle<fuchsia::io::Directory> exposed_dir;
    realm_proxy_->OpenExposedDir(
        child_ref, exposed_dir.NewRequest(),
        [this, exposed_dir = std::move(exposed_dir)](
            fuchsia::component::Realm_OpenExposedDir_Result result) mutable {
          ZX_ASSERT(!result.is_err());
          std::shared_ptr<sys::ServiceDirectory> svc = std::make_shared<sys::ServiceDirectory>(
              sys::ServiceDirectory(std::move(exposed_dir)));

          SendEchoRequest(svc);
        });
  }
  // [END connect_child]

  // [START echo_send]
  // Connect to the fidl.examples.routing.echo capability exposed by the child's
  // service directory.
  void SendEchoRequest(std::shared_ptr<sys::ServiceDirectory> svc_directory) {
    // Connect to the protocol inside the child's exposed directory
    svc_directory->Connect(echo_proxy_.NewRequest());

    // Send a protocol request
    echo_proxy_->EchoString(message_, [&](fidl::StringPtr response) {
      FX_LOGS(INFO) << "Server response: " << response;
      DestroyDynamicChild();
    });
  }
  // [END echo_send]

 private:
  std::string message_;
  fidl::examples::routing::echo::EchoPtr echo_proxy_;
  fuchsia::component::RealmPtr realm_proxy_;
  async::Loop* loop_;
};

int main(int argc, const char** argv) {
  syslog::SetTags({"lifecycle", "example"});

  // Create the main async event loop and component context
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto context = sys::ComponentContext::Create();

  // Connect to the fuchsia.component.Binder capability exposed by the static
  // child instance, causing it to start.
  FX_LOGS(INFO) << "Starting lifecycle child instance.";
  fuchsia::component::BinderPtr binder_proxy;
  context->svc()->Connect(binder_proxy.NewRequest());

  // Submit a request for each program argument, and wait for the result to
  // complete.
  auto request_manager = ChildRequestManager(std::move(context), &loop);
  for (int i = 1; i < argc; i++) {
    request_manager.StartChildRequest(argv[i]);
    loop.Run();
  }

  return 0;
}
