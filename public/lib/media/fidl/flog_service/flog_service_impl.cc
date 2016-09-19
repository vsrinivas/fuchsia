// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo/public/cpp/application/connect.h"
#include "mojo/public/cpp/application/service_provider_impl.h"
#include "services/flog/flog_directory.h"
#include "services/flog/flog_logger_impl.h"
#include "services/flog/flog_reader_impl.h"
#include "services/flog/flog_service_impl.h"

namespace mojo {
namespace flog {

FlogServiceImpl::FlogServiceImpl() {}

FlogServiceImpl::~FlogServiceImpl() {}

void FlogServiceImpl::OnInitialize() {
  directory_ = std::shared_ptr<FlogDirectory>(new FlogDirectory(shell()));
  directory_->GetExistingFiles([this](
      std::unique_ptr<std::map<uint32_t, std::string>> labels_by_id) {
    log_labels_by_id_ = std::move(labels_by_id);
    DCHECK(log_labels_by_id_);
    for (const std::pair<uint32_t, std::string>& pair : *log_labels_by_id_) {
      if (pair.first > last_allocated_log_id_) {
        last_allocated_log_id_ = pair.first;
      }
    }

    ready_.Occur();
  });
}

bool FlogServiceImpl::OnAcceptConnection(
    ServiceProviderImpl* service_provider_impl) {
  service_provider_impl->AddService<FlogService>(
      [this](const ConnectionContext& connection_context,
             InterfaceRequest<FlogService> flog_service_request) {
        bindings_.AddBinding(this, flog_service_request.Pass());
      });
  return true;
}

void FlogServiceImpl::CreateLogger(InterfaceRequest<FlogLogger> logger,
                                   const String& label) {
  // TODO(dalesat): Get rid of this capture hack once we have c++14.
  MessagePipeHandle handle = logger.PassMessagePipe().release();
  ready_.When([this, handle, label]() {
    InterfaceRequest<FlogLogger> logger =
        InterfaceRequest<FlogLogger>(ScopedMessagePipeHandle(handle));
    AddProduct(FlogLoggerImpl::Create(logger.Pass(), ++last_allocated_log_id_,
                                      label, directory_, this));
  });
}

void FlogServiceImpl::GetLogDescriptions(
    const GetLogDescriptionsCallback& callback) {
  ready_.When([this, callback]() {
    DCHECK(log_labels_by_id_);
    // TODO(dalesat): Include open and new logs.
    Array<FlogDescriptionPtr> descriptions =
        Array<FlogDescriptionPtr>::New(log_labels_by_id_->size());

    size_t i = 0;
    for (std::pair<uint32_t, std::string> pair : *log_labels_by_id_) {
      FlogDescriptionPtr description = FlogDescription::New();
      description->log_id = pair.first;
      description->label = pair.second;
      description->open = false;
      descriptions[i++] = description.Pass();
    }

    callback.Run(descriptions.Pass());
  });
}

void FlogServiceImpl::CreateReader(InterfaceRequest<FlogReader> reader,
                                   uint32_t log_id) {
  // TODO(dalesat): Get rid of this capture hack once we have c++14.
  MessagePipeHandle handle = reader.PassMessagePipe().release();
  ready_.When([this, handle, log_id]() {
    DCHECK(log_labels_by_id_);
    auto iter = log_labels_by_id_->find(log_id);
    AddProduct(FlogReaderImpl::Create(
        InterfaceRequest<FlogReader>(ScopedMessagePipeHandle(handle)), log_id,
        iter == log_labels_by_id_->end() ? nullptr : iter->second, directory_,
        this));
  });
}

void FlogServiceImpl::DeleteLog(uint32_t log_id) {
  ready_.When([this, log_id]() {
    DCHECK(log_labels_by_id_);
    auto iter = log_labels_by_id_->find(log_id);
    if (iter == log_labels_by_id_->end()) {
      return;
    }

    directory_->DeleteFile(log_id, iter->second);
    log_labels_by_id_->erase(iter);
  });
}

void FlogServiceImpl::DeleteAllLogs() {
  ready_.When([this]() {
    DCHECK(log_labels_by_id_);

    for (const std::pair<uint32_t, std::string>& pair : *log_labels_by_id_) {
      directory_->DeleteFile(pair.first, pair.second);
    }

    log_labels_by_id_->clear();
  });
}

}  // namespace flog
}  // namespace mojo
