// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_SYSLOG_FDIO_CONNECT_H_
#define ZIRCON_SYSTEM_ULIB_SYSLOG_FDIO_CONNECT_H_

#include <lib/zx/socket.h>

// Connects to the logger. By default, we use the unstructured logger.
// This will default to false until we've removed all dependencies
// on unstructured logging. Currently, to be cautious
// we default to false for cases where we're not sure it's safe
// (such as Vulkan). This also allows for backwards-compatibility
// with out-of-tree code that might still be manually calling Connect
// instead of ConnectStructured.
zx::socket connect_to_logger(bool structured = false);

#endif  // ZIRCON_SYSTEM_ULIB_SYSLOG_FDIO_CONNECT_H_
