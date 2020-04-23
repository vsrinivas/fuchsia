// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_S905D2_AML_MALI_H_
#define SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_S905D2_AML_MALI_H_

#include <ddk/protocol/platform/bus.h>

zx_status_t aml_mali_init(pbus_protocol_t* pbus, uint32_t bti_index);

#endif  // SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_S905D2_AML_MALI_H_
