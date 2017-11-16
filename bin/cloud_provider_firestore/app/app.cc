// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <thread>

#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/log_settings_command_line.h"
#include "lib/lifecycle/fidl/lifecycle.fidl.h"
#include "peridot/bin/cloud_provider_firestore/firestore/firestore_service.h"

namespace cloud_provider_firestore {
namespace {

constexpr fxl::StringView kServerIdFlag = "server-id";

void PrintUsage(const char* executable_name) {
  std::cout << "Usage: " << executable_name << " --" << kServerIdFlag
            << "=<string>" << std::endl;
}

// This is a proof-of-concept app demonstrating a single gRPC call on the
// Firestore server, to be replaced with real cloud provider.
class App : public modular::Lifecycle {
 public:
  explicit App(std::string server_id)
      : server_id_(std::move(server_id)),
        firestore_service_(loop_.task_runner(), MakeChannel()) {}

  void Run() {
    // Make a request that creates a new document with an "abc" field.
    auto request = google::firestore::v1beta1::CreateDocumentRequest();
    request.set_parent("projects/" + server_id_ +
                       "/databases/(default)/documents");
    request.set_collection_id("top-level-collection");
    google::firestore::v1beta1::Value forty_two;
    forty_two.set_integer_value(42);
    std::string key = "abc";
    (*(request.mutable_document()->mutable_fields()))[key] = forty_two;

    // Make the RPC and print the status.
    firestore_service_.CreateDocument(
        std::move(request), [this](auto status, auto result) {
          FXL_LOG(INFO) << "RPC status: " << status.error_code();
          if (!status.ok()) {
            FXL_LOG(INFO) << "error message: " << status.error_message();
            FXL_LOG(INFO) << "error details: " << status.error_details();
          }

          loop_.PostQuitTask();
        });

    loop_.Run();
  }

 private:
  // modular::Lifecycle:
  void Terminate() override { loop_.PostQuitTask(); }

  fsl::MessageLoop loop_;

  const std::string server_id_;
  FirestoreService firestore_service_;

  std::shared_ptr<grpc::Channel> MakeChannel() {
    auto opts = grpc::SslCredentialsOptions();
    auto credentials = grpc::SslCredentials(opts);
    return grpc::CreateChannel("firestore.googleapis.com:443", credentials);
  }

  FXL_DISALLOW_COPY_AND_ASSIGN(App);
};
}  // namespace

}  // namespace cloud_provider_firestore

int main(int argc, const char** argv) {
  setenv("GRPC_DEFAULT_SSL_ROOTS_FILE_PATH", "/system/data/boringssl/cert.pem",
         1);

  const auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  fxl::SetLogSettingsFromCommandLine(command_line);

  std::string server_id;
  if (!command_line.GetOptionValue(
          cloud_provider_firestore::kServerIdFlag.ToString(), &server_id)) {
    cloud_provider_firestore::PrintUsage(argv[0]);
    return -1;
  }

  cloud_provider_firestore::App app(std::move(server_id));
  app.Run();

  return 0;
}
