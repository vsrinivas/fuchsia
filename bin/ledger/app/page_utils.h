// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_APP_PAGE_UTILS_H_
#define APPS_LEDGER_SRC_APP_PAGE_UTILS_H_

#include <functional>

#include "apps/ledger/services/public/ledger.fidl.h"
#include "apps/ledger/src/convert/convert.h"
#include "apps/ledger/src/storage/public/page_storage.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/strings/string_view.h"

namespace ledger {

class PageUtils {
 public:
  // Converts a status from storage into a status from the mojo API. If the
  // storage status is storage::Status::NOT_FOUND, |not_found_status| will be
  // returned.
  static Status ConvertStatus(storage::Status status,
                              Status not_found_status = Status::INTERNAL_ERROR);

  // Returns a Reference contents as a ValuePtr.
  static void GetReferenceAsValuePtr(
      storage::PageStorage* storage,
      convert::ExtendedStringView reference_id,
      std::function<void(Status, ValuePtr)> callback);

  // Returns a subset of a Reference contents as a buffer. |offset| can be
  // negative. In that case, the offset is understood as starting from the end
  // of the contents.
  static void GetPartialReferenceAsBuffer(
      storage::PageStorage* storage,
      convert::ExtendedStringView reference_id,
      int64_t offset,
      int64_t max_size,
      std::function<void(Status, mx::vmo)> callback);

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(PageUtils);
};

}  // namespace ledger

#endif  // APPS_LEDGER_SRC_APP_PAGE_UTILS_H_
