// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_SYSLOG_EXPORT_H_
#define ZIRCON_SYSTEM_ULIB_SYSLOG_EXPORT_H_

#include <zircon/compiler.h>

#ifdef SYSLOG_STATIC
#define SYSLOG_EXPORT
#else
#define SYSLOG_EXPORT __EXPORT
#endif

#endif  // ZIRCON_SYSTEM_ULIB_SYSLOG_EXPORT_H_
