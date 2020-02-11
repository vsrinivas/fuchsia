// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_DRIVERS_NETWORK_DEVICE_MAC_LOG_H_
#define SRC_CONNECTIVITY_NETWORK_DRIVERS_NETWORK_DEVICE_MAC_LOG_H_

#ifndef NETDEV_DDK
#include <lib/syslog/global.h>
#define LOG_ERROR(msg) FX_LOG(ERROR, "network-mac", msg)
#define LOG_WARN(msg) FX_LOG(WARNING, "network-mac", msg)
#define LOG_INFO(msg) FX_LOG(INFO, "network-mac", msg)
#define LOG_TRACE(msg) FX_VLOG(1, "network-mac", msg)

#define LOGF_ERROR(fmt, ...) FX_LOGF(ERROR, "network-mac", fmt, ##__VA_ARGS__)
#define LOGF_WARN(fmt, ...) FX_LOGF(WARNING, "network-mac", fmt, ##__VA_ARGS__)
#define LOGF_INFO(fmt, ...) FX_LOGF(INFO, "network-mac", fmt, ##__VA_ARGS__)
#define LOGF_TRACE(fmt, ...) FX_VLOGF(1, "network-mac", fmt, ##__VA_ARGS__)
#else
#include <ddk/debug.h>
#define LOG_ERROR(msg) zxlogf(ERROR, msg "\n")
#define LOG_WARN(msg) zxlogf(WARN, msg "\n")
#define LOG_INFO(msg) zxlogf(INFO, msg "\n")
#define LOG_TRACE(msg) zxlogf(TRACE, msg "\n")

#define LOGF_ERROR(fmt, ...) zxlogf(ERROR, fmt "\n", ##__VA_ARGS__)
#define LOGF_WARN(fmt, ...) zxlogf(WARN, fmt "\n", ##__VA_ARGS__)
#define LOGF_INFO(fmt, ...) zxlogf(INFO, fmt "\n", ##__VA_ARGS__)
#define LOGF_TRACE(fmt, ...) zxlogf(TRACE, fmt "\n", ##__VA_ARGS__)
#endif

#endif  // SRC_CONNECTIVITY_NETWORK_DRIVERS_NETWORK_DEVICE_MAC_LOG_H_
