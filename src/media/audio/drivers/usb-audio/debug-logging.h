// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_DRIVERS_USB_AUDIO_DEBUG_LOGGING_H_
#define SRC_MEDIA_AUDIO_DRIVERS_USB_AUDIO_DEBUG_LOGGING_H_

#include <inttypes.h>

#include <ddk/debug.h>

// Notes: The TRACE and SPEW levels of logging are disabled by default.  In
// order to enable them, you can pass something like the following in the kernel
// command line args.
//
//   driver.usb_audio.log=+trace,+spew
//
constexpr size_t LOG_PREFIX_STORAGE = 32;

#define GLOBAL_LOG(level, ...) zxlogf(level, "[USBAud] " __VA_ARGS__)
#define LOG_EX(level, obj, fmt, ...) zxlogf(level, "[%s] " fmt, (obj).log_prefix(), ##__VA_ARGS__)
#define LOG(level, fmt, ...) LOG_EX(level, *this, fmt, ##__VA_ARGS__)

#endif  // SRC_MEDIA_AUDIO_DRIVERS_USB_AUDIO_DEBUG_LOGGING_H_
