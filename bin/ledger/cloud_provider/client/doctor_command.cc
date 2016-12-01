// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/cloud_provider/client/doctor_command.h"

#include <iostream>

#include "apps/ledger/src/glue/crypto/rand.h"
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
  return std::to_string(glue::RandUint64());
}

void what(ftl::StringView what) {
  std::cout << " > " << what << std::endl;
}

void ok(ftl::StringView message = "") {
  std::cout << "   [OK] ";
  if (message.size()) {
    std::cout << message;
  }
  std::cout << std::endl;
}

void ok(ftl::TimeDelta request_time) {
  std::cout << "   [OK] request time " << request_time.ToMilliseconds() << " ms"
            << std::endl;
}

void error(ftl::StringView message) {
  std::cout << "   [FAILED] ";
  if (message.size()) {
    std::cout << " " << message;
  }
  std::cout << std::endl;
}

void error(Status status) {
  std::cout << "   [FAILED] with cloud provider status " << status << std::endl;
}

void hint(ftl::StringView hint) {
  std::cout << "   hint: " << hint << std::endl;
  std::cout
      << "   see also the User Guide at "
      << "https://fuchsia.googlesource.com/ledger/+/HEAD/docs/user_guide.md"
      << std::endl;
}

std::string FirebaseUrlFromId(const std::string& firebase_id) {
  return ftl::Concatenate({"https://", firebase_id, ".firebaseio.com/.json"});
}

}  // namespace

DoctorCommand::DoctorCommand(ledger::NetworkService* network_service,
                             const std::string& firebase_id,
                             CloudProvider* cloud_provider)
    : network_service_(network_service),
      firebase_id_(firebase_id),
      cloud_provider_(cloud_provider) {
  FTL_DCHECK(network_service_);
  FTL_DCHECK(!firebase_id_.empty());
  FTL_DCHECK(cloud_provider_);
}

DoctorCommand::~DoctorCommand() {}

void DoctorCommand::Start(ftl::Closure on_done) {
  std::cout << "Sync Checkup" << std::endl;
  on_done_ = std::move(on_done);
  CheckHttpConnectivity();
}

void DoctorCommand::OnRemoteCommit(Commit commit, std::string timestamp) {
  if (on_remote_commit_) {
    on_remote_commit_(std::move(commit), std::move(timestamp));
  }
}

void DoctorCommand::OnConnectionError() {
  if (on_error_) {
    on_error_("connection error");
  }
}

void DoctorCommand::OnMalformedNotification() {
  if (on_error_) {
    on_error_("malformed notification");
  }
}

void DoctorCommand::CheckHttpConnectivity() {
  what("http - fetch http://example.com");

  auto request = network_service_->Request(
      [] {
        auto url_request = network::URLRequest::New();
        url_request->url = "http://example.com";
        return url_request;
      },
      [ this, request_start =
                  ftl::TimePoint::Now() ](network::URLResponsePtr response) {
        if (response->status_code != 200 || response->error) {
          error("network error " + response->error->description.get() +
                ", status code " + std::to_string(response->status_code));
          hint(
              "It looks like your Fuchsia doesn't have connectivity to the "
              "internets outside. Make sure to follow the instructions in "
              "https://fuchsia.googlesource.com/netstack/+/master/README.md");
          on_done_();
          return;
        }

        ftl::TimeDelta delta = ftl::TimePoint::Now() - request_start;
        ok(delta);
        CheckHttpsConnectivity();
      });
}

void DoctorCommand::CheckHttpsConnectivity() {
  what("https - fetch https://example.com");

  auto request = network_service_->Request(
      [] {
        auto url_request = network::URLRequest::New();
        url_request->url = "https://example.com";
        return url_request;
      },
      [ this, request_start =
                  ftl::TimePoint::Now() ](network::URLResponsePtr response) {
        if (response->status_code != 200 || response->error) {
          error("network error " + response->error->description.get() +
                ", status code " + std::to_string(response->status_code));
          hint(
              "It looks like the http*s* request failed even though http seems "
              "to work. Please file a Userspace bug for the network stack.");
          on_done_();
          return;
        }

        ftl::TimeDelta delta = ftl::TimePoint::Now() - request_start;
        ok(delta);
        CheckObjects();
      });
}

