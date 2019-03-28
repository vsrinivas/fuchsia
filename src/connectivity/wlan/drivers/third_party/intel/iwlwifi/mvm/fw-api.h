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

#include "fw/api/alive.h"
#include "fw/api/binding.h"
#include "fw/api/cmdhdr.h"
#include "fw/api/coex.h"
#include "fw/api/commands.h"
#include "fw/api/config.h"
#include "fw/api/context.h"
#include "fw/api/d3.h"
#include "fw/api/datapath.h"
#include "fw/api/filter.h"
#include "fw/api/led.h"
#include "fw/api/mac-cfg.h"
#include "fw/api/mac.h"
#include "fw/api/nvm-reg.h"
#include "fw/api/offload.h"
#include "fw/api/phy-ctxt.h"
#include "fw/api/phy.h"
#include "fw/api/power.h"
#include "fw/api/rs.h"
#include "fw/api/rx.h"
#include "fw/api/scan.h"
#include "fw/api/sf.h"
#include "fw/api/soc.h"
#include "fw/api/sta.h"
#include "fw/api/stats.h"
#include "fw/api/tdls.h"
#include "fw/api/time-event.h"
#include "fw/api/tof.h"
#include "fw/api/tx.h"
#ifdef CPTCFG_IWLWIFI_DEBUG_HOST_CMD_ENABLED
#include "fw/api/dhc.h"
#endif
#include "fw/api/testing.h"

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_MVM_FW_API_H_
