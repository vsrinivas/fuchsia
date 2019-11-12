// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_SYSLOG_FDIO_CONNECT_H_
#define ZIRCON_SYSTEM_ULIB_SYSLOG_FDIO_CONNECT_H_

#include <lib/zx/socket.h>

zx::socket connect_to_logger();

#endif  // ZIRCON_SYSTEM_ULIB_SYSLOG_FDIO_CONNECT_H_
