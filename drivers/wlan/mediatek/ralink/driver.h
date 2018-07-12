// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/async/dispatcher.h>

// Retrieves the async_dispatcher_t* for this driver.
//
// This pointer is guaranteed to be valid after the driver .init hook returns and before the driver
// .release hook is called. Therefore any device created and bound by this driver may assume the
// async_dispatcher_t* is initialized and running.
async_dispatcher_t* ralink_async_t();
