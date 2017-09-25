// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_APP_PAGE_UTILS_H_
#define PERIDOT_BIN_LEDGER_APP_PAGE_UTILS_H_

#include <functional>

#include "lib/ledger/fidl/ledger.fidl.h"
#include "peridot/bin/ledger/convert/convert.h"
#include "peridot/bin/ledger/storage/public/page_storage.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/strings/string_view.h"
#include "zx/vmo.h"

namespace ledger {

class PageUtils {
 public:
  // Converts a status from storage into a status from the FIDL API. If the
  // storage status is storage::Status::NOT_FOUND, |not_found_status| will be
  // returned.
  static Status ConvertStatus(storage::Status status,
                              Status not_found_status = Status::INTERNAL_ERROR);

  // Returns a Reference as a StringView with no offset.
  static void GetReferenceAsStringView(
      storage::PageStorage* storage,
      convert::ExtendedStringView opaque_id,
      storage::PageStorage::Location location,
      Status not_found_status,
      std::function<void(Status, fxl::StringView)> callback);

  // Returns a subset of a Reference contents as a buffer. |offset| can be
  // negative. In that case, the offset is understood as starting from the end
  // of the contents.
  static void GetPartialReferenceAsBuffer(
      storage::PageStorage* storage,
      convert::ExtendedStringView reference_id,
      int64_t offset,
      int64_t max_size,
      storage::PageStorage::Location location,
      Status not_found_status,
      std::function<void(Status, zx::vmo)> callback);

  // Returns true if a key matches the provided prefix, false otherwise.
  static bool MatchesPrefix(const std::string& key, const std::string& prefix);

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(PageUtils);
};

}  // namespace ledger

#endif  // PERIDOT_BIN_LEDGER_APP_PAGE_UTILS_H_
