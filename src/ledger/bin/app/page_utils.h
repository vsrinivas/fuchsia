// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_APP_PAGE_UTILS_H_
#define SRC_LEDGER_BIN_APP_PAGE_UTILS_H_

#include <lib/fit/function.h>
#include <lib/fsl/vmo/sized_vmo.h>

#include <functional>

#include "peridot/lib/convert/convert.h"
#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/bin/storage/public/page_storage.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/strings/string_view.h"

namespace ledger {

class PageUtils {
 public:
  // Converts a status from storage into a status from the FIDL API.
  static Status ConvertStatus(storage::Status status);

  // From a callback that takes as first argument a Status, returns one that
  // takes a storage::Status and use |ConvertStatus| to transform the received
  // value into the expected one.
  template <typename... A>
  static fit::function<void(storage::Status, A...)> AdaptStatusCallback(
      fit::function<void(Status, A...)> callback) {
    return [callback = std::move(callback)](storage::Status status, A... args) {
      callback(ConvertStatus(status), std::forward<A>(args)...);
    };
  }

  // Retrieves the data referenced by the given identifier as a StringView with
  // no offset.
  static void ResolveObjectIdentifierAsStringView(
      storage::PageStorage* storage,
      storage::ObjectIdentifier object_identifier,
      storage::PageStorage::Location location,
      fit::function<void(storage::Status, fxl::StringView)> callback);

  // Retrieves the data referenced by the given identifier and returns a subset
  // of its contents as a buffer. |offset| can be negative. In that case, the
  // offset is understood as starting from the end of the contents.
  static void ResolveObjectIdentifierAsBuffer(
      storage::PageStorage* storage,
      storage::ObjectIdentifier object_identifier, int64_t offset,
      int64_t max_size, storage::PageStorage::Location location,
      fit::function<void(storage::Status, fsl::SizedVmo)> callback);

  // Returns true if a key matches the provided prefix, false otherwise.
  static bool MatchesPrefix(const std::string& key, const std::string& prefix);

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(PageUtils);
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_APP_PAGE_UTILS_H_
