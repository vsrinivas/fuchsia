// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_C_SANITIZERS_HWASAN_STUBS_H_
#define ZIRCON_SYSTEM_ULIB_C_SANITIZERS_HWASAN_STUBS_H_

// These macros call HWASAN_STUB(NAME) for each __hwasan_NAME symbol
// that represents a function called by instrumented code.
//
// HWASAN_STUBS covers all the entry points.

#if __has_feature(hwaddress_sanitizer)
#define HWASAN_STUBS            \
  HWASAN_STUB(add_frame_record) \
  HWASAN_STUB(storeN)           \
  HWASAN_STUB(store1)           \
  HWASAN_STUB(store2)           \
  HWASAN_STUB(store4)           \
  HWASAN_STUB(store8)           \
  HWASAN_STUB(store16)          \
  HWASAN_STUB(loadN)            \
  HWASAN_STUB(load1)            \
  HWASAN_STUB(load2)            \
  HWASAN_STUB(load4)            \
  HWASAN_STUB(load8)            \
  HWASAN_STUB(load16)           \
  HWASAN_STUB(tag_memory)
#else
#define HWASAN_STUBS
#endif

#endif  // ZIRCON_SYSTEM_ULIB_C_SANITIZERS_HWASAN_STUBS_H_