void DoctorCommand::CheckObjects() {
  what("Firebase - upload test object");
  std::string id = RandomString();
  std::string content = RandomString();
  mx::vmo data;
  auto result = mtl::VmoFromString(content, &data);
  FTL_DCHECK(result);

  ftl::TimePoint request_start = ftl::TimePoint::Now();
  cloud_provider_->AddObject(
      id, std::move(data), [this, id, content, request_start](Status status) {

        if (status != Status::OK) {
          error(status);
          hint(ftl::Concatenate(
              {"It seems that we can't access the Firebase instance. "
               "Please verify that you can access ",
               FirebaseUrlFromId(firebase_id_),
               " on your host machine. If not, refer to the User Guide for the "
               "recommended Firebase configuration."}));
          on_done_();
          return;
        }
        ftl::TimeDelta delta = ftl::TimePoint::Now() - request_start;
        ok(delta);
        CheckGetObject(id, content);
      });
}

void DoctorCommand::CheckGetObject(std::string id, std::string content) {
  what("Firebase - retrieve test object");
  ftl::TimePoint request_start = ftl::TimePoint::Now();
  cloud_provider_->GetObject(
      id, [this, request_start](Status status, uint64_t size,
                                mx::datapipe_consumer data) {
        if (status != Status::OK) {
          error(status);
          on_done_();
          return;
        }

        ftl::TimeDelta delta = ftl::TimePoint::Now() - request_start;
        ok(delta);
        CheckCommits();
      });
}

void DoctorCommand::CheckCommits() {
  what("Firebase - upload test commit");
  Commit commit(RandomString(), RandomString(), {});
  ftl::TimePoint request_start = ftl::TimePoint::Now();
  cloud_provider_->AddCommit(
      commit,
      ftl::MakeCopyable(
          [ this, commit = commit.Clone(), request_start ](Status status) {
            if (status != Status::OK) {
              error(status);
              on_done_();
              return;
            }
            ftl::TimeDelta delta = ftl::TimePoint::Now() - request_start;
            ok(delta);
            CheckGetCommits(commit.Clone());
          }));
}

void DoctorCommand::CheckGetCommits(Commit commit) {
  what("Firebase - retrieve test commits");
  ftl::TimePoint request_start = ftl::TimePoint::Now();
  cloud_provider_->GetCommits(
      "", ftl::MakeCopyable([ this, commit = std::move(commit), request_start ](
              Status status, std::vector<Record> records) {
        if (status != Status::OK) {
          error(status);
          hint(
              "It seems that we can't query Firebase for commits. "
              "This might indicate that database indices are not configured "
              "or their configuration is out of date. "
              "Please refer to the User Guide for the recommended Firebase "
              "configuration.");
          on_done_();
          return;
        }

        ftl::TimeDelta delta = ftl::TimePoint::Now() - request_start;
        ok(delta);
        CheckWatchExistingCommits(commit.Clone());
      }));
}

void DoctorCommand::CheckWatchExistingCommits(Commit expected_commit) {
  what("Firebase - watch for existing commits");
  on_remote_commit_ =
      ftl::MakeCopyable([ this, expected_commit = std::move(expected_commit) ](
          Commit commit, std::string timestamp) {
        on_error_ = nullptr;
        if (commit.id != expected_commit.id ||
            commit.content != expected_commit.content) {
          error("received a wrong commit");
          on_done_();
          on_remote_commit_ = nullptr;
          return;
        }
        on_remote_commit_ = nullptr;
        ok();
        CheckWatchNewCommits();
      });
  on_error_ = [this](ftl::StringView description) {
    on_remote_commit_ = nullptr;
    on_error_ = nullptr;
    error(description);
    on_done_();
  };
  cloud_provider_->WatchCommits("", this);
}

void DoctorCommand::CheckWatchNewCommits() {
  what("Firebase - watch for new commits");
  Commit commit(RandomString(), RandomString(), {});
  on_remote_commit_ = ftl::MakeCopyable([
    this, expected_commit = commit.Clone(),
    request_start = ftl::TimePoint::Now()
  ](Commit commit, std::string timestamp) {
    on_error_ = nullptr;
    if (commit.id != expected_commit.id ||
        commit.content != expected_commit.content) {
      error("received a wrong commit");
      on_done_();
      on_remote_commit_ = nullptr;
      return;
    }
    ftl::TimeDelta delta = ftl::TimePoint::Now() - request_start;
    on_remote_commit_ = nullptr;
    ok(delta);
    Done();
  });
  on_error_ = [this](ftl::StringView description) {
    on_remote_commit_ = nullptr;
    on_error_ = nullptr;
    error(description);
    on_done_();
  };

  cloud_provider_->AddCommit(
      commit, ftl::MakeCopyable(
                  [ this, expected_commit = commit.Clone() ](Status status) {
                    if (status != Status::OK) {
                      error(status);
                      on_done_();
                      return;
                    }
                  }));
}

void DoctorCommand::Done() {
  std::cout << "You're all set!" << std::endl;
  on_done_();
}

}  // namespace cloud_provider
