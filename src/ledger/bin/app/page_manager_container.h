// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_APP_PAGE_MANAGER_CONTAINER_H_
#define SRC_LEDGER_BIN_APP_PAGE_MANAGER_CONTAINER_H_

#include <string>

#include <lib/fidl/cpp/interface_request.h>
#include <lib/fit/function.h>

#include "src/ledger/bin/app/page_connection_notifier.h"
#include "src/ledger/bin/app/page_impl.h"
#include "src/ledger/bin/app/page_manager.h"
#include "src/ledger/bin/app/page_usage_listener.h"
#include "src/ledger/bin/app/types.h"
#include "src/ledger/bin/storage/public/types.h"

namespace ledger {

// Container for a PageManager that keeps tracks of in-flight page requests and
// callbacks and fires them when the PageManager is available.
class PageManagerContainer {
 public:
  PageManagerContainer(std::string ledger_name, storage::PageId page_id,
                       PageUsageListener* page_usage_listener);
  ~PageManagerContainer();

  void set_on_empty(fit::closure on_empty_callback);

  // Keeps track of |page| and |callback|. Binds |page| and fires |callback|
  // when a PageManager is available or an error occurs.
  void BindPage(fidl::InterfaceRequest<Page> page_request,
                fit::function<void(storage::Status)> callback);

  // Registers a new internal request for PageStorage.
  void NewInternalRequest(
      fit::function<void(storage::Status, ExpiringToken, PageManager*)>
          callback);

  // Sets the PageManager or the error status for the container. This notifies
  // all awaiting callbacks and binds all pages in case of success.
  void SetPageManager(storage::Status status,
                      std::unique_ptr<PageManager> page_manager);

  // Returns true if there is at least one active external page connection.
  bool PageConnectionIsOpen();

 private:
  // Checks whether this container is empty, and calls the |on_empty_callback_|
  // if it is.
  void CheckEmpty();

  const storage::PageId page_id_;

  std::unique_ptr<PageManager> page_manager_;
  // |status_| holds the status given to |SetPageManager|. If
  // |page_manager_is_set_| is true, |status_| is |storage::Status::OK| if and
  // only if |page_manager_| is not null.
  storage::Status status_ = storage::Status::OK;
  // |page_manager_is_set_| if |SetPageManager| has been called. |page_manager_|
  // may still be null.
  bool page_manager_is_set_ = false;

  PageConnectionNotifier connection_notifier_;
  // page_impls_ is only populated before page_manager_ is set. Once the
  // PageManager is created and assigned to page_manager_, the PageImpls stored
  // in page_impls_ are handed off to that PageManager and page_impls_ is not
  // used again.
  std::vector<std::pair<std::unique_ptr<PageImpl>,
                        fit::function<void(storage::Status)>>>
      page_impls_;
  std::vector<fit::function<void(storage::Status, ExpiringToken, PageManager*)>>
      internal_request_callbacks_;
  fit::closure on_empty_callback_;

  FXL_DISALLOW_COPY_AND_ASSIGN(PageManagerContainer);
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_APP_PAGE_MANAGER_CONTAINER_H_
