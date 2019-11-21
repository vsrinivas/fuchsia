// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_APP_PAGE_UTILS_H_
#define SRC_LEDGER_BIN_APP_PAGE_UTILS_H_

#include <lib/fit/function.h>

#include <functional>

#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/bin/storage/public/page_storage.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/lib/fsl/vmo/sized_vmo.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/strings/string_view.h"

namespace ledger {

class PageUtils {
 public:
  // Retrieves the data referenced by the given identifier as a StringView with
  // no offset.
  static void ResolveObjectIdentifierAsStringView(
      storage::PageStorage* storage, storage::ObjectIdentifier object_identifier,
      storage::PageStorage::Location location,
      fit::function<void(Status, fxl::StringView)> callback);

  // Returns true if a key matches the provided prefix, false otherwise.
  static bool MatchesPrefix(const std::string& key, const std::string& prefix);

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(PageUtils);
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_APP_PAGE_UTILS_H_
