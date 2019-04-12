/******************************************************************************
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
 * Copyright(c) 2016 - 2017 Intel Deutschland GmbH
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

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_MVM_FW_API_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_MVM_FW_API_H_

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fw/api/alive.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fw/api/binding.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fw/api/cmdhdr.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fw/api/coex.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fw/api/commands.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fw/api/config.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fw/api/context.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fw/api/d3.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fw/api/datapath.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fw/api/filter.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fw/api/led.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fw/api/mac-cfg.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fw/api/mac.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fw/api/nvm-reg.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fw/api/offload.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fw/api/phy-ctxt.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fw/api/phy.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fw/api/power.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fw/api/rs.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fw/api/rx.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fw/api/scan.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fw/api/sf.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fw/api/soc.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fw/api/sta.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fw/api/stats.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fw/api/tdls.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fw/api/time-event.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fw/api/tof.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fw/api/tx.h"
#ifdef CPTCFG_IWLWIFI_DEBUG_HOST_CMD_ENABLED
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fw/api/dhc.h"
#endif
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fw/api/testing.h"

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_MVM_FW_API_H_
