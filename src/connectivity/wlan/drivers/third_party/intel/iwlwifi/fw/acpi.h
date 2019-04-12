/******************************************************************************
 *
 * Copyright(c) 2017        Intel Deutschland GmbH
 * Copyright(c) 2018        Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *****************************************************************************/
#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_FW_ACPI_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_FW_ACPI_H_

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fuchsia_porting.h"

#define ACPI_WRDS_METHOD "WRDS"
#define ACPI_EWRD_METHOD "EWRD"
#define ACPI_WGDS_METHOD "WGDS"
#define ACPI_WRDD_METHOD "WRDD"
#define ACPI_SPLC_METHOD "SPLC"

#define ACPI_WIFI_DOMAIN (0x07)

#define ACPI_SAR_TABLE_SIZE 10
#define ACPI_SAR_PROFILE_NUM 4

#define ACPI_GEO_TABLE_SIZE 6
#define ACPI_NUM_GEO_PROFILES 3
#define ACPI_GEO_PER_CHAIN_SIZE 3

#define ACPI_SAR_NUM_CHAIN_LIMITS 2
#define ACPI_SAR_NUM_SUB_BANDS 5

#define ACPI_WRDS_WIFI_DATA_SIZE (ACPI_SAR_TABLE_SIZE + 2)
#define ACPI_EWRD_WIFI_DATA_SIZE ((ACPI_SAR_PROFILE_NUM - 1) * ACPI_SAR_TABLE_SIZE + 3)
#define ACPI_WGDS_WIFI_DATA_SIZE 19
#define ACPI_WRDD_WIFI_DATA_SIZE 2
#define ACPI_SPLC_WIFI_DATA_SIZE 2

#define ACPI_WGDS_NUM_BANDS 2
#define ACPI_WGDS_TABLE_SIZE 3

#ifdef CONFIG_ACPI

void* iwl_acpi_get_object(struct device* dev, acpi_string method);
union acpi_object* iwl_acpi_get_wifi_pkg(struct device* dev, union acpi_object* data,
                                         int data_size);

/**
 * iwl_acpi_get_mcc - read MCC from ACPI, if available
 *
 * @dev: the struct device
 * @mcc: output buffer (3 bytes) that will get the MCC
 *
 * This function tries to read the current MCC from ACPI if available.
 */
int iwl_acpi_get_mcc(struct device* dev, char* mcc);

uint64_t iwl_acpi_get_pwr_limit(struct device* dev);

#else /* CONFIG_ACPI */

static inline void* iwl_acpi_get_object(struct device* dev, acpi_string method) {
    // NEEDS_PORTING return ERR_PTR(-ENOENT);
    return NULL;
}

static inline union acpi_object* iwl_acpi_get_wifi_pkg(struct device* dev, union acpi_object* data,
                                                       int data_size) {
    // NEEDS_PORTING return ERR_PTR(-ENOENT);
    return NULL;
}

static inline zx_status_t iwl_acpi_get_mcc(struct device* dev, char* mcc) {
    return ZX_ERR_NOT_FOUND;
}

static inline uint64_t iwl_acpi_get_pwr_limit(struct device* dev) {
    return 0;
}

#endif  /* CONFIG_ACPI */
#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_FW_ACPI_H_
