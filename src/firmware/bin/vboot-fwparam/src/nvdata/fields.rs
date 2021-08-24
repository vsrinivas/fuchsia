// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use bitfield::bitfield;

bitfield! {
    pub struct Header(u8);
    impl Debug;

    /// Low bits of signature.
    pub sig_low, _ : 1, 0;
    // bit 2 unused.
    /// Firmware requested factory reset. We can only clear this field.
    /// fuchsia.vboot/Key.REQ_WIPEOUT.
    pub wipeout, set_wipeout: 3;
    /// Set if nvram is cleared, should be cleared once kernel acknowledges it.
    /// fuchsia.vboot/Key.KERNEL_SETTINGS_RESET.
    pub kernel_settings_reset, set_kernel_settings_reset: 4;
    /// Set if nvram is cleared, only for use by firmware.
    /// fuchsia.vboot/Key.FIRMWARE_SETTINGS_RESET.
    pub firmware_settings_reset, set_firmware_settings_reset: 5;
    /// High bits of signature.
    pub sig_high, _: 7, 6;
}

bitfield! {
    pub struct Boot(u8);
    impl Debug;


    /// Number of tries left for the selected firmware slot.
    /// fuchsia.vboot/Key.TRY_COUNT
    pub try_count, set_try_count : 3, 0;
    /// Backup nvram request. No longer used.
    /// fuchsia.vboot/Key.BACKUP_NVRAM_REQUEST.
    pub backup_nvram, set_backup_nvram : 4;
    /// Request that the display be initialised at boot, for developer mode bootloader screens.
    /// fuchsia.vboot/Key.DISPLAY_REQUEST.
    pub display_request, set_display_request : 5;
    /// Request that firmware disable developer mode on the next boot.
    /// fuchsia.vboot/Key.DISABLE_DEV_REQUEST
    pub disable_dev_mode, set_disable_dev_mode : 6;
    /// Request debug mode on next S3->S0 transition.
    /// fuchsia.vboot/Key.DEBUG_RESET_MODE.
    pub debug_reset, set_debug_reset : 7;
}

bitfield! {
    pub struct Boot2(u8);
    impl Debug;


    /// Result of firmware this boot.
    /// fuchsia.vboot/Key.FW_RESULT.
    pub fw_result, _ : 1, 0;
    /// Firmware that was tried this boot.
    /// fuchsia.vboot/Key.FW_TRIED.
    pub fw_tried, _ : 2;
    /// Firmware to be tried next.
    /// fuchsia.vboot/Key.TRY_NEXT.
    pub try_next, set_try_next : 3;
    /// Result of previous firmware boot.
    /// fuchsia.vboot/Key.FW_PREV_RESULT.
    pub prev_result, _ : 5, 4;
    /// Previous boot firmware that was tried.
    /// fuchsia.vboot/Key.FW_PREV_TRIED.
    pub prev_tried, _ : 6;
    /// Request that diagnostic ROM be run on next boot.
    /// NVdata V2 only.
    /// fuchsia.vboot/Key.DIAG_REQUEST.
    pub req_diag, set_req_diag : 7;
}

bitfield! {
    pub struct Dev(u8);
    impl Debug;

    /// Allow booting from external media (e.g. USB)
    /// fuchsia.vboot/Key.DEV_BOOT_EXTERNAL.
    pub allow_external, set_allow_external : 0;
    /// Only boot properly-signed images in developer mode.
    /// fuchsia.vboot/Key.DEV_BOOT_SIGNED_ONLY.
    pub allow_signed_only, set_allow_signed_only : 1;
    /// Allow using altfw.
    /// fuchsia.vboot/Key.DEV_BOOT_ALTFW.
    pub allow_altfw, set_allow_altfw : 2;
    /// Deprecated.
    pub full_fastboot_deprecated, set_full_fastboot_deprecated : 3;
    /// Default boot source.
    /// fuchsia.vboot/Key.DEV_DEFAULT_BOOT.
    pub default_boot_source, set_default_boot_source : 5, 4;
    /// Enable USB device-mode on devices that support it.
    /// fuchsia.vboot/Key.DEV_ENABLE_UDC.
    pub enable_udc, set_enable_udc : 6;
    // bit 7 unused.
}

bitfield! {
    pub struct Tpm(u8);
    impl Debug;

    /// Request that firmware clear the TPM owner on the next boot.
    /// fuchsia.vboot/Key.CLEAR_TPM_OWNER_REQUEST.
    pub clear_owner_request, set_clear_owner_request : 0;
    /// Set after TPM owner is cleared.
    /// fuchsia.vboot/Key.CLEAR_TPM_OWNER_DONE.
    pub clear_owner_done, set_clear_owner_done : 1;
    /// TPM requesting repeated reboot.
    /// fuchsia.vboot/Key.TPM_REQUESTED_REBOOT.
    pub tpm_rebooted, set_tpm_rebooted : 2;
    // bits 7-3 unused.
}

bitfield! {
    pub struct Misc(u8);
    impl Debug;

    /// Deprecated.
    pub unlock_fastboot_deprecated, _ : 0;
    /// Boot the system when AC is plugged in.
    /// fuchsia.vboot/Key.BOOT_ON_AC_DETECT.
    pub boot_on_ac, set_boot_on_ac : 1;
    /// Try syncing the EC-RO image.
    /// fuchsia.vboot/Key.TRY_RO_SYNC.
    pub try_ro_sync, set_try_ro_sync : 2;
    /// Cut off battery and shutdown on next boot.
    /// fuchsia.vboot/Key.BATTERY_CUTOFF_REQUEST.
    pub battery_cutoff, set_battery_cutoff : 3;
    /// Priority of miniOS partition to load (v2 only).
    /// fuchsia.vboot/Key.MINIOS_PRIORITY.
    pub minios_priority, set_minios_priority : 4;
    // bit 5 unused.
    /// Add a short delay after EC software sync for interaction
    /// with EC-RW.
    /// fuchsia.vboot/Key.POST_EC_SYNC_DELAY.
    pub post_ec_sync_delay, set_post_ec_sync_delay : 6;
    // bit 7 unused.
}
