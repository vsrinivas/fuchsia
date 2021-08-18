// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod fields;
pub mod flash;

use crate::{error::NvdataError, nvdata::flash::Flash};
use anyhow::{Context, Error};
use fidl_fuchsia_hardware_nvram as nvram;
use fidl_fuchsia_vboot_fwparam::{FirmwareParamRequest, FirmwareParamRequestStream, Key};
use fuchsia_inspect::{self as inspect, Property};
use fuchsia_syslog::fx_log_err;
use fuchsia_zircon as zx;
use futures::{lock::Mutex, TryStreamExt};
use std::convert::TryInto;

#[derive(Copy, Clone, Eq, PartialEq, Debug)]
pub enum NvdataVersion {
    /// Version 1 nvdata, 16 bytes of data.
    V1,
    /// Version 2 nvdata, 64 bytes of data.
    V2,
}

impl NvdataVersion {
    /// If |version| is valid, returns the NvdataVersion that represents it.
    /// Otherwise returns Err(version).
    pub fn from_raw(version: usize) -> Result<NvdataVersion, usize> {
        match version {
            1 => Ok(NvdataVersion::V1),
            2 => Ok(NvdataVersion::V2),
            v => Err(v),
        }
    }
}

/// Represents nvram configuration.
/// See 2nvstorage_fields.h in vboot_reference for the source of truth for this implementation.
#[allow(dead_code)] // Will be removed when set() is supported.
pub struct Nvdata {
    base: u32,
    size: u32,
    version: NvdataVersionInfo,
    inner: Mutex<NvdataInner>,
    device: nvram::DeviceProxy,
    flash: Flash,
    inspect: inspect::Node,
}

/// Mutable state for |Nvdata|.
struct NvdataInner {
    data: Vec<u8>,
    inspect_state: Vec<Option<inspect::UintProperty>>,
    version: NvdataVersionInfo,
    modified: bool,
    header: fields::Header,
    boot: fields::Boot,
    boot2: fields::Boot2,
    dev: fields::Dev,
    tpm: fields::Tpm,
    misc: fields::Misc,
}

#[derive(Copy, Clone, Debug, PartialEq, Eq)]
enum NvdataOffset {
    /// See |nvram::Header|.
    Header = 0,
    /// See |nvram::Boot|.
    Boot = 1,
    /// Recovery reason. See 2recovery_reasons.h in vboot_reference.
    /// fuchsia.vboot.fwparam/Key.RECOVERY_REQUEST.
    Recovery = 2,
    /// Locale ID, used for localisation of firmware screens.
    /// fuchsia.vboot.fwparam/Key.LOCALIZATION_INDEX.
    Localization = 3,
    /// See |nvram::Dev|.
    Dev = 4,
    /// See |nvram::Tpm|.
    Tpm = 5,
    /// Recovery reason subcode.
    /// fuchsia.vboot.fwparam/Key.RECOVERY_SUBCODE.
    RecoverySubcode = 6,
    /// See |nvram::Boot2|.
    Boot2 = 7,
    /// See |nvram::Misc|.
    Misc = 8,
    /// Bits 0-7 of KernelMaxRollForward.
    /// Used to restrict how far forward rollback prevention will go.
    /// fuchsia.vboot.fwparam/Key.KERNEL_MAX_ROLLFORWARD.
    KernelMaxRollForward1 = 9,
    /// Bits 8-15 of KernelMaxRollForward.
    KernelMaxRollForward2 = 10,
    /// Bits 0-7 of Kernel.
    /// fuchsia.vboot.fwparam/Key.KERNEL_FIELD.
    Kernel1 = 11,
    /// Bits 8-15 of Kernel.
    Kernel2 = 12,
    /// Bits 16-23 of KernelMaxRollForward.
    KernelMaxRollForward3 = 13,
    /// Bits 24-31 of KernelMaxRollForward.
    KernelMaxRollForward4 = 14,
    /// CRC8 for nvdata V1. 0xff in V2.
    CrcV1 = 15,

    // Everything past this point is only present in V2.
    /// Bits 0-7 of FirmwareMaxRollForward.
    /// fuchsia.vboot.fwparam/Key.FW_MAX_ROLLFORWARD.
    FirmwareMaxRollForward1 = 16,
    /// Bits 8-15 of FirmwareMaxRollForward.
    FirmwareMaxRollForward2 = 17,
    /// Bits 16-23 of FirmwareMaxRollForward.
    FirmwareMaxRollForward3 = 18,
    /// Bits 24-31 of FirmwareMaxRollForward.
    FirmwareMaxRollForward4 = 19,
    /// CRC8 for nvdata V2.
    CrcV2 = 63,
}

