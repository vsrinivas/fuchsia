// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/agents/clipboard/clipboard_storage.h"
#include "lib/fsl/vmo/strings.h"
#include "lib/fxl/functional/make_copyable.h"

namespace modular {
namespace {

fidl::Array<uint8_t> ToArray(const std::string& str) {
  auto array = fidl::Array<uint8_t>::New(str.size());
  memcpy(array.data(), str.data(), str.size());
  return array;
}

std::string ToString(fsl::SizedVmoTransportPtr value) {
  fsl::SizedVmo vmo;
  std::string parsed_string;
  if (!fsl::SizedVmo::FromTransport(std::move(value), &vmo)) {
    FXL_LOG(ERROR) << "Could not decode clipboard value.";
  }
  if (!fsl::StringFromVmo(vmo, &parsed_string)) {
    FXL_LOG(ERROR) << "Clipboard vmo could not be decoded to string.";
  }
  return parsed_string;
}

// Returns a closure logs the provided error message if the Ledger operation
// doesn't return ledger::Status::OK.
std::function<void(ledger::Status)> LedgerErrorReportingCallback(
    const std::string& error_message) {
  return [error_message](ledger::Status status) {
    if (status != ledger::Status::OK) {
      FXL_LOG(ERROR) << error_message;
    }
  };
}

// The Ledger key that is used to store the current value.
constexpr char kCurrentValueKey[] = "current_value";

}  // namespace

ClipboardStorage::ClipboardStorage(LedgerClient* ledger_client,
                                   LedgerPageId page_id)
    : PageClient("ClipboardStorage", ledger_client, std::move(page_id)) {}

ClipboardStorage::~ClipboardStorage() = default;

void ClipboardStorage::Push(const fidl::String& text) {
  page()->Put(ToArray(kCurrentValueKey), ToArray(text),
              LedgerErrorReportingCallback("Failed to put text."));
}

void ClipboardStorage::Peek(const std::function<void(fidl::String)>& callback) {
  ledger::PageSnapshotPtr snapshot;
  page()->GetSnapshot(snapshot.NewRequest(), nullptr, nullptr,
                      LedgerErrorReportingCallback(
                          "Failed to get the latest snapshot from page."));
  snapshot->Get(ToArray(kCurrentValueKey),
                fxl::MakeCopyable([callback, snapshot = std::move(snapshot)](
                                      ledger::Status status,
                                      fsl::SizedVmoTransportPtr value) mutable {
                  callback(ToString(std::move(value)));
                }));
  ;
}

}  // namespace modular