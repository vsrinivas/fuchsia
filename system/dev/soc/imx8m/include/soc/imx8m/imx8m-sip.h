// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// ARM SiP (Silicon Provider) services, accessed via SMC (Secure Monitor Call).
#define IMX8M_SIP_GPC 0xC2000000
#define IMX8M_SIP_CONFIG_GPC_MASK 0x00
#define IMX8M_SIP_CONFIG_GPC_UNMASK 0x01
#define IMX8M_SIP_CONFIG_GPC_SET_WAKE 0x02
#define IMX8M_SIP_CONFIG_GPC_PM_DOMAIN 0x03

#define IMX8M_SIP_CPUFREQ 0xC2000001
#define IMX8M_SIP_SET_CPUFREQ 0x00

#define IMX8M_SIP_SRTC 0xC2000002
#define IMX8M_SIP_SRTC_SET_TIME 0x00
#define IMX8M_SIP_SRTC_START_WDOG 0x01
#define IMX8M_SIP_SRTC_STOP_WDOG 0x02
#define IMX8M_SIP_SRTC_SET_WDOG_ACT 0x03
#define IMX8M_SIP_SRTC_PING_WDOG 0x04
#define IMX8M_SIP_SRTC_SET_TIMEOUT_WDOG 0x05
#define IMX8M_SIP_SRTC_GET_WDOG_STAT 0x06
#define IMX8M_SIP_SRTC_SET_PRETIME_WDOG 0x07

#define IMX8M_SIP_DDR_DVFS 0xc2000004

// Power domains.
#define IMX8M_PD_MIPI 0
#define IMX8M_PD_PCIE0 1
#define IMX8M_PD_USB_OTG1 2
#define IMX8M_PD_USB_OTG2 3
#define IMX8M_PD_GPU 4
#define IMX8M_PD_VPU 5
#define IMX8M_PD_MIPI_CSI1 8
#define IMX8M_PD_MIPI_CSI2 9
#define IMX8M_PD_PCIE1 10