impl NvdataOffset {
    fn val(&self) -> usize {
        *self as usize
    }
}

/// Information about a particular nvdata version.
#[derive(Clone, Copy, Debug)]
struct NvdataVersionInfo {
    /// Version number.
    version: NvdataVersion,
    /// Number of bytes nvdata occupies.
    size_needed: usize,
    /// Offset of the CRC byte.
    crc_offset: usize,
}

impl Nvdata {
    /// Initialise a nvdata instance, verifying that its contents are valid.
    pub async fn new(
        base: u32,
        size: u32,
        device: nvram::DeviceProxy,
        flash: Flash,
        inspect: inspect::Node,
    ) -> Result<Self, NvdataError> {
        let fidl_result = device
            .read(base, size)
            .await
            .context("Sending read request to nvram")
            .map_err(NvdataError::Other)?;

        let data = fidl_result
            .map_err(zx::Status::from_raw)
            .context("Reading data from nvram")
            .map_err(NvdataError::Other)?;

        let version = Self::get_version(fields::Header(data[NvdataOffset::Header.val()]))
            .ok_or(NvdataError::InvalidSignature)?;
        // Make sure we have enough data.
        if (size as usize) < version.size_needed {
            return Err(NvdataError::NotEnoughSpace(version.size_needed));
        }
        let inner = NvdataInner::new(data, version);
        let nvdata =
            Nvdata { base, size, version, inner: Mutex::new(inner), device, flash, inspect };
        if let Err(e) = nvdata.verify_crc().await {
            // TODO(fxbug.dev/82832): wipe nvram when this happens.
            fx_log_err!("Nvdata had invalid CRC. https://fxbug.dev/82832");
            return Err(e);
        }
        nvdata.fill_inspect().await;

        Ok(nvdata)
    }

    /// Check that the header signature matches a known version.
    /// If it does, returns information about that version.
    /// If it doesn't, returns none.
    fn get_version(header: fields::Header) -> Option<NvdataVersionInfo> {
        let signature = header.sig_low() | (header.sig_high() << 6);
        if signature == 0x40 {
            Some(NvdataVersionInfo {
                version: NvdataVersion::V1,
                size_needed: 16,
                crc_offset: NvdataOffset::CrcV1.val(),
            })
        } else if signature == 0x3 {
            Some(NvdataVersionInfo {
                version: NvdataVersion::V2,
                size_needed: 64,
                crc_offset: NvdataOffset::CrcV2.val(),
            })
        } else {
            None
        }
    }

    async fn verify_crc(&self) -> Result<(), NvdataError> {
        self.inner.lock().await.verify_crc()
    }

    pub async fn serve(&self, mut stream: FirmwareParamRequestStream) -> Result<(), Error> {
        while let Some(request) = stream.try_next().await.context("Reading request")? {
            match request {
                FirmwareParamRequest::Get { key, responder } => {
                    responder
                        .send(&mut self.get(key).await.map_err(|e| match e {
                            NvdataError::UnknownKey => zx::Status::NOT_SUPPORTED.into_raw(),
                            _ => zx::Status::INTERNAL.into_raw(),
                        }))
                        .context("Responding to get()")?;
                }
                FirmwareParamRequest::Set { key, value, responder } => {
                    responder
                        .send(&mut self.set(key, value).await.map_err(|e| match e {
                            NvdataError::UnknownKey => zx::Status::NOT_SUPPORTED.into_raw(),
                            NvdataError::NotWritable => zx::Status::IO_REFUSED.into_raw(),
                            NvdataError::ExpectedBool => zx::Status::INVALID_ARGS.into_raw(),
                            NvdataError::ClearOnly => zx::Status::INVALID_ARGS.into_raw(),
                            _ => zx::Status::INTERNAL.into_raw(),
                        }))
                        .context("Responding to set()")?;
                }
            }
        }
        Ok(())
    }

    async fn fill_inspect(&self) {
        // Record state that might change.
        let mut inner = self.inner.lock().await;
        let mut i = 0;
        while let Some(key) = Key::from_primitive(i) {
            match inner.get(key) {
                Ok(value) => inner
                    .inspect_state
                    .push(Some(self.inspect.create_uint(format!("{:?}", key), value as u64))),
                Err(_) => inner.inspect_state.push(None),
            }

            i += 1;
        }

        // Record inspect state that doesn't change.
        self.inspect.record_uint("nvdata-version", self.version.version as u64);
    }

