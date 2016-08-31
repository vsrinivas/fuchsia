// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_NETSTACK_DISPATCHER_H_
#define APPS_NETSTACK_DISPATCHER_H_

#include <magenta/types.h>

#include "apps/netstack/iostate.h"

mx_status_t dispatcher_add(mx_handle_t h, iostate_t* ios);

mx_handle_t dispatcher(void);

#endif  // APPS_NETSTACK_DISPATCHER_H_
