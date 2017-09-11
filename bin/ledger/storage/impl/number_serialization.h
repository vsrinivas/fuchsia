// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef _APPS_LEDGER_SRC_STORAGE_IMPL_NUMBER_SERIALIZATION_H_
#define _APPS_LEDGER_SRC_STORAGE_IMPL_NUMBER_SERIALIZATION_H_

namespace storage {

template <typename I>
I DeserializeNumber(fxl::StringView value) {
  FXL_DCHECK(value.size() == sizeof(I));
  I result;
  memcpy(&result, value.data(), sizeof(I));
  return result;
}

template <typename I>
fxl::StringView SerializeNumber(const I& value) {
  return fxl::StringView(reinterpret_cast<const char*>(&value), sizeof(I));
}

}  // namespace storage

#endif  // _APPS_LEDGER_SRC_STORAGE_IMPL_NUMBER_SERIALIZATION_H_
