// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/testing/quit_on_error.h"

#include <lib/fit/function.h>
#include <zircon/errors.h>
#include <zircon/status.h>

#include <sstream>

#include "src/ledger/lib/logging/logging.h"
#include "src/lib/fxl/logging.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace ledger {

namespace internal {

namespace {
template <typename E>
std::string FidlEnumToString(E e) {
  std::stringstream ss;
  ss << fidl::ToUnderlying(e);
  return ss.str();
}

template <>
std::string FidlEnumToString<zx_status_t>(zx_status_t status) {
  return zx_status_get_string(status);
}

template <typename R>
std::string FidlResultToString(const R& r) {
  if (r.is_response()) {
    return "";
  }
  return FidlEnumToString(r.err());
};
}  // namespace

StatusTranslater::StatusTranslater(Status status)
    : ok_(status == Status::OK), description_(FidlEnumToString(status)) {}

StatusTranslater::StatusTranslater(zx_status_t status)
    : ok_(status == ZX_OK || status == ZX_ERR_PEER_CLOSED),
      description_(zx_status_get_string(status)) {}

StatusTranslater::StatusTranslater(
    const fuchsia::ledger::Page_CreateReferenceFromBuffer_Result& result)
    : ok_(result.is_response()), description_(FidlResultToString(result)) {}

StatusTranslater::StatusTranslater(const fuchsia::ledger::PageSnapshot_Get_Result& result)
    : ok_(result.is_response()), description_(FidlResultToString(result)) {}

StatusTranslater::StatusTranslater(const fuchsia::ledger::PageSnapshot_GetInline_Result& result)
    : ok_(result.is_response()), description_(FidlResultToString(result)) {}

StatusTranslater::StatusTranslater(const fuchsia::ledger::PageSnapshot_Fetch_Result& result)
    : ok_(result.is_response()), description_(FidlResultToString(result)) {}

StatusTranslater::StatusTranslater(const fuchsia::ledger::PageSnapshot_FetchPartial_Result& result)
    : ok_(result.is_response()), description_(FidlResultToString(result)) {}
}  // namespace internal

bool QuitOnError(fit::closure quit_callback, internal::StatusTranslater status,
                 absl::string_view description) {
  if (status.ok()) {
    return false;
  }
  LEDGER_LOG(ERROR) << description << " failed with status " << status.description() << ".";
  quit_callback();
  return true;
}

}  // namespace ledger