    async fn get(&self, key: Key) -> Result<u32, NvdataError> {
        self.inner.lock().await.get(key)
    }

    async fn set(&self, key: Key, value: u32) -> Result<(), NvdataError> {
        let mut inner = self.inner.lock().await;
        inner.set(key, value)?;
        if !inner.modified {
            return Ok(());
        }

        // Write to the various storage devices.
        // In the future, we could try and coalesce writes to reduce flash wear.
        self.device
            .write(self.base, inner.data.as_slice())
            .await
            .context("sending fidl write")
            .map_err(NvdataError::Other)?
            .map_err(zx::Status::from_raw)
            .context("doing write")
            .map_err(NvdataError::Other)?;

        self.flash.save(inner.data.as_slice()).await.context("Writing to flash")?;
        inner.modified = false;

        Ok(())
    }
}

impl NvdataInner {
    pub fn new(data: Vec<u8>, version: NvdataVersionInfo) -> Self {
        let header = fields::Header(data[NvdataOffset::Header.val()]);
        let boot = fields::Boot(data[NvdataOffset::Boot.val()]);
        let boot2 = fields::Boot2(data[NvdataOffset::Boot2.val()]);
        let tpm = fields::Tpm(data[NvdataOffset::Tpm.val()]);
        let dev = fields::Dev(data[NvdataOffset::Dev.val()]);
        let misc = fields::Misc(data[NvdataOffset::Misc.val()]);

        NvdataInner {
            data,
            inspect_state: Vec::new(),
            version,
            modified: false,
            header,
            boot,
            boot2,
            tpm,
            dev,
            misc,
        }
    }

    /// Verify that the contents of the nvdata match the CRC.
    pub fn verify_crc(&self) -> Result<(), NvdataError> {
        let expected = self.data[self.version.crc_offset];

        let actual = Self::calculate_crc(&self.data[0..self.version.crc_offset]);

        if expected != actual {
            Err(NvdataError::CrcMismatch)
        } else {
            Ok(())
        }
    }

    /// CRC-8 ITU version with x^8 + x^2 + x + 1 as a polynomial.
    /// Matches the vboot_reference implementation.
    fn calculate_crc(data: &[u8]) -> u8 {
        let mut crc: usize = 0;
        for value in data.iter() {
            crc ^= (*value as usize) << 8;
            for _ in 0..8 {
                if crc & 0x8000 != 0 {
                    crc ^= 0x1070 << 3;
                }
                crc <<= 1;
            }
        }

        ((crc >> 8) & 0xff).try_into().unwrap()
    }

