// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/binding.h>

mx_status_t read_driver_info(int fd, void *cookie,
                             void (*func)(magenta_note_driver_t* note,
                                          mx_bind_inst_t* binding,
                                          void *cookie));