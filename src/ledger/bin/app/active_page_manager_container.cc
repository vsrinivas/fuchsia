// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/active_page_manager_container.h"

#include <lib/fidl/cpp/interface_request.h>
#include <lib/fit/function.h>

#include <string>
#include <utility>
#include <vector>

#include "src/ledger/bin/app/active_page_manager.h"
#include "src/ledger/bin/app/page_usage_listener.h"
#include "src/ledger/bin/app/types.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/lib/logging/logging.h"

namespace ledger {

ActivePageManagerContainer::ActivePageManagerContainer(
    Environment* environment, std::string ledger_name, storage::PageId page_id,
    std::vector<PageUsageListener*> page_usage_listeners)
    : environment_(environment),
      ledger_name_(std::move(ledger_name)),
      page_id_(std::move(page_id)),
      page_usage_listeners_(std::move(page_usage_listeners)),
      weak_factory_(this) {
  token_manager_.SetOnDiscardable([this] { OnInternallyUnused(); });
}

ActivePageManagerContainer::~ActivePageManagerContainer() = default;

void ActivePageManagerContainer::SetOnDiscardable(fit::closure on_discardable) {
  on_discardable_ = std::move(on_discardable);
}

void ActivePageManagerContainer::BindPage(fidl::InterfaceRequest<Page> page_request,
                                          fit::function<void(Status)> callback) {
  if (!has_external_requests_) {
    has_external_requests_ = true;
    for (const auto& page_usage_listener : page_usage_listeners_) {
      page_usage_listener->OnExternallyUsed(ledger_name_, page_id_);
    }
  }

  if (status_ != Status::OK) {
    callback(status_);
    return;
  }
  auto page_impl =
      std::make_unique<PageImpl>(environment_->dispatcher(), page_id_, std::move(page_request));
  if (active_page_manager_) {
    active_page_manager_->AddPageImpl(std::move(page_impl), std::move(callback));
    return;
  }
  page_impls_.emplace_back(std::move(page_impl), std::move(callback));
}

void ActivePageManagerContainer::NewInternalRequest(
    fit::function<void(Status, ExpiringToken, ActivePageManager*)> callback) {
  if (status_ != Status::OK) {
    callback(status_, ExpiringToken(), nullptr);
    return;
  }

  if (active_page_manager_) {
    if (token_manager_.IsDiscardable()) {
      for (PageUsageListener* page_usage_listener : page_usage_listeners_) {
        page_usage_listener->OnInternallyUsed(ledger_name_, page_id_);
      }
    }
    callback(status_, token_manager_.CreateToken(), active_page_manager_.get());
    return;
  }

  internal_request_callbacks_.push_back(std::move(callback));
}

void ActivePageManagerContainer::SetActivePageManager(
    Status status, std::unique_ptr<ActivePageManager> active_page_manager) {
  TRACE_DURATION("ledger", "active_page_manager_container_set_active_page_manager");
  LEDGER_DCHECK(!active_page_manager_is_set_);
  LEDGER_DCHECK((status != Status::OK) == !active_page_manager);
  LEDGER_DCHECK(token_manager_.IsDiscardable());

  for (auto& [page_impl, callback] : page_impls_) {
    if (active_page_manager) {
      active_page_manager->AddPageImpl(std::move(page_impl), std::move(callback));
    } else {
      callback(status);
    }
  }
  page_impls_.clear();

  if (!internal_request_callbacks_.empty()) {
    if (!active_page_manager) {
      for (auto& internal_request_callback : internal_request_callbacks_) {
        internal_request_callback(status, ExpiringToken(), nullptr);
      }
    } else {
      // Create a token before calling the callbacks to ensure this class is not
      // discardable until this is done.
      auto token = token_manager_.CreateToken();
      for (PageUsageListener* page_usage_listener : page_usage_listeners_) {
        page_usage_listener->OnInternallyUsed(ledger_name_, page_id_);
      }
      for (auto& internal_request_callback : internal_request_callbacks_) {
        internal_request_callback(status, token_manager_.CreateToken(), active_page_manager.get());
      }
    }
    internal_request_callbacks_.clear();
  }

  // Only after assigning these fields is this |ActivePageManagerContainer| able to become empty.
  // TODO(https://bugs.fuchsia.dev/p/fuchsia/issues/detail?id=35152): Make these fields unable to
  // represent illegal state.
  status_ = status;
  active_page_manager_ = std::move(active_page_manager);
  active_page_manager_is_set_ = true;

  if (active_page_manager_) {
    active_page_manager_->SetOnDiscardable([this] { OnExternallyUnused(); });
    CheckDiscardable();
  } else {
    OnExternallyUnused();
  }
}

bool ActivePageManagerContainer::PageConnectionIsOpen() {
  return (active_page_manager_ && !active_page_manager_->IsDiscardable()) || !page_impls_.empty();
}

void ActivePageManagerContainer::OnExternallyUnused() {
  if (has_external_requests_) {
    auto weak_this = weak_factory_.GetWeakPtr();
    std::string ledger_name = ledger_name_;
    storage::PageId page_id = page_id_;
    std::vector<PageUsageListener*> page_usage_listeners = page_usage_listeners_;
    // This might delete the ActivePageManagerContainer object.
    for (PageUsageListener* page_usage_listener : page_usage_listeners) {
      page_usage_listener->OnExternallyUnused(ledger_name, page_id);
    }
    if (!weak_this) {
      return;
    }
    has_external_requests_ = false;
  }
  CheckDiscardable();
}

void ActivePageManagerContainer::OnInternallyUnused() {
  auto weak_this = weak_factory_.GetWeakPtr();
  std::string ledger_name = ledger_name_;
  storage::PageId page_id = page_id_;
  std::vector<PageUsageListener*> page_usage_listeners = page_usage_listeners_;
  // This might delete the ActivePageManagerContainer object.
  for (PageUsageListener* page_usage_listener : page_usage_listeners) {
    page_usage_listener->OnInternallyUnused(ledger_name, page_id);
  }
  if (weak_this) {
    CheckDiscardable();
  }
}

bool ActivePageManagerContainer::IsDiscardable() const {
  // The ActivePageManagerContainer is not considered empty until
  // |SetActivePageManager| has been called.
  return !has_external_requests_ && token_manager_.IsDiscardable() && active_page_manager_is_set_ &&
         (!active_page_manager_ || active_page_manager_->IsDiscardable());
}

void ActivePageManagerContainer::CheckDiscardable() {
  if (on_discardable_ && IsDiscardable()) {
    on_discardable_();
  }
}

}  // namespace ledger
