/******************************************************************************
 *
 * Copyright(c) 2005 - 2014 Intel Corporation. All rights reserved.
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

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_XVT_USER_INFC_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_XVT_USER_INFC_H_

#include "iwl-tm-gnl.h"
#include "iwl-tm-infc.h"
#include "xvt.h"

/*
 * iwl_xvt_user_send_notif masks the usage of iwl-tm-gnl
 * If there is a need in replacing the interface, it
 * should be done only here.
 */
static inline int iwl_xvt_user_send_notif(struct iwl_xvt* xvt, uint32_t cmd, void* data, uint32_t size,
                                          gfp_t flags) {
    int err;
    IWL_DEBUG_INFO(xvt, "send user notification: cmd=0x%x, size=%d\n", cmd, size);
    err = iwl_tm_gnl_send_msg(xvt->trans, cmd, false, data, size, flags);

    WARN_ONCE(err, "failed to send notification to user, err %d\n", err);
    return err;
}

void iwl_xvt_send_user_rx_notif(struct iwl_xvt* xvt, struct iwl_rx_cmd_buffer* rxb);

int iwl_xvt_user_cmd_execute(struct iwl_testmode* testmode, uint32_t cmd, struct iwl_tm_data* data_in,
                             struct iwl_tm_data* data_out, bool* supported_cmd);

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_XVT_USER_INFC_H_
