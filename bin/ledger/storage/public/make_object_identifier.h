// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_STORAGE_PUBLIC_MAKE_OBJECT_IDENTIFIER_H_
#define PERIDOT_BIN_LEDGER_STORAGE_PUBLIC_MAKE_OBJECT_IDENTIFIER_H_

#include "peridot/bin/ledger/storage/public/types.h"

namespace storage {

// Creates an |ObjectIdentifier| from an |ObjectDigest|.
//
// TODO(qsr): This is only used until LE-286 (real encryption) is implemented.
ObjectIdentifier MakeDefaultObjectIdentifier(ObjectDigest digest);

}  // namespace storage

#endif  // PERIDOT_BIN_LEDGER_STORAGE_PUBLIC_MAKE_OBJECT_IDENTIFIER_H_
