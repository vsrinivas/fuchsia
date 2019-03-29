// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/page_manager_container.h"

#include <string>

#include <lib/fidl/cpp/interface_request.h>
#include <lib/fit/function.h>
#include <src/lib/fxl/logging.h>

#include "src/ledger/bin/app/page_manager.h"
#include "src/ledger/bin/app/page_usage_listener.h"
#include "src/ledger/bin/app/types.h"
#include "src/ledger/bin/storage/public/types.h"

namespace ledger {

PageManagerContainer::PageManagerContainer(
    std::string ledger_name, storage::PageId page_id,
    PageUsageListener* page_usage_listener)
    : page_id_(page_id),
      connection_notifier_(std::move(ledger_name), std::move(page_id),
                           page_usage_listener) {}

PageManagerContainer::~PageManagerContainer() = default;

void PageManagerContainer::set_on_empty(fit::closure on_empty_callback) {
  on_empty_callback_ = std::move(on_empty_callback);
  connection_notifier_.set_on_empty([this] { CheckEmpty(); });
  if (page_manager_) {
    page_manager_->set_on_empty(
        [this] { connection_notifier_.UnregisterExternalRequests(); });
  }
}

void PageManagerContainer::BindPage(
    fidl::InterfaceRequest<Page> page_request,
    fit::function<void(storage::Status)> callback) {
  connection_notifier_.RegisterExternalRequest();

  if (status_ != storage::Status::OK) {
    callback(status_);
    return;
  }
  auto page_impl =
      std::make_unique<PageImpl>(page_id_, std::move(page_request));
  if (page_manager_) {
    page_manager_->AddPageImpl(std::move(page_impl), std::move(callback));
    return;
  }
  page_impls_.emplace_back(std::move(page_impl), std::move(callback));
}

void PageManagerContainer::NewInternalRequest(
    fit::function<void(storage::Status, ExpiringToken, PageManager*)>
        callback) {
  if (status_ != storage::Status::OK) {
    callback(status_, fit::defer<fit::closure>([] {}), nullptr);
    return;
  }

  if (page_manager_) {
    callback(status_, connection_notifier_.NewInternalRequestToken(),
             page_manager_.get());
    return;
  }

  internal_request_callbacks_.push_back(std::move(callback));
}

void PageManagerContainer::SetPageManager(
    storage::Status status, std::unique_ptr<PageManager> page_manager) {
  auto token = connection_notifier_.NewInternalRequestToken();
  TRACE_DURATION("ledger", "page_manager_container_set_page_manager");

  FXL_DCHECK(!page_manager_is_set_);
  FXL_DCHECK((status != storage::Status::OK) == !page_manager);
  status_ = status;
  page_manager_ = std::move(page_manager);
  page_manager_is_set_ = true;

  for (auto& [page_impl, callback] : page_impls_) {
    if (page_manager_) {
      page_manager_->AddPageImpl(std::move(page_impl), std::move(callback));
    } else {
      callback(status_);
    }
  }
  page_impls_.clear();

  for (auto& callback : internal_request_callbacks_) {
    if (!page_manager_) {
      callback(status_, fit::defer<fit::closure>([] {}), nullptr);
      continue;
    }
    callback(status_, connection_notifier_.NewInternalRequestToken(),
             page_manager_.get());
  }
  internal_request_callbacks_.clear();

  if (page_manager_) {
    page_manager_->set_on_empty(
        [this] { connection_notifier_.UnregisterExternalRequests(); });
  }
  // |CheckEmpty| called when |token| goes out of scope.
}

bool PageManagerContainer::PageConnectionIsOpen() {
  return (page_manager_ && !page_manager_->IsEmpty()) || !page_impls_.empty();
}

void PageManagerContainer::CheckEmpty() {
  // The PageManagerContainer is not considered empty until |SetPageManager| has
  // been called.
  if (on_empty_callback_ && connection_notifier_.IsEmpty() &&
      page_manager_is_set_ && (!page_manager_ || page_manager_->IsEmpty())) {
    on_empty_callback_();
  }
}

}  // namespace ledger
