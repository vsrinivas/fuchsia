// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <unittest/unittest.h>

extern bool check_dso_ctor();
extern bool check_dso_tlocal_in_thread();
extern bool check_dso_tlocal_after_join();

template<bool* tlocal_ctor_ran, bool* tlocal_dtor_ran>
struct ThreadLocal {
    bool flag = false;
    ThreadLocal() {
        *tlocal_ctor_ran = true;
    }
    ~ThreadLocal() {
        *tlocal_dtor_ran = true;
    }
    static bool check_before_reference() {
        BEGIN_HELPER;
        EXPECT_FALSE(*tlocal_ctor_ran);
        EXPECT_FALSE(*tlocal_dtor_ran);
        END_HELPER;
    }
    static bool check_after_reference() {
        BEGIN_HELPER;
        EXPECT_TRUE(*tlocal_ctor_ran);
        EXPECT_FALSE(*tlocal_dtor_ran);
        END_HELPER;
    }
    static bool check_after_join() {
        BEGIN_HELPER;
        EXPECT_TRUE(*tlocal_ctor_ran);
        EXPECT_TRUE(*tlocal_dtor_ran);
        END_HELPER;
    }
};
