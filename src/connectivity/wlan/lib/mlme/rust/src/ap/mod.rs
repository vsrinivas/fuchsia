// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod context;
mod frame_writer;
mod infra_bss;
mod remote_client;

use {
    crate::{
        buffer::BufferProvider,
        device::Device,
        error::Error,
        key::KeyConfig,
        timer::{EventId, Scheduler, Timer},
    },
    fidl_fuchsia_wlan_mlme as fidl_mlme, fuchsia_zircon as zx,
    log::{error, info, log},
    std::fmt,
    wlan_common::{
        mac::{self, Bssid, MacAddr},
        TimeUnit,
    },
    zerocopy::ByteSlice,
};

use context::*;
use infra_bss::*;
use remote_client::*;

/// Rejection reasons for why a frame was not proceessed.
#[derive(Debug)]
pub enum Rejection {
    /// The frame was for another BSS.
    OtherBss,

    /// For data frames: The To DS bit was false, or the From DS bit was true.
    /// For management frames: The To DS bit was set and the frame was not a QMF (QoS Management
    /// frame) management frame, or the reserved From DS bit was set.
    BadDsBits,

    /// No source address was found.
    NoSrcAddr,

    /// No client with the given address was found.
    NoSuchClient(MacAddr),

    /// Some error specific to a client occurred.
    Client(MacAddr, ClientRejection),

    /// Some general error occurred.
    Error(failure::Error),
}

impl Rejection {
    fn log_level(&self) -> log::Level {
        match self {
            Self::NoSrcAddr => log::Level::Error,
            Self::Client(_, e) => e.log_level(),
            _ => log::Level::Trace,
        }
    }
}

impl fmt::Display for Rejection {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Client(addr, e) => write!(f, "client {:02X?}: {:?}", addr, e),
            _ => fmt::Debug::fmt(self, f),
        }
    }
}

impl From<failure::Error> for Rejection {
    fn from(e: failure::Error) -> Rejection {
        Self::Error(e)
    }
}

#[derive(Debug)]
pub enum TimedEvent {
    /// Events that are destined for a client to handle.
    ClientEvent(MacAddr, ClientEvent),
}

pub struct Ap {
    // TODO(37891): Make this private once we no longer need to depend on this in C bindings.
    pub ctx: Context,
    bss: Option<InfraBss>,
}

/// This trait adds an ok_or_bss_err for Option<&Bss> and Option<&mut Bss>, which returns an error
/// with ZX_ERR_BAD_STATE if the Option is uninhabited.
trait BssOptionExt<T: std::borrow::Borrow<InfraBss>> {
    fn ok_or_bss_err(self) -> Result<T, Error>;
}

impl<T: std::borrow::Borrow<InfraBss>> BssOptionExt<T> for Option<T> {
    fn ok_or_bss_err(self) -> Result<T, Error> {
        self.ok_or(Error::Status(format!("BSS not started"), zx::Status::BAD_STATE))
    }
}

impl Ap {
    pub fn new(
        device: Device,
        buf_provider: BufferProvider,
        scheduler: Scheduler,
        bssid: Bssid,
    ) -> Self {
        Self {
            ctx: Context::new(device, buf_provider, Timer::<TimedEvent>::new(scheduler), bssid),
            bss: None,
        }
    }

    // Timer handler functions.
    pub fn handle_timed_event(&mut self, event_id: EventId) {
        let bss = match self.bss.as_mut() {
            Some(bss) => bss,
            None => {
                error!("received timed event but BSS was not started yet");
                return;
            }
        };

        let event = match self.ctx.timer.triggered(&event_id) {
            Some(event) => event,
            None => {
                error!("received unknown timed event");
                return;
            }
        };

        if let Err(e) = bss.handle_timed_event(&mut self.ctx, event_id, event) {
            error!("failed to handle timed event frame: {}", e)
        }
    }

    // MLME handler functions.

    /// Handles MLME-START.request (IEEE Std 802.11-2016, 6.3.11.2) from the SME.
    fn handle_mlme_start_req(&mut self, req: fidl_mlme::StartRequest) -> Result<(), Error> {
        if self.bss.is_some() {
            info!("MLME-START.request: BSS already started");
            self.ctx
                .send_mlme_start_conf(fidl_mlme::StartResultCodes::BssAlreadyStartedOrJoined)?;
            return Ok(());
        }

        if req.bss_type != fidl_mlme::BssTypes::Infrastructure {
            info!("MLME-START.request: BSS type {:?} not supported", req.bss_type);
            self.ctx.send_mlme_start_conf(fidl_mlme::StartResultCodes::NotSupported)?;
            return Ok(());
        }

        self.bss.replace(InfraBss::new(
            &mut self.ctx,
            req.ssid.clone(),
            TimeUnit(req.beacon_period),
            req.rates,
            req.channel,
            req.rsne,
        )?);

        self.ctx.send_mlme_start_conf(fidl_mlme::StartResultCodes::Success)?;

        Ok(())
    }

