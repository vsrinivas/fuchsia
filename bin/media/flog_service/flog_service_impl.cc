// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/src/flog_service/flog_service_impl.h"

#include "apps/media/src/flog_service/flog_directory.h"
#include "apps/media/src/flog_service/flog_logger_impl.h"
#include "apps/media/src/flog_service/flog_reader_impl.h"
#include "apps/modular/lib/app/connect.h"
#include "apps/modular/lib/app/service_provider_impl.h"
#include "lib/ftl/functional/make_copyable.h"

namespace flog {

FlogServiceImpl::FlogServiceImpl() {
  application_context()->outgoing_services()->AddService<FlogService>(
      [this](fidl::InterfaceRequest<FlogService> request) {
        bindings_.AddBinding(this, std::move(request));
      });

  directory_ = std::shared_ptr<FlogDirectory>(new FlogDirectory());
  directory_->GetExistingFiles([this](
      std::unique_ptr<std::map<uint32_t, std::string>> labels_by_id) {
    log_labels_by_id_ = std::move(labels_by_id);
    FTL_DCHECK(log_labels_by_id_);
    for (const std::pair<uint32_t, std::string>& pair : *log_labels_by_id_) {
      if (pair.first > last_allocated_log_id_) {
        last_allocated_log_id_ = pair.first;
      }
    }

    ready_.Occur();
  });
}

FlogServiceImpl::~FlogServiceImpl() {}

void FlogServiceImpl::CreateLogger(fidl::InterfaceRequest<FlogLogger> request,
                                   const fidl::String& label) {
  ready_.When(ftl::MakeCopyable([
    this, request = std::move(request), label
  ]() mutable {
    std::shared_ptr<FlogLoggerImpl> logger = FlogLoggerImpl::Create(
        std::move(request), ++last_allocated_log_id_, label, directory_, this);
    AddProduct(logger);
    log_labels_by_id_->insert(std::make_pair(logger->id(), logger->label()));
  }));
}

void FlogServiceImpl::GetLogDescriptions(
    const GetLogDescriptionsCallback& callback) {
  ready_.When([this, callback]() {
    FTL_DCHECK(log_labels_by_id_);
    fidl::Array<FlogDescriptionPtr> descriptions =
        fidl::Array<FlogDescriptionPtr>::New(log_labels_by_id_->size());

    size_t i = 0;
    for (std::pair<uint32_t, std::string> pair : *log_labels_by_id_) {
      FlogDescriptionPtr description = FlogDescription::New();
      description->log_id = pair.first;
      description->label = pair.second;
      description->open = false;
      descriptions[i++] = std::move(description);
    }

    callback(std::move(descriptions));
  });
}

void FlogServiceImpl::CreateReader(fidl::InterfaceRequest<FlogReader> reader,
                                   uint32_t log_id) {
  ready_.When(
      ftl::MakeCopyable([ this, reader = std::move(reader), log_id ]() mutable {
        FTL_DCHECK(log_labels_by_id_);
        auto iter = log_labels_by_id_->find(log_id);
        AddProduct(FlogReaderImpl::Create(
            std::move(reader), log_id,
            iter == log_labels_by_id_->end() ? nullptr : iter->second,
            directory_, this));
      }));
}

void FlogServiceImpl::DeleteLog(uint32_t log_id) {
  ready_.When([this, log_id]() {
    FTL_DCHECK(log_labels_by_id_);
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
    FTL_DCHECK(log_labels_by_id_);

    for (const std::pair<uint32_t, std::string>& pair : *log_labels_by_id_) {
      directory_->DeleteFile(pair.first, pair.second);
    }

    log_labels_by_id_->clear();
  });
}

}  // namespace flog
