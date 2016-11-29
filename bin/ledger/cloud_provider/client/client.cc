// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/cloud_provider/client/client.h"

#include <iostream>

#include "apps/ledger/src/cloud_provider/impl/cloud_provider_impl.h"
#include "apps/ledger/src/cloud_provider/public/types.h"
#include "apps/ledger/src/configuration/configuration.h"
#include "apps/ledger/src/configuration/configuration_encoder.h"
#include "apps/ledger/src/firebase/encoding.h"
#include "apps/ledger/src/firebase/firebase_impl.h"
#include "apps/ledger/src/glue/crypto/rand.h"
#include "apps/modular/lib/app/connect.h"
#include "apps/network/services/network_service.fidl.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/strings/concatenate.h"
#include "lib/ftl/strings/string_view.h"
#include "lib/ftl/time/time_delta.h"
#include "lib/ftl/time/time_point.h"
#include "lib/mtl/tasks/message_loop.h"
#include "lib/mtl/vmo/strings.h"

namespace cloud_provider {

namespace {

std::string RandomString() {
  return std::to_string(rand());
}

void ok(ftl::StringView what, ftl::StringView message = "") {
  std::cout << " > [OK] " << what;
  if (message.size()) {
    std::cout << ": " << message;
  }
  std::cout << std::endl;
}

void ok(ftl::StringView what, ftl::TimeDelta request_time) {
  std::cout << " > [OK] " << what << ": request time "
            << request_time.ToMilliseconds() << " ms" << std::endl;
}

void error(ftl::StringView what, ftl::StringView message) {
  std::cout << " > [FAILED] " << what;
  if (message.size()) {
    std::cout << " " << message;
  }
  std::cout << std::endl;
  mtl::MessageLoop::GetCurrent()->PostQuitTask();
}

void error(ftl::StringView what, Status status) {
  std::cout << " > [FAILED] " << what << ": cloud provider status " << status
            << std::endl;
  mtl::MessageLoop::GetCurrent()->PostQuitTask();
}

std::string GetFirebasePrefix(ftl::StringView user_prefix,
                              ftl::StringView page_id) {
  return ftl::Concatenate({firebase::EncodeKey(user_prefix), "/",
                           firebase::EncodeKey("debug_cloud_sync"), "/",
                           firebase::EncodeKey(page_id)});
}

}  // namespace

ClientApp::ClientApp(ftl::CommandLine command_line)
    : command_line_(std::move(command_line)),
      context_(modular::ApplicationContext::CreateFromStartupInfo()) {
  Start();
}

void ClientApp::OnRemoteCommit(Commit commit, std::string timestamp) {
  if (on_remote_commit_) {
    on_remote_commit_(std::move(commit), std::move(timestamp));
  }
}

void ClientApp::OnConnectionError() {
  if (on_error_) {
    on_error_("connection error");
  }
}

void ClientApp::OnMalformedNotification() {
  if (on_error_) {
    on_error_("malformed notification");
  }
}

void ClientApp::Start() {
  std::cout << "--- Debug Cloud Sync ---" << std::endl;

  configuration::Configuration configuration;
  if (!configuration::ConfigurationEncoder::Decode(
          configuration::kDefaultConfigurationFile.ToString(),
          &configuration)) {
    error("ledger configuration",
          ftl::Concatenate({"not able to read file at ",
                            configuration::kDefaultConfigurationFile}));
    return;
  }
  ok("ledger configuration");

  if (!configuration.use_sync) {
    error("sync enabled", "sync is disabled");
    return;
  }

  ok("sync enabled",
     ftl::Concatenate({"syncing to ", configuration.sync_params.firebase_id,
                       " / ", configuration.sync_params.firebase_prefix}));

  network_service_ = std::make_unique<ledger::NetworkServiceImpl>([this] {
    return context_->ConnectToEnvironmentService<network::NetworkService>();
  });

  firebase_ = std::make_unique<firebase::FirebaseImpl>(
      network_service_.get(), configuration.sync_params.firebase_id,
      GetFirebasePrefix(configuration.sync_params.firebase_prefix,
                        RandomString()));
  cloud_provider_ = std::make_unique<CloudProviderImpl>(firebase_.get());

  CheckObjects();
}

void ClientApp::CheckObjects() {
  std::string id = RandomString();
  std::string content = RandomString();
  mx::vmo data;
  auto result = mtl::VmoFromString(content, &data);
  FTL_DCHECK(result);

  ftl::TimePoint request_start = ftl::TimePoint::Now();
  cloud_provider_->AddObject(
      id, std::move(data), [this, id, content, request_start](Status status) {

        if (status != Status::OK) {
          error("upload test object", status);
          return;
        }
        ftl::TimeDelta delta = ftl::TimePoint::Now() - request_start;
        ok("upload test object", delta);
        CheckGetObject(id, content);
      });
}

void ClientApp::CheckGetObject(std::string id, std::string content) {
  ftl::TimePoint request_start = ftl::TimePoint::Now();
  cloud_provider_->GetObject(
      id, [this, request_start](Status status, uint64_t size,
                                mx::datapipe_consumer data) {
        if (status != Status::OK) {
          error("retrieve test object", status);
          return;
        }

        ftl::TimeDelta delta = ftl::TimePoint::Now() - request_start;
        ok("retrieve test object", delta);
        CheckCommits();
      });
}

void ClientApp::CheckCommits() {
  Commit commit(RandomString(), RandomString(), {});
  ftl::TimePoint request_start = ftl::TimePoint::Now();
  cloud_provider_->AddCommit(
      commit,
      ftl::MakeCopyable(
          [ this, commit = commit.Clone(), request_start ](Status status) {
            if (status != Status::OK) {
              error("upload test commit", status);
              return;
            }
            ftl::TimeDelta delta = ftl::TimePoint::Now() - request_start;
            ok("upload test commit", delta);
            CheckGetCommits(commit.Clone());
          }));
}

void ClientApp::CheckGetCommits(Commit commit) {
  ftl::TimePoint request_start = ftl::TimePoint::Now();
  cloud_provider_->GetCommits(
      "", ftl::MakeCopyable([ this, commit = std::move(commit), request_start ](
              Status status, std::vector<Record> records) {
        if (status != Status::OK) {
          error("retrieve test commits", status);
          return;
        }

        ftl::TimeDelta delta = ftl::TimePoint::Now() - request_start;
        ok("retrieve test commits", delta);
        CheckWatchExistingCommits(commit.Clone());
      }));
}

void ClientApp::CheckWatchExistingCommits(Commit expected_commit) {
  on_remote_commit_ =
      ftl::MakeCopyable([ this, expected_commit = std::move(expected_commit) ](
          Commit commit, std::string timestamp) {
        on_error_ = nullptr;
        if (commit.id != expected_commit.id ||
            commit.content != expected_commit.content) {
          error("watch for existing commits", "received a wrong commit");
          on_remote_commit_ = nullptr;
          return;
        }
        on_remote_commit_ = nullptr;
        ok("watch for existing commits");
        CheckWatchNewCommits();
      });
  on_error_ = [this](ftl::StringView description) {
    on_remote_commit_ = nullptr;
    on_error_ = nullptr;
    error("watch for existing commits", description);
  };
  cloud_provider_->WatchCommits("", this);
}

void ClientApp::CheckWatchNewCommits() {
  Commit commit(RandomString(), RandomString(), {});
  on_remote_commit_ = ftl::MakeCopyable([
    this, expected_commit = commit.Clone(),
    request_start = ftl::TimePoint::Now()
  ](Commit commit, std::string timestamp) {
    on_error_ = nullptr;
    if (commit.id != expected_commit.id ||
        commit.content != expected_commit.content) {
      error("watch for new commits", "received a wrong commit");
      on_remote_commit_ = nullptr;
      return;
    }
    ftl::TimeDelta delta = ftl::TimePoint::Now() - request_start;
    on_remote_commit_ = nullptr;
    ok("watch for new commits", delta);
    Done();
  });
  on_error_ = [this](ftl::StringView description) {
    on_remote_commit_ = nullptr;
    on_error_ = nullptr;
    error("watch for new commits", description);
  };

  cloud_provider_->AddCommit(
      commit, ftl::MakeCopyable(
                  [ this, expected_commit = commit.Clone() ](Status status) {
                    if (status != Status::OK) {
                      error("watch for new commits", status);
                      return;
                    }
                  }));
}

void ClientApp::Done() {
  std::cout << "You're all set!" << std::endl;
  mtl::MessageLoop::GetCurrent()->PostQuitTask();
}

}  // namespace cloud_provider

int main(int argc, const char** argv) {
  srand(time(0));
  ftl::CommandLine command_line = ftl::CommandLineFromArgcArgv(argc, argv);
  mtl::MessageLoop loop;

  cloud_provider::ClientApp app(std::move(command_line));

  loop.Run();
  return 0;
}