    pub fn get(&self, key: Key) -> Result<u32, NvdataError> {
        let data = &self.data;
        let value: u32 = match key {
            Key::FirmwareSettingsReset => self.header.firmware_settings_reset() as u32,
            Key::KernelSettingsReset => self.header.kernel_settings_reset() as u32,
            Key::DebugResetMode => self.boot.debug_reset() as u32,
            Key::TryNext => self.boot2.try_next() as u32,
            Key::TryCount => self.boot.try_count() as u32,
            Key::RecoveryRequest => data[NvdataOffset::Recovery.val()] as u32,
            Key::LocalizationIndex => data[NvdataOffset::Localization.val()] as u32,
            Key::KernelField => {
                (data[NvdataOffset::Kernel1.val()] as u32)
                    | ((data[NvdataOffset::Kernel2.val()] as u32) << 8)
            }
            Key::DevBootExternal => self.dev.allow_external() as u32,
            Key::DevBootAltfw => self.dev.allow_altfw() as u32,
            Key::DevBootSignedOnly => self.dev.allow_signed_only() as u32,
            Key::DevDefaultBoot => self.dev.default_boot_source() as u32,
            Key::DevEnableUdc => self.dev.enable_udc() as u32,
            Key::DisableDevRequest => self.boot.disable_dev_mode() as u32,
            Key::DisplayRequest => self.boot.display_request() as u32,
            Key::ClearTpmOwnerRequest => self.tpm.clear_owner_request() as u32,
            Key::ClearTpmOwnerDone => self.tpm.clear_owner_done() as u32,
            Key::TpmRequestedReboot => self.tpm.tpm_rebooted() as u32,
            Key::RecoverySubcode => data[NvdataOffset::RecoverySubcode.val()] as u32,
            Key::BackupNvramRequest => self.boot.backup_nvram() as u32,
            Key::FwTried => self.boot2.fw_tried() as u32,
            Key::FwResult => self.boot2.fw_result() as u32,
            Key::FwPrevTried => self.boot2.prev_tried() as u32,
            Key::FwPrevResult => self.boot2.prev_result() as u32,
            Key::ReqWipeout => self.header.wipeout() as u32,
            Key::BootOnAcDetect => self.misc.boot_on_ac() as u32,
            Key::TryRoSync => self.misc.try_ro_sync() as u32,
            Key::BatteryCutoffRequest => self.misc.battery_cutoff() as u32,
            Key::KernelMaxRollforward => {
                (data[NvdataOffset::KernelMaxRollForward1.val()] as u32)
                    | ((data[NvdataOffset::KernelMaxRollForward2.val()] as u32) << 8)
                    | ((data[NvdataOffset::KernelMaxRollForward3.val()] as u32) << 16)
                    | ((data[NvdataOffset::KernelMaxRollForward4.val()] as u32) << 24)
            }
            Key::FwMaxRollforward => {
                if self.version.version == NvdataVersion::V1 {
                    // On v1 where controlling roll forward isn't supported, default to rolling
                    // forward as much as possible.
                    u32::MAX - 1
                } else {
                    (data[NvdataOffset::FirmwareMaxRollForward1.val()] as u32)
                        | ((data[NvdataOffset::FirmwareMaxRollForward2.val()] as u32) << 8)
                        | ((data[NvdataOffset::FirmwareMaxRollForward3.val()] as u32) << 16)
                        | ((data[NvdataOffset::FirmwareMaxRollForward4.val()] as u32) << 24)
                }
            }
            Key::PostEcSyncDelay => self.misc.post_ec_sync_delay() as u32,
            Key::DiagRequest => self.boot2.req_diag() as u32,
            Key::MiniosPriority => self.misc.minios_priority() as u32,
            _ => return Err(NvdataError::UnknownKey),
        };

        Ok(value)
    }

