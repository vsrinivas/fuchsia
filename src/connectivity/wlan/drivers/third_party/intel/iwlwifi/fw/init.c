/******************************************************************************
 *
 * Copyright(c) 2017 Intel Deutschland GmbH
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
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fw/dbg.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fw/debugfs.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fw/runtime.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-drv.h"

void iwl_fw_runtime_init(struct iwl_fw_runtime* fwrt, struct iwl_trans* trans,
                         const struct iwl_fw* fw, const struct iwl_fw_runtime_ops* ops,
                         void* ops_ctx, struct dentry* dbgfs_dir) {
    memset(fwrt, 0, sizeof(*fwrt));
    fwrt->trans = trans;
    fwrt->fw = fw;
    fwrt->dev = trans->dev;
    fwrt->dump.conf = FW_DBG_INVALID;
    fwrt->ops = ops;
    fwrt->ops_ctx = ops_ctx;
#if 0   // NEEDS_PORTING
    INIT_DELAYED_WORK(&fwrt->dump.wk, iwl_fw_error_dump_wk);
#endif  // NEEDS_PORTING
    iwl_fwrt_dbgfs_register(fwrt, dbgfs_dir);
}

void iwl_fw_runtime_suspend(struct iwl_fw_runtime* fwrt) {
    iwl_fw_suspend_timestamp(fwrt);
}

void iwl_fw_runtime_resume(struct iwl_fw_runtime* fwrt) {
    iwl_fw_resume_timestamp(fwrt);
}
