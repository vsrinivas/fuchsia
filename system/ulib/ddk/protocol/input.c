// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/protocol/input.h>

const boot_kbd_report_t report_err_rollover = { .modifier = 1, .usage = {1, 1, 1, 1, 1, 1 } };
