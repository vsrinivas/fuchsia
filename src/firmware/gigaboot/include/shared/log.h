// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_FIRMWARE_GIGABOOT_INCLUDE_SHARED_LOG_H_
#define SRC_FIRMWARE_GIGABOOT_INCLUDE_SHARED_LOG_H_

#include <xefi.h>

// LOG/WLOG/ELOG are always printed, the only difference is that WLOG and ELOG
// are prefixed with "Warning" and "Error" strings.
//
// All logging macros automatically append \n, the caller does not need to.
#define LOG(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)
#define WLOG(fmt, ...) LOG("Warning: " fmt, ##__VA_ARGS__)
#define ELOG(fmt, ...) LOG("Error: " fmt, ##__VA_ARGS__)

// *LOG_S variations also append the string representation of an efi_status.
#define LOG_S(status, fmt, ...) LOG(fmt " (%s)", ##__VA_ARGS__, xefi_strerror(status))
#define WLOG_S(status, fmt, ...) WLOG(fmt " (%s)", ##__VA_ARGS__, xefi_strerror(status))
#define ELOG_S(status, fmt, ...) ELOG(fmt " (%s)", ##__VA_ARGS__, xefi_strerror(status))

// DLOG is only printed if DEBUG_LOGGING was set when this file was included.
// These macros also prepend the function and line to help with debugging.
#if DEBUG_LOGGING
#define DLOG(fmt, ...) LOG("%s:%d: " fmt, __func__, __LINE__, ##__VA_ARGS__)
#define DLOG_S(status, fmt, ...) LOG_S(status, "%s:%d: " fmt, __func__, __LINE__, ##__VA_ARGS__)
#else
#define DLOG(...)
#define DLOG_S(...)
#endif

#endif  // SRC_FIRMWARE_GIGABOOT_INCLUDE_SHARED_LOG_H_