    /// Handles MLME-STOP.request (IEEE Std 802.11-2016, 6.3.12.2) from the SME.
    fn handle_mlme_stop_req(&mut self, _req: fidl_mlme::StopRequest) -> Result<(), Error> {
        if let Some(bss) = self.bss.take() {
            bss.stop(&mut self.ctx)?;
        } else {
            info!("MLME-STOP.request: BSS not started");
        }
        Ok(())
    }

    /// Handles MLME-SETKEYS.request (IEEE Std 802.11-2016, 6.3.19.1) from the SME.
    ///
    /// The MLME should set the keys on the PHY.
    pub fn handle_mlme_setkeys_request(
        &mut self,
        req: fidl_mlme::SetKeysRequest,
    ) -> Result<(), Error> {
        for key_desc in req.keylist {
            self.ctx
                .device
                .set_key(KeyConfig::from(&key_desc))
                .map_err(|s| Error::Status(format!("failed to set keys on PHY"), s))?;
        }
        Ok(())
    }

    #[allow(deprecated)] // Allow until main message loop is in Rust.
    pub fn handle_mlme_msg(&mut self, msg: fidl_mlme::MlmeRequestMessage) -> Result<(), Error> {
        match msg {
            fidl_mlme::MlmeRequestMessage::StartReq { req } => self.handle_mlme_start_req(req),
            fidl_mlme::MlmeRequestMessage::StopReq { req } => self.handle_mlme_stop_req(req),
            fidl_mlme::MlmeRequestMessage::SetKeysReq { req } => {
                self.handle_mlme_setkeys_request(req)
            }
            fidl_mlme::MlmeRequestMessage::AuthenticateResp { resp } => {
                self.bss.as_mut().ok_or_bss_err()?.handle_mlme_auth_resp(&mut self.ctx, resp)
            }
            fidl_mlme::MlmeRequestMessage::DeauthenticateReq { req } => {
                self.bss.as_mut().ok_or_bss_err()?.handle_mlme_deauth_req(&mut self.ctx, req)
            }
            fidl_mlme::MlmeRequestMessage::AssociateResp { resp } => {
                self.bss.as_mut().ok_or_bss_err()?.handle_mlme_assoc_resp(&mut self.ctx, resp)
            }
            fidl_mlme::MlmeRequestMessage::DisassociateReq { req } => {
                self.bss.as_mut().ok_or_bss_err()?.handle_mlme_disassoc_req(&mut self.ctx, req)
            }
            fidl_mlme::MlmeRequestMessage::SetControlledPort { req } => {
                self.bss.as_mut().ok_or_bss_err()?.handle_mlme_set_controlled_port_req(req)
            }
            fidl_mlme::MlmeRequestMessage::EapolReq { req } => {
                self.bss.as_ref().ok_or_bss_err()?.handle_mlme_eapol_req(&mut self.ctx, req)
            }
            _ => Err(Error::Status(format!("not supported"), zx::Status::NOT_SUPPORTED)),
        }
        .map_err(|e| {
            error!("error handling MLME message: {}", e);
            e
        })
    }

    pub fn handle_eth_frame(
        &mut self,
        dst_addr: MacAddr,
        src_addr: MacAddr,
        ether_type: u16,
        body: &[u8],
    ) {
        let bss = match self.bss.as_mut() {
            Some(bss) => bss,
            None => {
                error!("received Ethernet frame but BSS was not started yet");
                return;
            }
        };

        if let Err(e) = bss.handle_eth_frame(&mut self.ctx, dst_addr, src_addr, ether_type, body) {
            log!(e.log_level(), "failed to handle Ethernet frame: {}", e)
        }
    }

    pub fn handle_mac_frame<B: ByteSlice>(&mut self, bytes: B, body_aligned: bool) {
        let bss = match self.bss.as_mut() {
            Some(bss) => bss,
            None => {
                error!("received WLAN frame but BSS was not started yet");
                return;
            }
        };

        let mac_frame = match mac::MacFrame::parse(bytes, body_aligned) {
            Some(mac_frame) => mac_frame,
            None => {
                error!("failed to parse MAC frame");
                return;
            }
        };

        if let Err(e) = match mac_frame {
            mac::MacFrame::Mgmt { mgmt_hdr, body, .. } => {
                bss.handle_mgmt_frame(&mut self.ctx, *mgmt_hdr, body)
            }
            mac::MacFrame::Data { fixed_fields, addr4, qos_ctrl, body, .. } => bss
                .handle_data_frame(
                    &mut self.ctx,
                    *fixed_fields,
                    addr4.map(|a| *a),
                    qos_ctrl.map(|x| x.get()),
                    body,
                ),
            _ => {
                // TODO(37891): Handle control frames.
                Ok(())
            }
        } {
            log!(e.log_level(), "failed to handle MAC frame: {}", e)
        }
    }
}

