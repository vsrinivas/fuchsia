// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_COMMON_AML_SPI_H_
#define SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_COMMON_AML_SPI_H_

#define DEVICE_METADATA_AMLSPI_CS_MAPPING 0x53435364  // 'SCSd'

typedef struct {
  uint32_t bus_id;
  uint32_t cs_count;
  uint32_t cs[4];
} amlspi_cs_map_t;

#endif  // SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_COMMON_AML_SPI_H_
