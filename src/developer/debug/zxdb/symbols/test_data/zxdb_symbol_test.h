// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_TEST_DATA_ZXDB_SYMBOL_TEST_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_TEST_DATA_ZXDB_SYMBOL_TEST_H_

// Mark the exported symbols to prevent the linker from stripping them.
#define EXPORT __attribute__((visibility("default")))
#define NOINLINE __attribute__((noinline))

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_TEST_DATA_ZXDB_SYMBOL_TEST_H_
