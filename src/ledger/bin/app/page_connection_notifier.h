// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_APP_PAGE_CONNECTION_NOTIFIER_H_
#define SRC_LEDGER_BIN_APP_PAGE_CONNECTION_NOTIFIER_H_

#include <lib/fit/function.h>
#include <lib/fxl/memory/weak_ptr.h>
#include <trace/event.h>

#include "src/ledger/bin/app/page_usage_listener.h"
#include "src/ledger/bin/app/types.h"
#include "src/ledger/bin/storage/public/types.h"

namespace ledger {

// A notifier for |PageUsageListener|.
//
// Given information about when internal and external page connections open and
// close, |PageConnectionNotifier| calls the corresponding methods from
// |PageUsageListener|. The |PageUsageListener| given in the constructor should
// outlive this object.
class PageConnectionNotifier {
 public:
  PageConnectionNotifier(std::string ledger_name, storage::PageId page_id,
                         PageUsageListener* page_usage_listener);
  ~PageConnectionNotifier();

  // Registers a new external page request.
  void RegisterExternalRequest();

  // Unregisters all active external page requests. This can be because all
  // active connections were closed, or because of failure to bind the requests.
  void UnregisterExternalRequests();

  // Registers a new internal page request, and returns a token. The internal
  // request is unregistered when the token is destructed.
  ExpiringToken NewInternalRequestToken();

  // Sets the on_empty callback, to be called every time this object becomes
  // empty.
  void set_on_empty(fit::closure on_empty_callback);

  // Checks and returns whether there are no active external or internal
  // requests.
  bool IsEmpty();

 private:
  // Checks whether this object is empty, and if it is and the on_empty callback
  // is set, calls it.
  void CheckEmpty();

  const std::string ledger_name_;
  const storage::PageId page_id_;
  PageUsageListener* page_usage_listener_;

  // Stores whether the page was opened by an external request but did not yet
  // send a corresponding OnPageUnused. The OnPageUnused notification is sent as
  // soon as all internal and external requests to the page are done.
  bool must_notify_on_page_unused_ = false;
  // Stores whether the page is currently opened by an external request.
  bool has_external_requests_ = false;
  // Stores the number of active internal requests.
  ssize_t internal_request_count_ = 0;

  fit::closure on_empty_callback_;

  // Must be the last member.
  fxl::WeakPtrFactory<PageConnectionNotifier> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(PageConnectionNotifier);
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_APP_PAGE_CONNECTION_NOTIFIER_H_
