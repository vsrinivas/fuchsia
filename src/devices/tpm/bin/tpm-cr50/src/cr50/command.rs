// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Structures used for communicating with the TPM.
//! The convention is that at rest, all values are stored little-endian.
//! Values are converted to/from big-endian when they're transmitted/received.

pub mod ccd;

use super::status::{ExecuteError, TpmStatus};
use crate::util::{DeserializeError, Deserializer, Serializer};
use anyhow::Context;
use async_trait::async_trait;
use fidl_fuchsia_tpm::TpmDeviceProxy;
use num_derive::FromPrimitive;
use num_traits::FromPrimitive as _;

/// Trait for a TPM request which has a companion response type.
pub trait TpmRequest {
    type ResponseType: Deserializable;
}

#[async_trait]
/// A TPM command that can be executed.
pub trait TpmCommand: Sync + Send {
    type ResponseType;
    async fn execute(self, tpm: &TpmDeviceProxy) -> Result<Self::ResponseType, ExecuteError>;
}

#[async_trait]
impl<T: TpmRequest + Serializable + Sync + Send> TpmCommand for T {
    type ResponseType = <T as TpmRequest>::ResponseType;
    async fn execute(self, tpm: &TpmDeviceProxy) -> Result<Self::ResponseType, ExecuteError> {
        let mut serializer = Serializer::new();
        self.serialize(&mut serializer);

        let (rc, data): (u16, Vec<u8>) = tpm
            .execute_vendor_command(0, &serializer.into_vec())
            .await
            .context("Sending execute request")?
            .map_err(fuchsia_zircon::Status::from_raw)
            .context("Executing TPM command")?;

        let status = TpmStatus::from(rc);
        if !status.is_ok() {
            return Err(ExecuteError::Tpm(status));
        }

        let mut deserializer = Deserializer::new(data);
        Ok(Self::ResponseType::deserialize(&mut deserializer).context("Deserialising response")?)
    }
}

/// Trait used for serialising TPM commands to byte arrays.
pub trait Serializable {
    fn serialize(&self, serializer: &mut Serializer);
}

/// Trait used for deserialising TPM commands from byte arrays.
pub trait Deserializable: Sized {
    fn deserialize(deserializer: &mut Deserializer) -> Result<Self, DeserializeError>;
}

#[repr(u16)]
#[derive(FromPrimitive, Debug, Copy, Clone, PartialEq, Eq)]
/// Vendor commands used by the TPM. These are stored in a u16 after the standard TPM header.
enum Subcommand {
    /* Original extension commands */
    ExtensionAes = 0,
    ExtensionHash = 1,
    ExtensionRsa = 2,
    ExtensionEcc = 3,
    ExtensionFwUpgrade = 4,
    ExtensionHkdf = 5,
    ExtensionEcies = 6,
    ExtensionPostReset = 7,

    LastExtensionCommand = 15,
    /* Our TPMv2 vendor-specific command codes. 16 bits available. */
    VendorCcGetLock = 16,
    SetLock = 17,
    Sysinfo = 18,
    /*
     * VENDOR_CC_IMMEDIATE_RESET may have an argument, which is a (uint16_t)
     * time delay (in milliseconds) in doing a reset. Max value is 1000.
     * The command may also be called without an argument, which will be
     * regarded as zero time delay.
     */
    ImmediateReset = 19,
    InvalidateInactiveRw = 20,
    CommitNvmem = 21,
    /* DEPRECATED(22): deep sleep control command. */
    ReportTpmState = 23,
    TurnUpdateOn = 24,
    GetBoardId = 25,
    SetBoardId = 26,
    U2fApdu = 27,
    PopLogEntry = 28,
    GetRecBtn = 29,
    RmaChallengeResponse = 30,
    /* DEPRECATED(31): CCD password command (now part of VENDOR_CC_CCD) */
    /*
     * Disable factory mode. Reset all ccd capabilities to default and reset
     * write protect to follow battery presence.
     */
    DisableFactory = 32,
    /* DEPRECATED(33): Manage CCD password phase */
    Ccd = 34,
    GetAlertsData = 35,
    SpiHash = 36,
    Pinweaver = 37,
    /*
     * Check the factory reset settings. If they're all set correctly, do a
     * factory reset to enable ccd factory mode. All capabilities will be
     * set to Always and write protect will be permanently disabled. This
     * mode can't be reset unless VENDOR_CC_DISABLE_FACTORY is called or
     * the 'ccd reset' console command is run.
     */
    ResetFactory = 38,
    /*
     * Get the write protect setting. This will return a single byte with
     * bits communicating the write protect setting as described by the
     * WPV subcommands.
     */
    Wp = 39,
    /*
     * Either enable or disable TPM mode. This is allowed for one-time only
     * until next TPM reset EVENT. In other words, once TPM mode is set,
     * then it cannot be altered to the other mode value. The allowed input
     * values are either TPM_MODE_ENABLED or TPM_MODE_DISABLED as defined
     * in 'enum tpm_modes', tpm_registers.h.
     * If the input size is zero, it won't change TPM_MODE.
     * If either the input size is zero or the input value is valid,
     * it will respond with the current tpm_mode value in uint8_t format.
     *
     *  Return code:
     *   VENDOR_RC_SUCCESS: completed successfully.
     *   VENDOR_RC_INTERNAL_ERROR: failed for an internal reason.
     *   VENDOR_RC_NOT_ALLOWED: failed in changing TPM_MODE,
     *                          since it is already set.
     *   VENDOR_RC_NO_SUCH_SUBCOMMAND: failed because the given input
     *                                 is undefined.
     */
    TpmMode = 40,
    /*
     * Initializes INFO1 SN data space, and sets SN hash. Takes three
     * int32 as parameters, which are written as the SN hash.
     */
    SnSetHash = 41,
    /*
     * Increments the RMA count in the INFO1 SN data space. The space must
     * have been previously initialized with the _SET_HASH command above for
     * this to succeed. Takes one byte as parameter, which indicates the
     * number to increment the RMA count by; this is typically 1 or 0.
     *
     * Incrementing the RMA count by 0 will set the RMA indicator, but not
     * incremement the count. This is useful to mark that a device has been
     * RMA'd, but that we were not able to log the new serial number.
     *
     * Incrementing the count by the maximum RMA count (currently 7) will
     * always set the RMA count to the maximum value, regardless of the
     * previous value. This can be used with any device, regardless of
     * current state, to mark it as RMA'd but with an unknown RMA count.
     */
    SnIncRma = 42,
    /*
     * Gets the latched state of a power button press to indicate user
     * recent user presence. The power button state is automatically cleared
     * after PRESENCE_TIMEOUT.
     */
    GetPwrBtn = 43,
    /*
     * U2F commands.
     */
    U2fGenerate = 44,
    U2fSign = 45,
    U2fAttest = 46,
    FlogTimestamp = 47,
    EndorsementSeed = 48,
    U2fMode = 49,
    /*
     * HMAC-SHA256 DRBG invocation for ACVP tests
     */
    DrbgTest = 50,
    TrngTest = 51,
    /* EC EFS(Early Firmware Selection) commands */
    GetBootMode = 52,
    ResetEc = 53,
    SeedApRoCheck = 54,
    FipsCmd = 55,
    GetApRoHash = 56,
    GetApRoStatus = 57,
    LastVendorCommand = 65535,
}

