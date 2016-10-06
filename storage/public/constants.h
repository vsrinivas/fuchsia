// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_STORAGE_PUBLIC_CONSTANTS_H_
#define APPS_LEDGER_STORAGE_PUBLIC_CONSTANTS_H_

namespace storage {

// The size of a commit id in number of bytes.
extern const unsigned long kCommitIdSize;

// The size of an object id in number of bytes.
extern const unsigned long kObjectIdSize;

extern const char kFirstPageCommitId[];

}  // namespace storage

#endif  // APPS_LEDGER_STORAGE_PUBLIC_CONSTANTS_H_
