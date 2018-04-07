// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/ledger_client/page_id.h"

#include "peridot/lib/base64url/base64url.h"

namespace modular {

ledger::PageId MakePageId(const std::string& value) {
  ledger::PageId page_id;
  memset(page_id.id.mutable_data(), 0, page_id.id.count());
  size_t size = std::min(value.length(), page_id.id.count());
  memcpy(page_id.id.mutable_data(), value.data(), size);
  return page_id;
}

ledger::PageId PageIdFromBase64(const std::string& base64) {
  // Both base64 libraries available to us require that we allocate an output
  // buffer large enough to decode any base64 string of the input length, which
  // for us it does not know contains padding since our target size is 16, so we
  // have to allocate an intermediate buffer. Hex would not require this but
  // results in a slightly larger transport size.

  std::string decoded;
  ledger::PageId page_id;

  if (base64url::Base64UrlDecode(base64, &decoded)) {
    size_t size;
    if (decoded.length() != page_id.id.count()) {
      FXL_LOG(ERROR) << "Unexpected page ID length for " << base64
                     << " (decodes to " << decoded.length() << " bytes; "
                     << page_id.id.count() << " expected)";
      size = std::min(decoded.length(), page_id.id.count());
      memset(page_id.id.mutable_data(), 0, page_id.id.count());
    } else {
      size = page_id.id.count();
    }

    memcpy(page_id.id.mutable_data(), decoded.data(), size);
  } else {
    FXL_LOG(ERROR) << "Unable to decode page ID " << base64;
  }

  return page_id;
}

std::string PageIdToBase64(const ledger::PageId& page_id) {
  return base64url::Base64UrlEncode(
      {reinterpret_cast<const char*>(page_id.id.data()), page_id.id.count()});
}

}  // namespace modular