#[derive(Debug, Copy, Clone, PartialEq, Eq)]
/// Command header used by all vendor commands.
struct Header {
    subcommand: Subcommand,
}

impl Header {
    fn new(subcommand: Subcommand) -> Self {
        Self { subcommand }
    }
}

impl Serializable for Header {
    fn serialize(&self, serializer: &mut Serializer) {
        serializer.put_be_u16(self.subcommand as u16)
    }
}

impl Deserializable for Header {
    fn deserialize(deserializer: &mut Deserializer) -> Result<Self, DeserializeError> {
        Ok(Header {
            subcommand: Subcommand::from_u16(deserializer.take_be_u16()?)
                .ok_or(DeserializeError::InvalidValue)?,
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::cr50::command::ccd::{CcdCommand, CcdRequest};
    use fidl_fuchsia_tpm::{TpmDeviceMarker, TpmDeviceRequest};
    use fuchsia_async::Task;
    use futures::TryStreamExt;
    use std::sync::Arc;

    struct FakeTpm {
        response: Vec<u8>,
        received: Vec<u8>,
    }

    impl FakeTpm {
        pub fn new(response: Vec<u8>, received: Vec<u8>) -> Arc<Self> {
            Arc::new(FakeTpm { response, received })
        }

        pub fn serve(self: Arc<Self>) -> TpmDeviceProxy {
            let (proxy, mut stream) =
                fidl::endpoints::create_proxy_and_stream::<TpmDeviceMarker>().unwrap();

            Task::spawn(async move {
                while let Some(req) = stream.try_next().await.unwrap() {
                    match req {
                        TpmDeviceRequest::ExecuteVendorCommand {
                            command_code,
                            data,
                            responder,
                        } => {
                            assert_eq!(command_code, 0);
                            assert_eq!(data, self.received);
                            responder.send(&mut Ok((0, self.response.clone()))).expect("Reply ok");
                        }
                        _ => unreachable!(),
                    }
                }
            })
            .detach();

            proxy
        }
    }

    #[derive(Debug, PartialEq)]
    struct DontCareResponse;
    impl Deserializable for DontCareResponse {
        fn deserialize(deserializer: &mut Deserializer) -> Result<Self, DeserializeError> {
            assert_eq!(deserializer.take_be_u16()?, 0x22);
            Ok(DontCareResponse)
        }
    }

    #[fuchsia::test]
    async fn test_execute() {
        let request = CcdRequest::<DontCareResponse>::new(CcdCommand::Open);
        let expected_req = vec![0x00, 0x22, 0x01]; /* Subcommand CCD, CcdCommand Open */
        let reply = vec![0x00, 0x22];

        let tpm = FakeTpm::new(reply, expected_req);
        let proxy = tpm.serve();
        assert_eq!(request.execute(&proxy).await.unwrap(), DontCareResponse);
    }
}
