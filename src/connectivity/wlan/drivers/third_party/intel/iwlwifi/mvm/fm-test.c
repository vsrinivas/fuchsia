/******************************************************************************
 *
 * Copyright(c) 2013 - 2015 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
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
#include <linux/debugfs.h>
#include <linux/fm/iui_fm.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>

/*
 * UT framework to debug Frequency Manager on any platform.
 *
 * The framework contains stub functions & debugfs entries so the iwlwifi driver
 * can run the FM code  without having an active FM in the background.
 *
 * FM--> iwlwifi driver API:
 * Done via debugfs entry: request the driver to mitigate its activity.
 * Through the debugfs the user can request driver to start tx power &
 * 2g coex mitigation.
 *
 * Iwlwfi driver-->FM API
 * Done via internal "stub" functions that will simulate the calls to the FM
 * If the debug mode is set, the calls to the real API functions are ignored,
 * and the stubs are called instead.
 *
 * To activate the FM debug mode you need to:
 * 1. Enable the IWLWIFI_FRQ_MGR_TEST config (it will enable by default the
 * IWLWIFI_FRQ_MGR config)
 * 2. add fm_debug_mode=0x1 in the file iwl-dbg-cfg.ini
 *
 * NOTE: If the platform does not have a FM then the ut will use the
 * include/linux/fm/iui_fm_test.h header file, otherwise it will use the
 * platform header file.
 */

/* pointer to callback function in debug mode */
static iui_fm_mitigation_cb fm_callback;
static struct iui_fm_wlan_info fm_notif;
static struct dentry* fm_debug_dir;

static ssize_t iwl_mvm_fm_debug_mitigate_write(struct file* file, const char __user* user_buf,
                                               size_t count, loff_t* ppos) {
    struct iui_fm_mitigation mitigation;
    struct iui_fm_wlan_mitigation wm;
    char buf[128];
    size_t buf_size = sizeof(buf);
    int mitigate_2g;
    int ret;
    int mitigate_dcdc;

    mitigation.info.wlan_mitigation = &wm;
    mitigation.type = IUI_FM_MITIGATION_TYPE_WLAN;

    if (copy_from_user(buf, user_buf, buf_size)) { return -EFAULT; }

    /* All platforms that are not xmm6321 & SOFIA 3G */
    if (IUI_FM_WLAN_MAX_CHANNELS == 4) {
        if (sscanf(buf, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d", &wm.num_channels,
                   &wm.channel_tx_pwr[0].frequency, &wm.channel_tx_pwr[0].max_tx_pwr,
                   &wm.channel_tx_pwr[1].frequency, &wm.channel_tx_pwr[1].max_tx_pwr,
                   &wm.channel_tx_pwr[2].frequency, &wm.channel_tx_pwr[2].max_tx_pwr,
                   &wm.channel_tx_pwr[3].frequency, &wm.channel_tx_pwr[3].max_tx_pwr,
                   &mitigate_dcdc, &wm.dcdc_div0, &wm.dcdc_div1, &mitigate_2g,
                   &wm.wlan_2g_coex_enable) != 14) {
            return -EINVAL;
        }
    } else if (sscanf(buf, "%d,%d,%d,%d,%d", &wm.num_channels, &wm.channel_tx_pwr[0].frequency,
                      &wm.channel_tx_pwr[0].max_tx_pwr, &wm.channel_tx_pwr[1].frequency,
                      &wm.channel_tx_pwr[1].max_tx_pwr) != 5) {
        return -EINVAL;
    }

    if (IUI_FM_WLAN_MAX_CHANNELS < wm.num_channels) { return -EINVAL; }

    wm.wlan_adc_dac_freq = 0;
    wm.rx_gain_behavior = IUI_FM_WLAN_RX_GAIN_NORMAL;

    wm.bitmask = 0;

    /* Set bit bitmask to indicate the required mitigations */
    if (wm.num_channels || mitigate_2g) { wm.bitmask |= WLAN_MITI; }
    if (mitigate_dcdc) { wm.bitmask |= DCDC_MITI; }

    ret = fm_callback(IUI_FM_MACRO_ID_WLAN, &mitigation, 0);
    pr_info("FM[test-mode]: mitigation callback %s (bitmask = 0x%x)\n",
            ret ? "failed" : "succeeded", wm.bitmask);

    return count;
}

static ssize_t iwl_mvm_fm_debug_notify_read(struct file* file, char __user* userbuf, size_t count,
                                            loff_t* ppos) {
    char buf[512];
    int bufsz = sizeof(buf);
    int pos = 0;
    u8 i;

    pos += scnprintf(buf + pos, bufsz - pos, "num_channels=%d\n", fm_notif.num_channels);
    for (i = 0; i < fm_notif.num_channels; i++)
        pos += scnprintf(buf + pos, bufsz - pos, "channel=%d, bandwidth=%d\n",
                         fm_notif.channel_info[i].frequency, fm_notif.channel_info[i].bandwidth);

    pos += scnprintf(buf + pos, bufsz - pos, "dcdc_div0=%d\n", fm_notif.dcdc_div0);
    pos += scnprintf(buf + pos, bufsz - pos, "dcdc_div1=%d\n", fm_notif.dcdc_div1);

    return simple_read_from_buffer(userbuf, count, ppos, buf, pos);
}

static const struct file_operations fm_debug_mitigate_ops = {
    .write = iwl_mvm_fm_debug_mitigate_write,
    .open = simple_open,
    .llseek = generic_file_llseek,
};

static const struct file_operations fm_debug_notify_ops = {
    .read = iwl_mvm_fm_debug_notify_read,
    .open = simple_open,
    .llseek = generic_file_llseek,
};

static int iwl_mvm_fm_create_debugfs(void) {
    struct dentry* entry;

    fm_debug_dir = debugfs_create_dir("frq_mgr", NULL);

    if (!fm_debug_dir) { goto err; }

    entry = debugfs_create_file("mitigate", S_IWUSR, fm_debug_dir, NULL, &fm_debug_mitigate_ops);
    if (!entry) { goto err; }

    entry = debugfs_create_file("notify", S_IRUSR, fm_debug_dir, NULL, &fm_debug_notify_ops);
    if (!entry) { goto err; }

    return 0;
err:
    pr_info("FM: Could not create debugfs entries\n");
    debugfs_remove_recursive(fm_debug_dir);
    return -1;
}

int32_t iwl_mvm_fm_test_register_callback(const enum iui_fm_macro_id macro_id,
                                          const iui_fm_mitigation_cb mitigation_cb) {
    int ret = 0;

    fm_callback = mitigation_cb;

    /* Unregister fm callback */
    if (!mitigation_cb) {
        debugfs_remove_recursive(fm_debug_dir);
        goto end;
    }

    /* Register fm callback */
    if (iwl_mvm_fm_create_debugfs()) {
        ret = -1;
        goto end;
    }

end:
    pr_info("FM[test-mode]: %sregistering fm callback function (fail = %d)\n", ret ? "un" : "",
            ret);
    return ret;
}

int32_t iwl_mvm_fm_test_notify_frequency(
    const enum iui_fm_macro_id macro_id,
    const struct iui_fm_freq_notification* const notification) {
    /* Platform does not have a FM or test mode was requested */
    memcpy(&fm_notif, notification->info.wlan_info, sizeof(struct iui_fm_wlan_info));

    pr_info("FM[test-mode]: notifying fm about change (mask = 0x%x)\n",
            notification->info.wlan_info->bitmask);

    return 0;
}