    pub fn set(&mut self, key: Key, value: u32) -> Result<(), NvdataError> {
        let as_bool = |value: u32| match value {
            1 => Ok(true),
            0 => Ok(false),
            _ => Err(NvdataError::ExpectedBool),
        };
        let as_bool_clear_only = |value: u32| match value {
            0 => Ok(false),
            1 => Err(NvdataError::ClearOnly),
            _ => Err(NvdataError::ExpectedBool),
        };
        // First - avoid doing anything if we don't have to.
        if self.get(key)? == value {
            return Ok(());
        }
        let data = &mut self.data;
        match key {
            Key::FirmwareSettingsReset => return Err(NvdataError::NotWritable),
            Key::KernelSettingsReset => {
                self.header.set_kernel_settings_reset(as_bool_clear_only(value)?)
            }
            Key::DebugResetMode => self.boot.set_debug_reset(as_bool(value)?),
            Key::TryNext => self.boot2.set_try_next(as_bool(value)?),
            Key::TryCount => {
                self.boot.set_try_count(value.try_into().map_err(NvdataError::InvalidValue)?)
            }
            Key::RecoveryRequest => {
                data[NvdataOffset::Recovery.val()] =
                    value.try_into().map_err(NvdataError::InvalidValue)?;
            }
            Key::LocalizationIndex => {
                data[NvdataOffset::Localization.val()] =
                    value.try_into().map_err(NvdataError::InvalidValue)?;
            }
            Key::KernelField => {
                let u16_val: u16 = value.try_into().map_err(NvdataError::InvalidValue)?;
                data[NvdataOffset::Kernel1.val()] = (u16_val & 0xff) as u8;
                data[NvdataOffset::Kernel2.val()] = (u16_val >> 8) as u8;
            }
            Key::DevBootExternal => self.dev.set_allow_external(as_bool(value)?),
            Key::DevBootAltfw => self.dev.set_allow_altfw(as_bool(value)?),
            Key::DevBootSignedOnly => self.dev.set_allow_signed_only(as_bool(value)?),
            Key::DevDefaultBoot => self
                .dev
                .set_default_boot_source(value.try_into().map_err(NvdataError::InvalidValue)?),
            Key::DevEnableUdc => self.dev.set_enable_udc(as_bool(value)?),
            Key::DisableDevRequest => self.boot.set_disable_dev_mode(as_bool(value)?),
            Key::DisplayRequest => self.boot.set_display_request(as_bool(value)?),
            Key::ClearTpmOwnerRequest => self.tpm.set_clear_owner_request(as_bool(value)?),
            Key::ClearTpmOwnerDone => self.tpm.set_clear_owner_done(as_bool_clear_only(value)?),
            Key::TpmRequestedReboot => return Err(NvdataError::NotWritable),
            Key::RecoverySubcode => {
                data[NvdataOffset::RecoverySubcode.val()] =
                    value.try_into().map_err(NvdataError::InvalidValue)?
            }
            Key::BackupNvramRequest => self.boot.set_backup_nvram(as_bool(value)?),
            Key::FwTried => return Err(NvdataError::NotWritable),
            Key::FwResult => return Err(NvdataError::NotWritable),
            Key::FwPrevTried => return Err(NvdataError::NotWritable),
            Key::FwPrevResult => return Err(NvdataError::NotWritable),
            Key::ReqWipeout => self.header.set_wipeout(as_bool_clear_only(value)?),
            Key::BootOnAcDetect => self.misc.set_boot_on_ac(as_bool(value)?),
            Key::TryRoSync => self.misc.set_try_ro_sync(as_bool(value)?),
            Key::BatteryCutoffRequest => self.misc.set_battery_cutoff(as_bool(value)?),
            Key::KernelMaxRollforward => {
                data[NvdataOffset::KernelMaxRollForward1.val()] = (value & 0xff) as u8;
                data[NvdataOffset::KernelMaxRollForward2.val()] = ((value >> 8) & 0xff) as u8;
                data[NvdataOffset::KernelMaxRollForward3.val()] = ((value >> 16) & 0xff) as u8;
                data[NvdataOffset::KernelMaxRollForward4.val()] = ((value >> 24) & 0xff) as u8;
            }
            Key::FwMaxRollforward => {
                if self.version.version == NvdataVersion::V1 {
                    return Err(NvdataError::NotWritable);
                } else {
                    data[NvdataOffset::FirmwareMaxRollForward1.val()] = (value & 0xff) as u8;
                    data[NvdataOffset::FirmwareMaxRollForward2.val()] = ((value >> 8) & 0xff) as u8;
                    data[NvdataOffset::FirmwareMaxRollForward3.val()] =
                        ((value >> 16) & 0xff) as u8;
                    data[NvdataOffset::FirmwareMaxRollForward4.val()] =
                        ((value >> 24) & 0xff) as u8;
                }
            }
            Key::PostEcSyncDelay => self.misc.set_post_ec_sync_delay(as_bool(value)?),
            Key::DiagRequest => self.boot2.set_req_diag(as_bool(value)?),
            Key::MiniosPriority => self.misc.set_minios_priority(as_bool(value)?),
            _ => return Err(NvdataError::UnknownKey),
        };

        if let Some(prop) = self.inspect_state[key.into_primitive() as usize].as_ref() {
            prop.set(value.into());
        }

        data[NvdataOffset::Header.val()] = self.header.0;
        data[NvdataOffset::Boot.val()] = self.boot.0;
        data[NvdataOffset::Boot2.val()] = self.boot2.0;
        data[NvdataOffset::Tpm.val()] = self.tpm.0;
        data[NvdataOffset::Dev.val()] = self.dev.0;
        data[NvdataOffset::Misc.val()] = self.misc.0;

        let new_crc = Self::calculate_crc(&self.data[0..self.version.crc_offset]);
        // Update the CRC.
        self.data[self.version.crc_offset] = new_crc;
        self.modified = true;

        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl_fuchsia_hardware_nvram::{DeviceMarker, DeviceProxy, DeviceRequest};
    use flash::tests::FakeFlash;
    use std::sync::Arc;

    struct FakeNvram {
        contents: Mutex<Vec<u8>>,
    }

    impl FakeNvram {
        pub fn new(data: Vec<u8>) -> Arc<Self> {
            Arc::new(FakeNvram { contents: Mutex::new(data) })
        }

        pub fn start(self: Arc<Self>) -> DeviceProxy {
            let (proxy, mut stream) = fidl::endpoints::create_proxy_and_stream::<DeviceMarker>()
                .expect("create proxy and stream ok");

            fuchsia_async::Task::spawn(async move {
                while let Some(request) = stream.try_next().await.expect("Getting item") {
                    match request {
                        DeviceRequest::GetSize { responder } => {
                            responder
                                .send(self.contents.lock().await.len().try_into().unwrap())
                                .expect("GetSize reply ok");
                        }
                        DeviceRequest::Read { offset, size, responder } => {
                            responder
                                .send(&mut Ok(self.contents.lock().await
                                    [(offset as usize)..((offset + size) as usize)]
                                    .to_vec()))
                                .expect("Read reply ok");
                        }
                        DeviceRequest::Write { offset, data, responder } => {
                            let offset: usize = offset as usize;
                            self.contents.lock().await[offset..offset + data.len()]
                                .copy_from_slice(data.as_slice());
                            responder.send(&mut Ok(())).expect("Write reply ok");
                        }
                    }
                }
            })
            .detach();

            proxy
        }
    }

    #[fuchsia::test]
    async fn test_empty_nvram() {
        let data = vec![0; 32];
        let nvram = FakeNvram::new(data).start();

        match Nvdata::new(0, 16, nvram, FakeFlash::empty().await, Default::default()).await {
            Ok(_) => unreachable!(),
            Err(NvdataError::InvalidSignature) => {}
            Err(e) => panic!("Unexpected error {:?}", e),
        }
    }

    #[fuchsia::test]
    async fn test_nvram_v2_too_small() {
        let mut data = vec![0; 64];
        data[0] = 0x3;
        let nvram = FakeNvram::new(data).start();

        match Nvdata::new(0, 16, nvram, FakeFlash::empty().await, Default::default()).await {
            Ok(_) => unreachable!(),
            Err(NvdataError::NotEnoughSpace(64)) => {}
            Err(e) => panic!("Unexpected error {:?}", e),
        }
    }

    #[fuchsia::test]
    async fn test_nvram_v1_too_small() {
        let mut data = vec![0; 64];
        data[0] = 0x40;
        let nvram = FakeNvram::new(data).start();

        match Nvdata::new(0, 12, nvram, FakeFlash::empty().await, Default::default()).await {
            Ok(_) => unreachable!(),
            Err(NvdataError::NotEnoughSpace(16)) => {}
            Err(e) => panic!("Unexpected error {:?}", e),
        }
    }

    #[fuchsia::test]
    async fn test_nvram_bad_crc() {
        let mut data = vec![0; 64];
        data[0] = 0x40;
        let nvram = FakeNvram::new(data).start();

        match Nvdata::new(0, 16, nvram, FakeFlash::empty().await, Default::default()).await {
            Ok(_) => unreachable!(),
            Err(NvdataError::CrcMismatch) => {}
            Err(e) => panic!("Unexpected error {:?}", e),
        }
    }

    #[fuchsia::test]
    async fn test_nvram_all_zero() {
        let mut data = vec![0; 64];
        data[0] = 0x03;
        data[63] = NvdataInner::calculate_crc(&data[0..63]);
        let nvram = FakeNvram::new(data).start();

        let nvdata = Nvdata::new(0, 64, nvram, FakeFlash::empty().await, Default::default())
            .await
            .expect("nvdata valid");
        let mut i = 0;
        while let Some(key) = Key::from_primitive(i) {
            let value = nvdata.get(key).await.unwrap();
            assert_eq!(value, 0);
            i += 1;
        }
    }

    #[fuchsia::test]
    async fn test_nvram_v1_fw_max_rollforward() {
        let mut data = vec![0; 16];
        data[0] = 0x40;
        data[15] = NvdataInner::calculate_crc(&data[0..15]);
        let nvram = FakeNvram::new(data).start();

        let nvdata = Nvdata::new(0, 16, nvram, FakeFlash::empty().await, Default::default())
            .await
            .expect("nvdata valid");
        assert_eq!(nvdata.get(Key::FwMaxRollforward).await.unwrap(), u32::MAX - 1);
    }

    #[fuchsia::test]
    async fn test_nvram_set() {
        let mut data = vec![0; 16];
        data[0] = 0x40;
        data[15] = NvdataInner::calculate_crc(&data[0..15]);
        let nvram = FakeNvram::new(data);
        let fake_flash = FakeFlash::new(16);

        let nvdata = Nvdata::new(
            0,
            16,
            Arc::clone(&nvram).start(),
            Arc::clone(&fake_flash).get_flash().await,
            Default::default(),
        )
        .await
        .expect("nvdata valid");
        nvdata.set(Key::DevBootSignedOnly, 1).await.expect("Set OK");

        let contents = nvram.contents.lock().await;
        // Make sure nvram was updated.
        assert!(fields::Dev(contents[NvdataOffset::Dev.val()]).allow_signed_only());
        // Make sure flash was saved.
        assert_eq!(contents.as_slice(), &fake_flash.data.lock().await[16..32]);
    }
}
