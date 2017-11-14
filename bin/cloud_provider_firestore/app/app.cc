// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <thread>

#include <google/firestore/v1beta1/document.pb.h>
#include <google/firestore/v1beta1/firestore.grpc.pb.h>
#include <grpc++/grpc++.h>

#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/log_settings_command_line.h"
#include "lib/lifecycle/fidl/lifecycle.fidl.h"
#include "peridot/bin/ledger/callback/auto_cleanable.h"

namespace cloud_provider_firestore {
namespace {

constexpr fxl::StringView kServerIdFlag = "server-id";

void PrintUsage(const char* executable_name) {
  std::cout << "Usage: " << executable_name << " --" << kServerIdFlag
            << "=<string>" << std::endl;
}

struct DocumentResponseCall {
  void set_on_empty(fxl::Closure on_empty) { this->on_empty = on_empty; }

  // Context used to make the remote call.
  grpc::ClientContext context;

  // Reader used to retrieve the result of the remote call.
  std::unique_ptr<
      grpc::ClientAsyncResponseReader<google::firestore::v1beta1::Document>>
      response_reader;

  // Response of the remote call.
  google::firestore::v1beta1::Document response;

  // Response status of the remote call.
  grpc::Status status;

  // Callback to be called upon completing the remote call.
  fxl::Closure on_complete;

  // Callback to be called when the call object can be deleted.
  fxl::Closure on_empty;
};

// Wrapper over the Firestore connection exposing an asynchronous API.
//
// Requests methods are assumed to be called on the |main_runner| thread. All
// client callbacks are called on the |main_runner|.
//
// Internally, the class uses a polling thread to wait for request completion.
class FirestoreService {
 public:
  FirestoreService(fxl::RefPtr<fxl::TaskRunner> main_runner,
                   std::shared_ptr<grpc::Channel> channel)
      : main_runner_(std::move(main_runner)),
        firestore_(google::firestore::v1beta1::Firestore::NewStub(channel)) {
    polling_thread_ = std::thread(&FirestoreService::Poll, this);
  }

  ~FirestoreService() {
    cq_.Shutdown();
    polling_thread_.join();
  }

  void CreateDocument(
      google::firestore::v1beta1::CreateDocumentRequest request,
      std::function<void(grpc::Status status,
                         google::firestore::v1beta1::Document document)>
          callback) {
    FXL_DCHECK(main_runner_->RunsTasksOnCurrentThread());

    DocumentResponseCall& call = document_response_calls_.emplace();

    call.response_reader =
        firestore_->AsyncCreateDocument(&call.context, request, &cq_);

    call.on_complete = [&call, callback = std::move(callback)] {
      callback(std::move(call.status), std::move(call.response));
      if (call.on_empty) {
        call.on_empty();
      }
    };
    call.response_reader->Finish(&call.response, &call.status,
                                 &call.on_complete);
  }

 private:
  void Poll() {
    void* tag;
    bool ok = false;
    while (cq_.Next(&tag, &ok)) {
      if (!ok) {
        // This happens after cq_.Shutdown() is called.
        break;
      }

      fxl::Closure* on_complete = reinterpret_cast<fxl::Closure*>(tag);
      main_runner_->PostTask([on_complete] { (*on_complete)(); });
    }
  }

  fxl::RefPtr<fxl::TaskRunner> main_runner_;
  std::thread polling_thread_;

  std::unique_ptr<google::firestore::v1beta1::Firestore::Stub> firestore_;
  grpc::CompletionQueue cq_;

  callback::AutoCleanableSet<DocumentResponseCall> document_response_calls_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FirestoreService);
};

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
