// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_UTEST_CTOR_DSO_CTOR_DSO_CTOR_H_
#define ZIRCON_SYSTEM_UTEST_CTOR_DSO_CTOR_DSO_CTOR_H_

#include <zircon/assert.h>

extern void check_dso_ctor();
extern void check_dso_tlocal_in_thread();
extern void check_dso_tlocal_after_join();

template <bool* tlocal_ctor_ran, bool* tlocal_dtor_ran>
struct ThreadLocal {
  bool flag = false;
  ThreadLocal() { *tlocal_ctor_ran = true; }
  ~ThreadLocal() { *tlocal_dtor_ran = true; }
  static void check_before_reference() {
    ZX_ASSERT(!*tlocal_ctor_ran);
    ZX_ASSERT(!*tlocal_dtor_ran);
  }
  static void check_after_reference() {
    ZX_ASSERT(*tlocal_ctor_ran);
    ZX_ASSERT(!*tlocal_dtor_ran);
  }
  static void check_after_join() {
    ZX_ASSERT(*tlocal_ctor_ran);
    ZX_ASSERT(*tlocal_dtor_ran);
  }
};

#endif  // ZIRCON_SYSTEM_UTEST_CTOR_DSO_CTOR_DSO_CTOR_H_
