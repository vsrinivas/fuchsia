// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/cloud_provider/client/doctor_command.h"

#include <iostream>

#include "apps/ledger/src/glue/crypto/rand.h"
#include "lib/ftl/functional/make_copyable.h"
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

}  // namespace

DoctorCommand::DoctorCommand(CloudProvider* cloud_provider)
    : cloud_provider_(cloud_provider) {
  FTL_DCHECK(cloud_provider);
}

DoctorCommand::~DoctorCommand() {}

void DoctorCommand::Start(ftl::Closure on_done) {
  std::cout << "Sync Checkup" << std::endl;
  on_done_ = std::move(on_done);
  CheckObjects();
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

void DoctorCommand::CheckObjects() {
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
          on_done_();
          return;
        }
        ftl::TimeDelta delta = ftl::TimePoint::Now() - request_start;
        ok("upload test object", delta);
        CheckGetObject(id, content);
      });
}

void DoctorCommand::CheckGetObject(std::string id, std::string content) {
  ftl::TimePoint request_start = ftl::TimePoint::Now();
  cloud_provider_->GetObject(
      id, [this, request_start](Status status, uint64_t size,
                                mx::datapipe_consumer data) {
        if (status != Status::OK) {
          error("retrieve test object", status);
          on_done_();
          return;
        }

        ftl::TimeDelta delta = ftl::TimePoint::Now() - request_start;
        ok("retrieve test object", delta);
        CheckCommits();
      });
}

void DoctorCommand::CheckCommits() {
  Commit commit(RandomString(), RandomString(), {});
  ftl::TimePoint request_start = ftl::TimePoint::Now();
  cloud_provider_->AddCommit(
      commit,
      ftl::MakeCopyable(
          [ this, commit = commit.Clone(), request_start ](Status status) {
            if (status != Status::OK) {
              error("upload test commit", status);
              on_done_();
              return;
            }
            ftl::TimeDelta delta = ftl::TimePoint::Now() - request_start;
            ok("upload test commit", delta);
            CheckGetCommits(commit.Clone());
          }));
}

void DoctorCommand::CheckGetCommits(Commit commit) {
  ftl::TimePoint request_start = ftl::TimePoint::Now();
  cloud_provider_->GetCommits(
      "", ftl::MakeCopyable([ this, commit = std::move(commit), request_start ](
              Status status, std::vector<Record> records) {
        if (status != Status::OK) {
          error("retrieve test commits", status);
          on_done_();
          return;
        }

        ftl::TimeDelta delta = ftl::TimePoint::Now() - request_start;
        ok("retrieve test commits", delta);
        CheckWatchExistingCommits(commit.Clone());
      }));
}

void DoctorCommand::CheckWatchExistingCommits(Commit expected_commit) {
  on_remote_commit_ =
      ftl::MakeCopyable([ this, expected_commit = std::move(expected_commit) ](
          Commit commit, std::string timestamp) {
        on_error_ = nullptr;
        if (commit.id != expected_commit.id ||
            commit.content != expected_commit.content) {
          error("watch for existing commits", "received a wrong commit");
          on_done_();
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
    on_done_();
  };
  cloud_provider_->WatchCommits("", this);
}

void DoctorCommand::CheckWatchNewCommits() {
  Commit commit(RandomString(), RandomString(), {});
  on_remote_commit_ = ftl::MakeCopyable([
    this, expected_commit = commit.Clone(),
    request_start = ftl::TimePoint::Now()
  ](Commit commit, std::string timestamp) {
    on_error_ = nullptr;
    if (commit.id != expected_commit.id ||
        commit.content != expected_commit.content) {
      error("watch for new commits", "received a wrong commit");
      on_done_();
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
    on_done_();
  };

  cloud_provider_->AddCommit(
      commit, ftl::MakeCopyable(
                  [ this, expected_commit = commit.Clone() ](Status status) {
                    if (status != Status::OK) {
                      error("watch for new commits", status);
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
