// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/inspect/inspect.h"

#include <fuchsia/ledger/cpp/fidl.h>

#include <cctype>
#include <string>

#include "src/ledger/bin/app/constants.h"
#include "src/ledger/bin/storage/public/constants.h"
#include "src/ledger/bin/storage/public/types.h"
#include "third_party/abseil-cpp/absl/strings/escaping.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace ledger {
namespace {

// TODO(https://bugs.fuchsia.dev/p/fuchsia/issues/detail?id=12294): Support
// unicode.
bool IsStringPrintable(absl::string_view input) {
  // Just ASCII for the time being. Sorry unicode!
  return std::all_of(input.begin(), input.end(),
                     [](unsigned char c) { return c >= 32 && c < 128; });
}

// TODO(https://github.com/abseil/abseil-cpp/issues/141): This shouldn't be
// necessary; eliminate  it.
bool IsHex(absl::string_view input) {
  return (input.size() % 2 == 0) &&
         std::all_of(input.begin(), input.end(), [](unsigned char c) { return std::isxdigit(c); });
}

// Modifies |page_id| to be the hex-decoding of |data|. (Preconditions: |data|
// must be the hex-encoding of some PageId and |page_id| must be empty.)
bool FromHex(absl::string_view data, storage::PageId* page_id) {
  FXL_DCHECK(page_id->empty());
  if (!IsHex(data)) {
    return false;
  }
  *page_id = absl::HexStringToBytes(data);
  return true;
}

}  // namespace

std::string PageIdToDisplayName(const storage::PageId& page_id) {
  if (IsStringPrintable(page_id)) {
    return convert::ToHex(page_id) + " (\"" + page_id + "\")";
  } else {
    return convert::ToHex(page_id);
  }
}

bool PageDisplayNameToPageId(const std::string& page_display_name, storage::PageId* page_id) {
  if (page_display_name.size() < fuchsia::ledger::PAGE_ID_SIZE * 2) {
    return false;
  }
  return FromHex(absl::string_view(page_display_name).substr(0, fuchsia::ledger::PAGE_ID_SIZE * 2),
                 page_id);
}

std::string CommitIdToDisplayName(const storage::CommitId& commit_id) {
  return convert::ToHex(commit_id);
}

bool CommitDisplayNameToCommitId(const std::string& commit_display_name,
                                 storage::CommitId* commit_id) {
  if (commit_display_name.size() != storage::kCommitIdSize * 2) {
    return false;
  } else {
    return FromHex(commit_display_name, commit_id);
  }
}

std::string KeyToDisplayName(const std::string& key) {
  // NOTE(nathaniel): 48 chosen arbirarily; no particular meaning to it other
  // than how "("<- text 24 chars wide ->") <- hex 48 chars wide ->" seems to
  // look in a terminal.
  if (IsStringPrintable(key) && key.size() < 48) {
    return "(\"" + key + "\") " + convert::ToHex(key);
  } else {
    return convert::ToHex(key);
  }
}

bool KeyDisplayNameToKey(const std::string& key_display_name, std::string* key) {
  absl::string_view hex_portion = key_display_name;
  if (key_display_name.size() >= 5) {
    size_t key_length = (key_display_name.size() - 5) / 3;
    if (key_display_name[key_length + 4] == ' ') {
      hex_portion = hex_portion.substr(key_length + 5);
    }
  }
  return hex_portion.size() <= kMaxKeySize * 2 && FromHex(hex_portion, key);
}

}  // namespace ledger
