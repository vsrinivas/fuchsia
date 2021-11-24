// Copyright 2021 The Fuchsia Authors
// Copyright (c) 2016, Google, Inc. All rights reserved
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_DEV_HDCP_AMLOGIC_S912_INCLUDE_DEV_HDCP_AMLOGIC_S912_INIT_H_
#define ZIRCON_KERNEL_DEV_HDCP_AMLOGIC_S912_INCLUDE_DEV_HDCP_AMLOGIC_S912_INIT_H_

#include <zircon/boot/driver-config.h>

// Initializes the driver.
void AmlogicS912HdcpInit(const dcfg_amlogic_hdcp_driver_t& config);

#endif  // ZIRCON_KERNEL_DEV_HDCP_AMLOGIC_S912_INCLUDE_DEV_HDCP_AMLOGIC_S912_INIT_H_
