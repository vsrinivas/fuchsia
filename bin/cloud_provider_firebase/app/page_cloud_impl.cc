// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/cloud_provider_firebase/app/page_cloud_impl.h"

#include <lib/fsl/vmo/sized_vmo.h>
#include <lib/fxl/functional/make_copyable.h>

#include "peridot/bin/cloud_provider_firebase/app/convert_status.h"
#include "peridot/lib/convert/convert.h"

namespace cloud_provider_firebase {

namespace {

std::string ToTimestamp(const std::unique_ptr<cloud_provider::Token>& token) {
  if (!token) {
    return "";
  }
  return convert::ToString(token->opaque_id);
}

void ConvertRecords(const std::vector<Record>& records,
                    fidl::VectorPtr<cloud_provider::Commit>* out_commits,
                    std::unique_ptr<cloud_provider::Token>* out_token) {
  fidl::VectorPtr<cloud_provider::Commit> commits;
  for (auto& record : records) {
    cloud_provider::Commit commit;
    commit.id = convert::ToArray(record.commit.id);
    commit.data = convert::ToArray(record.commit.content);
    commits.push_back(std::move(commit));
  }
  std::unique_ptr<cloud_provider::Token> token;
  if (!records.empty()) {
    token = std::make_unique<cloud_provider::Token>();
    token->opaque_id = convert::ToArray(records.back().timestamp);
  }

  out_commits->swap(commits);
  out_token->swap(token);
}

}  // namespace

PageCloudImpl::PageCloudImpl(
    firebase_auth::FirebaseAuth* firebase_auth,
    std::unique_ptr<firebase::Firebase> firebase,
    std::unique_ptr<gcs::CloudStorage> cloud_storage,
    std::unique_ptr<PageCloudHandler> handler,
    fidl::InterfaceRequest<cloud_provider::PageCloud> request)
    : firebase_auth_(firebase_auth),
      firebase_(std::move(firebase)),
      cloud_storage_(std::move(cloud_storage)),
      handler_(std::move(handler)),
      binding_(this, std::move(request)) {
  FXL_DCHECK(firebase_auth_);
  // The class shuts down when the client connection is disconnected.
  binding_.set_error_handler([this] {
    if (on_empty_) {
      on_empty_();
    }
  });
}

PageCloudImpl::~PageCloudImpl() {
  if (handler_watcher_set_) {
    Unregister();
  }
}

void PageCloudImpl::OnRemoteCommits(std::vector<Record> records) {
  FXL_DCHECK(watcher_);
  std::move(records.begin(), records.end(), std::back_inserter(records_));
  if (!waiting_for_remote_commits_ack_) {
    SendRemoteCommits();
  }
}

void PageCloudImpl::OnConnectionError() {
  FXL_DCHECK(watcher_);
  watcher_->OnError(cloud_provider::Status::NETWORK_ERROR);
  Unregister();
}

void PageCloudImpl::OnTokenExpired() {
  FXL_DCHECK(watcher_);
  watcher_->OnError(cloud_provider::Status::AUTH_ERROR);
  Unregister();
}

void PageCloudImpl::OnMalformedNotification() {
  FXL_DCHECK(watcher_);
  watcher_->OnError(cloud_provider::Status::PARSE_ERROR);
  Unregister();
}

void PageCloudImpl::SendRemoteCommits() {
  if (records_.empty()) {
    return;
  }

  fidl::VectorPtr<cloud_provider::Commit> commits;
  std::unique_ptr<cloud_provider::Token> position_token;
  ConvertRecords(records_, &commits, &position_token);
  waiting_for_remote_commits_ack_ = true;
  watcher_->OnNewCommits(std::move(commits), std::move(position_token), [this] {
    waiting_for_remote_commits_ack_ = false;
    SendRemoteCommits();
  });
  records_.clear();
}

void PageCloudImpl::AddCommits(fidl::VectorPtr<cloud_provider::Commit> commits,
                               AddCommitsCallback callback) {
  auto request = firebase_auth_->GetFirebaseToken(fxl::MakeCopyable(
      [this, commits = std::move(commits), callback = std::move(callback)](
          firebase_auth::AuthStatus auth_status,
          std::string auth_token) mutable {
        if (auth_status != firebase_auth::AuthStatus::OK) {
          callback(cloud_provider::Status::AUTH_ERROR);
          return;
        }

        std::vector<Commit> handler_commits;
        for (auto& commit : *commits) {
          handler_commits.emplace_back(convert::ToString(commit.id),
                                       convert::ToString(commit.data));
        }

        handler_->AddCommits(std::move(auth_token), std::move(handler_commits),
                             [callback = std::move(callback)](Status status) {
                               callback(ConvertInternalStatus(status));
                             });
      }));
  auth_token_requests_.emplace(request);
}

void PageCloudImpl::GetCommits(
    std::unique_ptr<cloud_provider::Token> min_position_token,
    GetCommitsCallback callback) {
  auto request = firebase_auth_->GetFirebaseToken(fxl::MakeCopyable(
      [this, min_timestamp = ToTimestamp(min_position_token),
       callback = std::move(callback)](firebase_auth::AuthStatus auth_status,
                                       std::string auth_token) mutable {
        if (auth_status != firebase_auth::AuthStatus::OK) {
          callback(cloud_provider::Status::AUTH_ERROR,
                   fidl::VectorPtr<cloud_provider::Commit>::New(0), nullptr);
          return;
        }

        handler_->GetCommits(
            std::move(auth_token), min_timestamp,
            [callback = std::move(callback)](Status status,
                                             std::vector<Record> records) {
              if (status != Status::OK) {
                callback(ConvertInternalStatus(status),
                         fidl::VectorPtr<cloud_provider::Commit>::New(0),
                         nullptr);
                return;
              }

              fidl::VectorPtr<cloud_provider::Commit> commits =
                  fidl::VectorPtr<cloud_provider::Commit>::New(0);
              if (records.empty()) {
                callback(ConvertInternalStatus(status), std::move(commits),
                         nullptr);
                return;
              }

              std::unique_ptr<cloud_provider::Token> position_token;
              ConvertRecords(records, &commits, &position_token);
              callback(ConvertInternalStatus(status), std::move(commits),
                       std::move(position_token));
            });
      }));
  auth_token_requests_.emplace(request);
}

void PageCloudImpl::AddObject(fidl::VectorPtr<uint8_t> id,
                              fuchsia::mem::Buffer data,
                              AddObjectCallback callback) {
  fsl::SizedVmo vmo;
  if (!fsl::SizedVmo::FromTransport(std::move(data), &vmo)) {
    callback(cloud_provider::Status::ARGUMENT_ERROR);
    return;
  }
  auto request = firebase_auth_->GetFirebaseToken(fxl::MakeCopyable(
      [this, id = convert::ToString(id), vmo = std::move(vmo),
       callback = std::move(callback)](firebase_auth::AuthStatus auth_status,
                                       std::string auth_token) mutable {
        if (auth_status != firebase_auth::AuthStatus::OK) {
          callback(cloud_provider::Status::AUTH_ERROR);
          return;
        }

        handler_->AddObject(std::move(auth_token), std::move(id),
                            std::move(vmo),
                            [callback = std::move(callback)](Status status) {
                              callback(ConvertInternalStatus(status));
                            });
      }));
  auth_token_requests_.emplace(request);
}

void PageCloudImpl::GetObject(fidl::VectorPtr<uint8_t> id,
                              GetObjectCallback callback) {
  auto request = firebase_auth_->GetFirebaseToken(
      [this, id = convert::ToString(id), callback = std::move(callback)](
          firebase_auth::AuthStatus auth_status,
          std::string auth_token) mutable {
        if (auth_status != firebase_auth::AuthStatus::OK) {
          callback(cloud_provider::Status::AUTH_ERROR, 0u, zx::socket());
          return;
        }

        handler_->GetObject(std::move(auth_token), std::move(id),
                            [callback = std::move(callback)](
                                Status status, uint64_t size, zx::socket data) {
                              callback(ConvertInternalStatus(status), size,
                                       std::move(data));
                            });
      });
  auth_token_requests_.emplace(request);
}

void PageCloudImpl::SetWatcher(
    std::unique_ptr<cloud_provider::Token> min_position_token,
    fidl::InterfaceHandle<cloud_provider::PageCloudWatcher> watcher,
    SetWatcherCallback callback) {
  watcher_ = watcher.Bind();
  watcher_.set_error_handler([this] {
    if (handler_watcher_set_) {
      Unregister();
    }
    waiting_for_remote_commits_ack_ = false;
  });
  auto request = firebase_auth_->GetFirebaseToken(
      [this, min_timestamp = ToTimestamp(min_position_token),
       callback = std::move(callback)](firebase_auth::AuthStatus auth_status,
                                       std::string auth_token) mutable {
        if (auth_status != firebase_auth::AuthStatus::OK) {
          watcher_->OnError(cloud_provider::Status::AUTH_ERROR);
          callback(cloud_provider::Status::AUTH_ERROR);
          return;
        }

        handler_->WatchCommits(std::move(auth_token), std::move(min_timestamp),
                               this);
        handler_watcher_set_ = true;
        callback(cloud_provider::Status::OK);
      });
  auth_token_requests_.emplace(request);
}

void PageCloudImpl::Unregister() {
  FXL_DCHECK(handler_watcher_set_);
  handler_->UnwatchCommits(this);
  handler_watcher_set_ = false;
}

}  // namespace cloud_provider_firebase
