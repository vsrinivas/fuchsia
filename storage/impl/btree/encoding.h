// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_STORAGE_IMPL_BTREE_ENCODING_H_
#define APPS_LEDGER_STORAGE_IMPL_BTREE_ENCODING_H_

#include <string>

#include "apps/ledger/storage/public/types.h"
#include "lib/ftl/strings/string_view.h"

namespace storage {

std::string EncodeNode(const std::vector<Entry>& entries,
                       const std::vector<ObjectId>& children);

bool DecodeNode(ftl::StringView json,
                std::vector<Entry>* entries,
                std::vector<ObjectId>* children);

}  // namespace storage

#endif  // APPS_LEDGER_STORAGE_IMPL_BTREE_ENCODING_H_
