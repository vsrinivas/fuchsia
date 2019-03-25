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

#include "debugfs.h"
#include "mvm.h"
#include "fw/api/ax-softap-testmode.h"
#include <net/mac80211.h>

static ssize_t
iwl_dbgfs_ax_softap_testmode_dl_basic_write(struct iwl_mvm *mvm,
					    char *buf, size_t count,
					    loff_t *ppos)
{
	struct ax_softap_testmode_dl_basic_cmd *cmd =
		(struct ax_softap_testmode_dl_basic_cmd *)buf;

	int ret;
	u32 status;

	if (sizeof(*cmd) != count) {
		IWL_ERR(mvm,
			"Bad size for softap dl basic cmd (%zd) should be (%zd)\n",
			count, sizeof(*cmd));
		return -EINVAL;
	}

	status = 0;

	mutex_lock(&mvm->mutex);
	ret = iwl_mvm_send_cmd_pdu_status(mvm,
					  iwl_cmd_id(AX_SOFTAP_TESTMODE_DL_BASIC,
						     DATA_PATH_GROUP, 0),
					  count, cmd, &status);
	mutex_unlock(&mvm->mutex);
	if (ret) {
		IWL_ERR(mvm, "Failed to send softap dl basic cmd (%d)\n",
			ret);
		return ret;
	}

	if (status) {
		IWL_ERR(mvm, "softap dl basic cmd failed (%d)\n",
			status);
		return -EIO;
	}

	return count;
}

static ssize_t
iwl_dbgfs_ax_softap_testmode_dl_mu_bar_write(struct iwl_mvm *mvm,
					     char *buf, size_t count,
					     loff_t *ppos)
{
	struct ax_softap_testmode_dl_mu_bar_cmd *cmd =
		(struct ax_softap_testmode_dl_mu_bar_cmd *)buf;

	int ret;
	u32 status;

	if (sizeof(*cmd) != count) {
		IWL_ERR(mvm,
			"Bad size for softap dl mu bar cmd (%zd) should be (%zd)\n",
			count, sizeof(*cmd));
		return -EINVAL;
	}

	status = 0;

	mutex_lock(&mvm->mutex);
	ret = iwl_mvm_send_cmd_pdu_status(mvm,
					  iwl_cmd_id(AX_SOFTAP_TESTMODE_DL_MU_BAR,
						     DATA_PATH_GROUP, 0),
					  count, cmd, &status);
	mutex_unlock(&mvm->mutex);
	if (ret) {
		IWL_ERR(mvm, "Failed to send softap dl mu bar cmd (%d)\n",
			ret);
		return ret;
	}

	if (status) {
		IWL_ERR(mvm, "softap dl mu bar cmd failed (%d)\n",
			status);
		return -EIO;
	}

	return count;
}

static ssize_t
iwl_dbgfs_ax_softap_testmode_ul_write(struct iwl_mvm *mvm,
				      char *buf, size_t count, loff_t *ppos)
{
	struct ax_softap_testmode_ul_cmd *cmd =
		(struct ax_softap_testmode_ul_cmd *)buf;

	int ret;
	u32 status;

	if (sizeof(*cmd) != count) {
		IWL_ERR(mvm,
			"Bad size for softap ul cmd (%zd) should be (%zd)\n",
			count, sizeof(*cmd));
		return -EINVAL;
	}

	status = 0;

	mutex_lock(&mvm->mutex);
	ret = iwl_mvm_send_cmd_pdu_status(mvm,
					  iwl_cmd_id(AX_SOFTAP_TESTMODE_UL,
						     DATA_PATH_GROUP, 0),
					  count, cmd, &status);
	mutex_unlock(&mvm->mutex);
	if (ret) {
		IWL_ERR(mvm, "Failed to send softap ul cmd (%d)\n",
			ret);
		return ret;
	}

	if (status) {
		IWL_ERR(mvm, "softap ul cmd failed (%d)\n",
			status);
		return -EIO;
	}

	return count;
}

#define MVM_DEBUGFS_WRITE_FILE_OPS(name, bufsz)				\
	_MVM_DEBUGFS_WRITE_FILE_OPS(name, bufsz, struct iwl_mvm)
#define MVM_DEBUGFS_ADD_FILE_AX_SOFTAP_TM(name, parent, mode) do {	\
		if (!debugfs_create_file(#name, mode, parent, mvm,	\
					 &iwl_dbgfs_##name##_ops))	\
			goto err;					\
	} while (0)

#define DL_BASIC_CMD_SIZE (sizeof(struct ax_softap_testmode_dl_basic_cmd) + 1)
#define DL_MU_BAR_CMD_SIZE (sizeof(struct ax_softap_testmode_dl_mu_bar_cmd) + 1)
#define UL_CMD_SIZE (sizeof(struct ax_softap_testmode_ul_cmd) + 1)

MVM_DEBUGFS_WRITE_FILE_OPS(ax_softap_testmode_dl_basic, DL_BASIC_CMD_SIZE);
MVM_DEBUGFS_WRITE_FILE_OPS(ax_softap_testmode_dl_mu_bar, DL_MU_BAR_CMD_SIZE);
MVM_DEBUGFS_WRITE_FILE_OPS(ax_softap_testmode_ul, UL_CMD_SIZE);

static void ax_softap_testmode_add_debugfs(struct ieee80211_hw *hw,
					   struct ieee80211_vif *vif,
					   struct ieee80211_sta *sta,
					   struct dentry *dir)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);

	MVM_DEBUGFS_ADD_FILE_AX_SOFTAP_TM(ax_softap_testmode_dl_basic,
					  dir, S_IWUSR);
	MVM_DEBUGFS_ADD_FILE_AX_SOFTAP_TM(ax_softap_testmode_dl_mu_bar,
					  dir, S_IWUSR);
	MVM_DEBUGFS_ADD_FILE_AX_SOFTAP_TM(ax_softap_testmode_ul,
					  dir, S_IWUSR);
	return;
err:
	IWL_ERR(mvm, "Can't create debugfs entity\n");
}

void
iwl_mvm_ax_softap_testmode_sta_add_debugfs(struct ieee80211_hw *hw,
					   struct ieee80211_vif *vif,
					   struct ieee80211_sta *sta,
					   struct dentry *dir)
{
	if (fw_has_capa(&IWL_MAC80211_GET_MVM(hw)->fw->ucode_capa,
			IWL_UCODE_TLV_CAPA_AX_SAP_TM_V2))
		ax_softap_testmode_add_debugfs(hw, vif, sta, dir);
}
