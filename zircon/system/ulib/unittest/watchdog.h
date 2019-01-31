// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <unittest/unittest.h>

void watchdog_set_base_timeout(int seconds);

bool watchdog_is_enabled();

void watchdog_initialize();

void watchdog_terminate();

void watchdog_start(test_type_t type, const char* name);

void watchdog_cancel();
