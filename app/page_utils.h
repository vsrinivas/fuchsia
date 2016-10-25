// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_APP_PAGE_UTILS_H_
#define APPS_LEDGER_APP_PAGE_UTILS_H_

#include "apps/ledger/api/ledger.mojom.h"
#include "apps/ledger/storage/public/types.h"

namespace ledger {

class PageUtils {
 public:
  // Converts a status from storage into a status from the mojo API. If the
  // storage status is storage::Status::NOT_FOUND, |not_found_status| will be
  // returned.
  static Status ConvertStatus(storage::Status status,
                              Status not_found_status = Status::INTERNAL_ERROR);
};

}  // namespace ledger

#endif  // APPS_LEDGER_APP_PAGE_UTILS_H_