// TODO(37891): Add test for MLME-START.request when everything is hooked up.
// TODO(37891): Add test for MLME-STOP.request when everything is hooked up.
#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{
            buffer::FakeBufferProvider,
            device::FakeDevice,
            key::{KeyType, Protection},
            timer::FakeScheduler,
        },
        banjo_ddk_protocol_wlan_info::{WlanChannel, WlanChannelBandwidth},
        fidl_fuchsia_wlan_common as fidl_common,
        wlan_common::test_utils::fake_frames::fake_wpa2_rsne,
    };
    const CLIENT_ADDR: MacAddr = [1u8; 6];
    const BSSID: Bssid = Bssid([2u8; 6]);
    const CLIENT_ADDR2: MacAddr = [3u8; 6];

    #[test]
    fn ap_handle_eth_frame() {
        let mut fake_device = FakeDevice::new();
        let mut fake_scheduler = FakeScheduler::new();
        let mut ap = Ap::new(
            fake_device.as_device(),
            FakeBufferProvider::new(),
            fake_scheduler.as_scheduler(),
            BSSID,
        );
        ap.bss.replace(
            InfraBss::new(
                &mut ap.ctx,
                b"coolnet".to_vec(),
                TimeUnit::DEFAULT_BEACON_INTERVAL,
                vec![0b11111000],
                1,
                None,
            )
            .expect("expected InfraBss::new ok"),
        );
        ap.bss.as_mut().unwrap().clients.insert(CLIENT_ADDR, RemoteClient::new(CLIENT_ADDR));

        let client = ap.bss.as_mut().unwrap().clients.get_mut(&CLIENT_ADDR).unwrap();
        client
            .handle_mlme_auth_resp(&mut ap.ctx, fidl_mlme::AuthenticateResultCodes::Success)
            .expect("expected OK");
        client
            .handle_mlme_assoc_resp(
                &mut ap.ctx,
                false,
                1,
                mac::CapabilityInfo(0),
                fidl_mlme::AssociateResultCodes::Success,
                1,
                &[1, 2, 3, 4, 5, 6, 7, 8, 9, 10][..],
            )
            .expect("expected OK");
        fake_device.wlan_queue.clear();

        ap.handle_eth_frame(CLIENT_ADDR, CLIENT_ADDR2, 0x1234, &[1, 2, 3, 4, 5][..]);

        assert_eq!(fake_device.wlan_queue.len(), 1);
        assert_eq!(
            &fake_device.wlan_queue[0].0[..],
            &[
                // Mgmt header
                0b00001000, 0b00000010, // Frame Control
                0, 0, // Duration
                1, 1, 1, 1, 1, 1, // addr1
                2, 2, 2, 2, 2, 2, // addr2
                3, 3, 3, 3, 3, 3, // addr3
                0x30, 0, // Sequence Control
                0xAA, 0xAA, 0x03, // DSAP, SSAP, Control, OUI
                0, 0, 0, // OUI
                0x12, 0x34, // Protocol ID
                // Data
                1, 2, 3, 4, 5,
            ][..]
        );
    }

    #[test]
    fn ap_handle_eth_frame_no_such_client() {
        let mut fake_device = FakeDevice::new();
        let mut fake_scheduler = FakeScheduler::new();
        let mut ap = Ap::new(
            fake_device.as_device(),
            FakeBufferProvider::new(),
            fake_scheduler.as_scheduler(),
            BSSID,
        );
        ap.bss.replace(
            InfraBss::new(
                &mut ap.ctx,
                b"coolnet".to_vec(),
                TimeUnit::DEFAULT_BEACON_INTERVAL,
                vec![0b11111000],
                1,
                None,
            )
            .expect("expected InfraBss::new ok"),
        );
        ap.handle_eth_frame(CLIENT_ADDR2, CLIENT_ADDR, 0x1234, &[1, 2, 3, 4, 5][..]);
    }

    #[test]
    fn ap_handle_mac_frame() {
        let mut fake_device = FakeDevice::new();
        let mut fake_scheduler = FakeScheduler::new();
        let mut ap = Ap::new(
            fake_device.as_device(),
            FakeBufferProvider::new(),
            fake_scheduler.as_scheduler(),
            BSSID,
        );
        ap.bss.replace(
            InfraBss::new(
                &mut ap.ctx,
                b"coolnet".to_vec(),
                TimeUnit::DEFAULT_BEACON_INTERVAL,
                vec![0b11111000],
                1,
                None,
            )
            .expect("expected InfraBss::new ok"),
        );
        ap.handle_mac_frame(
            &[
                // Mgmt header
                0b10110000, 0b00000000, // Frame Control
                0, 0, // Duration
                2, 2, 2, 2, 2, 2, // addr1
                1, 1, 1, 1, 1, 1, // addr2
                2, 2, 2, 2, 2, 2, // addr3
                0x10, 0, // Sequence Control
                // Auth body
                0, 0, // Auth Algorithm Number
                1, 0, // Auth Txn Seq Number
                0, 0, // Status code
            ][..],
            false,
        );

        assert_eq!(ap.bss.as_mut().unwrap().clients.contains_key(&CLIENT_ADDR), true);

        let msg = fake_device
            .next_mlme_msg::<fidl_mlme::AuthenticateIndication>()
            .expect("expected MLME message");
        assert_eq!(
            msg,
            fidl_mlme::AuthenticateIndication {
                peer_sta_address: CLIENT_ADDR,
                auth_type: fidl_mlme::AuthenticationTypes::OpenSystem,
            },
        );
    }

    #[test]
    fn ap_handle_mac_frame_no_such_client() {
        let mut fake_device = FakeDevice::new();
        let mut fake_scheduler = FakeScheduler::new();
        let mut ap = Ap::new(
            fake_device.as_device(),
            FakeBufferProvider::new(),
            fake_scheduler.as_scheduler(),
            BSSID,
        );
        ap.bss.replace(
            InfraBss::new(
                &mut ap.ctx,
                b"coolnet".to_vec(),
                TimeUnit::DEFAULT_BEACON_INTERVAL,
                vec![0b11111000],
                1,
                None,
            )
            .expect("expected InfraBss::new ok"),
        );
        ap.handle_mac_frame(
            &[
                // Mgmt header
                0b10100000, 0b00000001, // Frame Control
                0, 0, // Duration
                2, 2, 2, 2, 2, 2, // addr1
                1, 1, 1, 1, 1, 1, // addr2
                2, 2, 2, 2, 2, 2, // addr3
                0x10, 0, // Sequence Control
                // Disassoc header:
                8, 0, // reason code
            ][..],
            false,
        );

        assert_eq!(ap.bss.as_mut().unwrap().clients.contains_key(&CLIENT_ADDR), false);
    }

    #[test]
    fn ap_handle_mac_frame_bogus() {
        let mut fake_device = FakeDevice::new();
        let mut fake_scheduler = FakeScheduler::new();
        let mut ap = Ap::new(
            fake_device.as_device(),
            FakeBufferProvider::new(),
            fake_scheduler.as_scheduler(),
            BSSID,
        );
        ap.bss.replace(
            InfraBss::new(
                &mut ap.ctx,
                b"coolnet".to_vec(),
                TimeUnit::DEFAULT_BEACON_INTERVAL,
                vec![0b11111000],
                1,
                None,
            )
            .expect("expected InfraBss::new ok"),
        );
        ap.handle_mac_frame(&[0][..], false);
    }

    #[test]
    fn ap_handle_mlme_start_req() {
        let mut fake_device = FakeDevice::new();
        let mut fake_scheduler = FakeScheduler::new();
        let mut ap = Ap::new(
            fake_device.as_device(),
            FakeBufferProvider::new(),
            fake_scheduler.as_scheduler(),
            BSSID,
        );
        ap.handle_mlme_start_req(fidl_mlme::StartRequest {
            ssid: b"coolnet".to_vec(),
            bss_type: fidl_mlme::BssTypes::Infrastructure,
            beacon_period: 5,
            dtim_period: 1,
            channel: 2,
            rates: vec![0b11111000],
            country: fidl_mlme::Country { alpha2: *b"xx", suffix: fidl_mlme::COUNTRY_ENVIRON_ALL },
            mesh_id: vec![],
            rsne: None,
            phy: fidl_common::Phy::Erp,
            cbw: fidl_common::Cbw::Cbw20,
        })
        .expect("expected Ap::handle_mlme_start_request OK");

        assert!(ap.bss.is_some());
        assert_eq!(
            fake_device.wlan_channel,
            WlanChannel {
                primary: 2,
                // TODO(40917): Correctly support this.
                cbw: WlanChannelBandwidth::_20,
                secondary80: 0,
            }
        );

        let msg =
            fake_device.next_mlme_msg::<fidl_mlme::StartConfirm>().expect("expected MLME message");
        assert_eq!(
            msg,
            fidl_mlme::StartConfirm { result_code: fidl_mlme::StartResultCodes::Success },
        );
    }

    #[test]
    fn ap_handle_mlme_start_req_already_started() {
        let mut fake_device = FakeDevice::new();
        let mut fake_scheduler = FakeScheduler::new();
        let mut ap = Ap::new(
            fake_device.as_device(),
            FakeBufferProvider::new(),
            fake_scheduler.as_scheduler(),
            BSSID,
        );
        ap.bss.replace(
            InfraBss::new(
                &mut ap.ctx,
                b"coolnet".to_vec(),
                TimeUnit::DEFAULT_BEACON_INTERVAL,
                vec![0b11111000],
                1,
                None,
            )
            .expect("expected InfraBss::new ok"),
        );

        ap.handle_mlme_start_req(fidl_mlme::StartRequest {
            ssid: b"coolnet".to_vec(),
            bss_type: fidl_mlme::BssTypes::Infrastructure,
            beacon_period: 5,
            dtim_period: 1,
            channel: 2,
            rates: vec![],
            country: fidl_mlme::Country { alpha2: *b"xx", suffix: fidl_mlme::COUNTRY_ENVIRON_ALL },
            mesh_id: vec![],
            rsne: None,
            phy: fidl_common::Phy::Erp,
            cbw: fidl_common::Cbw::Cbw20,
        })
        .expect("expected Ap::handle_mlme_start_request OK");

        let msg =
            fake_device.next_mlme_msg::<fidl_mlme::StartConfirm>().expect("expected MLME message");
        assert_eq!(
            msg,
            fidl_mlme::StartConfirm {
                result_code: fidl_mlme::StartResultCodes::BssAlreadyStartedOrJoined
            },
        );
    }

    #[test]
    fn ap_handle_mlme_stop_req() {
        let mut fake_device = FakeDevice::new();
        let mut fake_scheduler = FakeScheduler::new();
        let mut ap = Ap::new(
            fake_device.as_device(),
            FakeBufferProvider::new(),
            fake_scheduler.as_scheduler(),
            BSSID,
        );
        ap.bss.replace(
            InfraBss::new(
                &mut ap.ctx,
                b"coolnet".to_vec(),
                TimeUnit::DEFAULT_BEACON_INTERVAL,
                vec![0b11111000],
                1,
                None,
            )
            .expect("expected InfraBss::new ok"),
        );

        ap.handle_mlme_stop_req(fidl_mlme::StopRequest { ssid: b"coolnet".to_vec() })
            .expect("expected Ap::handle_mlme_stop_request OK");
        assert!(ap.bss.is_none());
    }

    #[test]
    fn ap_handle_mlme_setkeys_request() {
        let mut fake_device = FakeDevice::new();
        let mut fake_scheduler = FakeScheduler::new();
        let mut ap = Ap::new(
            fake_device.as_device(),
            FakeBufferProvider::new(),
            fake_scheduler.as_scheduler(),
            BSSID,
        );
        ap.handle_mlme_setkeys_request(fidl_mlme::SetKeysRequest {
            keylist: vec![fidl_mlme::SetKeyDescriptor {
                cipher_suite_oui: [1, 2, 3],
                cipher_suite_type: 4,
                key_type: fidl_mlme::KeyType::Pairwise,
                address: [5; 6],
                key_id: 6,
                key: vec![1, 2, 3, 4, 5, 6, 7],
                rsc: 8,
            }],
        })
        .expect("expected Ap::handle_mlme_setkeys_request OK");
        assert_eq!(fake_device.keys.len(), 1);
        assert_eq!(
            fake_device.keys[0],
            KeyConfig {
                bssid: 0,
                protection: Protection::RX_TX,
                cipher_oui: [1, 2, 3],
                cipher_type: 4,
                key_type: KeyType::PAIRWISE,
                peer_addr: [5; 6],
                key_idx: 6,
                key_len: 7,
                key: [
                    1, 2, 3, 4, 5, 6, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                    0, 0, 0, 0, 0, 0,
                ],
                rsc: 8,
            }
        );
    }

    #[test]
    fn ap_handle_mlme_msg_handle_mlme_auth_resp() {
        let mut fake_device = FakeDevice::new();
        let mut fake_scheduler = FakeScheduler::new();
        let mut ap = Ap::new(
            fake_device.as_device(),
            FakeBufferProvider::new(),
            fake_scheduler.as_scheduler(),
            BSSID,
        );
        ap.bss.replace(
            InfraBss::new(
                &mut ap.ctx,
                b"coolnet".to_vec(),
                TimeUnit::DEFAULT_BEACON_INTERVAL,
                vec![0b11111000],
                1,
                None,
            )
            .expect("expected InfraBss::new ok"),
        );
        ap.bss.as_mut().unwrap().clients.insert(CLIENT_ADDR, RemoteClient::new(CLIENT_ADDR));

        #[allow(deprecated)]
        ap.handle_mlme_msg(fidl_mlme::MlmeRequestMessage::AuthenticateResp {
            resp: fidl_mlme::AuthenticateResponse {
                peer_sta_address: CLIENT_ADDR,
                result_code: fidl_mlme::AuthenticateResultCodes::AntiCloggingTokenRequired,
            },
        })
        .expect("expected Ap::handle_mlme_msg(fidl_mlme::MlmeRequestMessage::AuthenticateResp) ok");
        assert_eq!(fake_device.wlan_queue.len(), 1);
        assert_eq!(
            &fake_device.wlan_queue[0].0[..],
            &[
                // Mgmt header
                0b10110000, 0, // Frame Control
                0, 0, // Duration
                1, 1, 1, 1, 1, 1, // addr1
                2, 2, 2, 2, 2, 2, // addr2
                2, 2, 2, 2, 2, 2, // addr3
                0x10, 0, // Sequence Control
                // Auth header:
                0, 0, // auth algorithm
                2, 0, // auth txn seq num
                76, 0, // status code
            ][..]
        );
    }

    #[test]
    fn ap_handle_mlme_msg_handle_mlme_auth_resp_no_bss() {
        let mut fake_device = FakeDevice::new();
        let mut fake_scheduler = FakeScheduler::new();
        let mut ap = Ap::new(
            fake_device.as_device(),
            FakeBufferProvider::new(),
            fake_scheduler.as_scheduler(),
            BSSID,
        );

        assert_eq!(
            zx::Status::from(
                #[allow(deprecated)]
                ap.handle_mlme_msg(fidl_mlme::MlmeRequestMessage::AuthenticateResp {
                    resp: fidl_mlme::AuthenticateResponse {
                        peer_sta_address: CLIENT_ADDR,
                        result_code: fidl_mlme::AuthenticateResultCodes::AntiCloggingTokenRequired,
                    },
                })
                .expect_err("expected Ap::handle_mlme_msg(fidl_mlme::MlmeRequestMessage::AuthenticateResp) error")
            ),
            zx::Status::BAD_STATE
        );
    }

    #[test]
    fn ap_handle_mlme_msg_handle_mlme_auth_resp_no_such_client() {
        let mut fake_device = FakeDevice::new();
        let mut fake_scheduler = FakeScheduler::new();
        let mut ap = Ap::new(
            fake_device.as_device(),
            FakeBufferProvider::new(),
            fake_scheduler.as_scheduler(),
            BSSID,
        );
        ap.bss.replace(
            InfraBss::new(
                &mut ap.ctx,
                b"coolnet".to_vec(),
                TimeUnit::DEFAULT_BEACON_INTERVAL,
                vec![0b11111000],
                1,
                None,
            )
            .expect("expected InfraBss::new ok"),
        );

        assert_eq!(
            zx::Status::from(
                #[allow(deprecated)]
                ap.handle_mlme_msg(fidl_mlme::MlmeRequestMessage::AuthenticateResp {
                    resp: fidl_mlme::AuthenticateResponse {
                        peer_sta_address: CLIENT_ADDR,
                        result_code: fidl_mlme::AuthenticateResultCodes::AntiCloggingTokenRequired,
                    },
                })
                .expect_err("expected Ap::handle_mlme_msg(fidl_mlme::MlmeRequestMessage::AuthenticateResp) error")
            ),
            zx::Status::NOT_FOUND
        );
    }

    #[test]
    fn ap_handle_mlme_msg_handle_mlme_deauth_req() {
        let mut fake_device = FakeDevice::new();
        let mut fake_scheduler = FakeScheduler::new();
        let mut ap = Ap::new(
            fake_device.as_device(),
            FakeBufferProvider::new(),
            fake_scheduler.as_scheduler(),
            BSSID,
        );
        ap.bss.replace(
            InfraBss::new(
                &mut ap.ctx,
                b"coolnet".to_vec(),
                TimeUnit::DEFAULT_BEACON_INTERVAL,
                vec![0b11111000],
                1,
                None,
            )
            .expect("expected InfraBss::new ok"),
        );
        ap.bss.as_mut().unwrap().clients.insert(CLIENT_ADDR, RemoteClient::new(CLIENT_ADDR));

        #[allow(deprecated)]
        ap.handle_mlme_msg(fidl_mlme::MlmeRequestMessage::DeauthenticateReq {
            req: fidl_mlme::DeauthenticateRequest {
                peer_sta_address: CLIENT_ADDR,
                reason_code: fidl_mlme::ReasonCode::LeavingNetworkDeauth,
            },
        })
        .expect(
            "expected Ap::handle_mlme_msg(fidl_mlme::MlmeRequestMessage::DeauthenticateReq) ok",
        );
        assert_eq!(fake_device.wlan_queue.len(), 1);
        assert_eq!(
            &fake_device.wlan_queue[0].0[..],
            &[
                // Mgmt header
                0b11000000, 0, // Frame Control
                0, 0, // Duration
                1, 1, 1, 1, 1, 1, // addr1
                2, 2, 2, 2, 2, 2, // addr2
                2, 2, 2, 2, 2, 2, // addr3
                0x10, 0, // Sequence Control
                // Deauth header:
                3, 0, // reason code
            ][..]
        );
    }

    #[test]
    fn ap_handle_mlme_msg_handle_mlme_assoc_resp() {
        let mut fake_device = FakeDevice::new();
        let mut fake_scheduler = FakeScheduler::new();
        let mut ap = Ap::new(
            fake_device.as_device(),
            FakeBufferProvider::new(),
            fake_scheduler.as_scheduler(),
            BSSID,
        );
        ap.bss.replace(
            InfraBss::new(
                &mut ap.ctx,
                b"coolnet".to_vec(),
                TimeUnit::DEFAULT_BEACON_INTERVAL,
                vec![0b11111000],
                1,
                None,
            )
            .expect("expected InfraBss::new ok"),
        );
        ap.bss.as_mut().unwrap().clients.insert(CLIENT_ADDR, RemoteClient::new(CLIENT_ADDR));

        #[allow(deprecated)]
        ap.handle_mlme_msg(fidl_mlme::MlmeRequestMessage::AssociateResp {
            resp: fidl_mlme::AssociateResponse {
                peer_sta_address: CLIENT_ADDR,
                result_code: fidl_mlme::AssociateResultCodes::Success,
                association_id: 1,
                rates: vec![1, 2, 3, 4, 5, 6, 7, 8, 9, 10],
            },
        })
        .expect("expected Ap::handle_mlme_msg(fidl_mlme::MlmeRequestMessage::AssociateResp) ok");
        assert_eq!(fake_device.wlan_queue.len(), 1);
        assert_eq!(
            &fake_device.wlan_queue[0].0[..],
            &[
                // Mgmt header
                0b00010000, 0, // Frame Control
                0, 0, // Duration
                1, 1, 1, 1, 1, 1, // addr1
                2, 2, 2, 2, 2, 2, // addr2
                2, 2, 2, 2, 2, 2, // addr3
                0x10, 0, // Sequence Control
                // Association response header:
                0, 0, // Capabilities
                0, 0, // status code
                1, 0, // AID
                // IEs
                1, 8, 1, 2, 3, 4, 5, 6, 7, 8, // Rates
                50, 2, 9, 10, // Extended rates
                90, 3, 90, 0, 0, // BSS max idle period
            ][..]
        );
    }

    #[test]
    fn ap_handle_mlme_msg_handle_mlme_disassoc_req() {
        let mut fake_device = FakeDevice::new();
        let mut fake_scheduler = FakeScheduler::new();
        let mut ap = Ap::new(
            fake_device.as_device(),
            FakeBufferProvider::new(),
            fake_scheduler.as_scheduler(),
            BSSID,
        );
        ap.bss.replace(
            InfraBss::new(
                &mut ap.ctx,
                b"coolnet".to_vec(),
                TimeUnit::DEFAULT_BEACON_INTERVAL,
                vec![0b11111000],
                1,
                None,
            )
            .expect("expected InfraBss::new ok"),
        );
        ap.bss.as_mut().unwrap().clients.insert(CLIENT_ADDR, RemoteClient::new(CLIENT_ADDR));

        #[allow(deprecated)]
        ap.handle_mlme_msg(fidl_mlme::MlmeRequestMessage::DisassociateReq {
            req: fidl_mlme::DisassociateRequest {
                peer_sta_address: CLIENT_ADDR,
                reason_code: fidl_mlme::ReasonCode::LeavingNetworkDisassoc as u16,
            },
        })
        .expect("expected Ap::handle_mlme_msg(fidl_mlme::MlmeRequestMessage::DisassociateReq) ok");
        assert_eq!(fake_device.wlan_queue.len(), 1);
        assert_eq!(
            &fake_device.wlan_queue[0].0[..],
            &[
                // Mgmt header
                0b10100000, 0, // Frame Control
                0, 0, // Duration
                1, 1, 1, 1, 1, 1, // addr1
                2, 2, 2, 2, 2, 2, // addr2
                2, 2, 2, 2, 2, 2, // addr3
                0x10, 0, // Sequence Control
                // Disassoc header:
                8, 0, // reason code
            ][..]
        );
    }

    #[test]
    fn ap_handle_mlme_msg_handle_mlme_set_controlled_port_req() {
        let mut fake_device = FakeDevice::new();
        let mut fake_scheduler = FakeScheduler::new();
        let mut ap = Ap::new(
            fake_device.as_device(),
            FakeBufferProvider::new(),
            fake_scheduler.as_scheduler(),
            BSSID,
        );
        ap.bss.replace(
            InfraBss::new(
                &mut ap.ctx,
                b"coolnet".to_vec(),
                TimeUnit::DEFAULT_BEACON_INTERVAL,
                vec![0b11111000],
                1,
                Some(fake_wpa2_rsne()),
            )
            .expect("expected InfraBss::new ok"),
        );
        ap.bss.as_mut().unwrap().clients.insert(CLIENT_ADDR, RemoteClient::new(CLIENT_ADDR));

        #[allow(deprecated)]
        ap.handle_mlme_msg(fidl_mlme::MlmeRequestMessage::AssociateResp {
            resp: fidl_mlme::AssociateResponse {
                peer_sta_address: CLIENT_ADDR,
                result_code: fidl_mlme::AssociateResultCodes::Success,
                association_id: 1,
                rates: vec![1, 2, 3],
            },
        })
        .expect("expected Ap::handle_mlme_msg(fidl_mlme::MlmeRequestMessage::AssociateResp) ok");

        #[allow(deprecated)]
        ap.handle_mlme_msg(fidl_mlme::MlmeRequestMessage::SetControlledPort {
            req: fidl_mlme::SetControlledPortRequest {
                peer_sta_address: CLIENT_ADDR,
                state: fidl_mlme::ControlledPortState::Open,
            },
        })
        .expect(
            "expected Ap::handle_mlme_msg(fidl_mlme::MlmeRequestMessage::SetControlledPort) ok",
        );
    }

    #[test]
    fn ap_handle_mlme_msg_handle_mlme_eapol_req() {
        let mut fake_device = FakeDevice::new();
        let mut fake_scheduler = FakeScheduler::new();
        let mut ap = Ap::new(
            fake_device.as_device(),
            FakeBufferProvider::new(),
            fake_scheduler.as_scheduler(),
            BSSID,
        );
        ap.bss.replace(
            InfraBss::new(
                &mut ap.ctx,
                b"coolnet".to_vec(),
                TimeUnit::DEFAULT_BEACON_INTERVAL,
                vec![0b11111000],
                1,
                None,
            )
            .expect("expected InfraBss::new ok"),
        );
        ap.bss.as_mut().unwrap().clients.insert(CLIENT_ADDR, RemoteClient::new(CLIENT_ADDR));

        #[allow(deprecated)]
        ap.handle_mlme_msg(fidl_mlme::MlmeRequestMessage::EapolReq {
            req: fidl_mlme::EapolRequest {
                dst_addr: CLIENT_ADDR,
                src_addr: BSSID.0,
                data: vec![1, 2, 3],
            },
        })
        .expect("expected Ap::handle_mlme_msg(fidl_mlme::MlmeRequestMessage::EapolReq) ok");
        assert_eq!(fake_device.wlan_queue.len(), 1);
        assert_eq!(
            &fake_device.wlan_queue[0].0[..],
            &[
                // Header
                0b00001000, 0b00000010, // Frame Control
                0, 0, // Duration
                1, 1, 1, 1, 1, 1, // addr1
                2, 2, 2, 2, 2, 2, // addr2
                2, 2, 2, 2, 2, 2, // addr3
                0x10, 0, // Sequence Control
                0xAA, 0xAA, 0x03, // DSAP, SSAP, Control, OUI
                0, 0, 0, // OUI
                0x88, 0x8E, // EAPOL protocol ID
                // Data
                1, 2, 3,
            ][..]
        );
    }

    #[test]
    fn display_rejection() {
        assert_eq!(format!("{}", Rejection::BadDsBits), "BadDsBits");
    }
}
