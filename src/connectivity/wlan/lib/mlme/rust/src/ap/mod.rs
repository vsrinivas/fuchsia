// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod context;
mod frame_writer;
mod infra_bss;
mod remote_client;

use {
    crate::{
        buffer::{BufferProvider, InBuf},
        device::{Device, TxFlags},
        error::Error,
        logger,
        timer::{EventId, Scheduler, Timer},
    },
    banjo_ddk_protocol_wlan_mac as banjo_wlan_mac, fidl_fuchsia_wlan_mlme as fidl_mlme,
    fuchsia_zircon as zx,
    log::{error, info, log},
    std::fmt,
    wlan_common::{
        mac::{self, Bssid, CapabilityInfo, MacAddr},
        TimeUnit,
    },
    zerocopy::ByteSlice,
};

use context::*;
use infra_bss::*;
use remote_client::*;

#[derive(Debug)]
struct BufferedFrame {
    in_buf: InBuf,
    bytes_written: usize,
    tx_flags: TxFlags,
}

/// Rejection reasons for why a frame was not proceessed.
#[derive(Debug)]
pub enum Rejection {
    /// The frame was for another BSS.
    OtherBss,

    /// For data frames: The To DS bit was false, or the From DS bit was true.
    /// For management frames: The To DS bit was set and the frame was not a QMF (QoS Management
    /// frame) management frame, or the reserved From DS bit was set.
    BadDsBits,

    /// For ethernet frames

    /// Frame is malformed (For example, a minimum Ethernet frame must contain a header(14 bytes).
    FrameMalformed,

    /// No source address was found.
    NoSrcAddr,

    /// No client with the given address was found.
    NoSuchClient(MacAddr),

    /// Some error specific to a client occurred.
    Client(MacAddr, ClientRejection),

    /// Some general error occurred.
    Error(anyhow::Error),
}

impl Rejection {
    fn log_level(&self) -> log::Level {
        match self {
            Self::NoSrcAddr | Self::FrameMalformed => log::Level::Error,
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

impl From<anyhow::Error> for Rejection {
    fn from(e: anyhow::Error) -> Rejection {
        Self::Error(e)
    }
}

#[derive(Debug)]
pub enum TimedEvent {
    /// Events that are destined for a client to handle.
    ClientEvent(MacAddr, ClientEvent),
}

pub struct Ap {
    ctx: Context,
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
        // TODO(fxbug.dev/41417): Remove this once devmgr installs a Rust logger.
        logger::install();

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
            req.dtim_period,
            CapabilityInfo(req.cap),
            req.rates,
            req.channel,
            req.rsne,
        )?);

        self.ctx.send_mlme_start_conf(fidl_mlme::StartResultCodes::Success)?;

        info!("MLME-START.request: OK");
        Ok(())
    }

    /// Handles MLME-STOP.request (IEEE Std 802.11-2016, 6.3.12.2) from the SME.
    fn handle_mlme_stop_req(&mut self, _req: fidl_mlme::StopRequest) -> Result<(), Error> {
        if let Some(bss) = self.bss.take() {
            bss.stop(&mut self.ctx)?;
        } else {
            info!("MLME-STOP.request: BSS not started");
        }
        info!("MLME-STOP.request: OK");
        Ok(())
    }

    /// Handles MLME-SETKEYS.request (IEEE Std 802.11-2016, 6.3.19.1) from the SME.
    ///
    /// The MLME should set the keys on the PHY.
    pub fn handle_mlme_setkeys_req(&mut self, req: fidl_mlme::SetKeysRequest) -> Result<(), Error> {
        if let Some(bss) = self.bss.as_mut() {
            bss.handle_mlme_setkeys_req(&mut self.ctx, &req.keylist[..])
        } else {
            Err(Error::Status(format!("cannot set keys on unstarted BSS"), zx::Status::BAD_STATE))
        }
    }

    #[allow(deprecated)] // Allow until main message loop is in Rust.
    pub fn handle_mlme_msg(&mut self, msg: fidl_mlme::MlmeRequestMessage) -> Result<(), Error> {
        match msg {
            fidl_mlme::MlmeRequestMessage::StartReq { req } => self.handle_mlme_start_req(req),
            fidl_mlme::MlmeRequestMessage::StopReq { req } => self.handle_mlme_stop_req(req),
            fidl_mlme::MlmeRequestMessage::SetKeysReq { req } => self.handle_mlme_setkeys_req(req),
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
                self.bss.as_mut().ok_or_bss_err()?.handle_mlme_eapol_req(&mut self.ctx, req)
            }
            _ => Err(Error::Status(format!("not supported"), zx::Status::NOT_SUPPORTED)),
        }
        .map_err(|e| {
            error!("error handling MLME message: {}", e);
            e
        })
    }

    pub fn handle_eth_frame(&mut self, frame: &[u8]) {
        let bss = match self.bss.as_mut() {
            Some(bss) => bss,
            None => {
                error!("received Ethernet frame but BSS was not started yet");
                return;
            }
        };

        let mac::EthernetFrame { hdr, body } =
            match mac::EthernetFrame::parse(frame).ok_or_else(|| Rejection::FrameMalformed) {
                Ok(eth_frame) => eth_frame,
                Err(e) => {
                    error!("failed to parse Ethernet frame: {}", e);
                    return;
                }
            };

        if let Err(e) = bss.handle_eth_frame(&mut self.ctx, *hdr, body) {
            log!(e.log_level(), "failed to handle Ethernet frame: {}", e)
        }
    }

    pub fn handle_mac_frame<B: ByteSlice>(
        &mut self,
        bytes: B,
        rx_info: Option<banjo_wlan_mac::WlanRxInfo>,
    ) {
        let bss = match self.bss.as_mut() {
            Some(bss) => bss,
            None => {
                error!("received WLAN frame but BSS was not started yet");
                return;
            }
        };

        // Rogue frames received from the wrong channel
        if let Some(rx_info) = rx_info {
            if rx_info.chan.primary != bss.channel {
                return;
            }
        }

        let body_aligned = rx_info.map_or(false, |ri| {
            (ri.rx_flags & banjo_wlan_mac::WlanRxInfoFlags::FRAME_BODY_PADDING_4.0) != 0
        });

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
            mac::MacFrame::Ctrl { frame_ctrl, body } => {
                bss.handle_ctrl_frame(&mut self.ctx, frame_ctrl, body)
            }
            mac::MacFrame::Unsupported { frame_ctrl } => {
                error!("received unsupported MAC frame: frame_ctrl = {:?}", frame_ctrl);
                return;
            }
        } {
            log!(e.log_level(), "failed to handle MAC frame: {}", e)
        }
    }

    pub fn handle_hw_indication(&mut self, ind: banjo_wlan_mac::WlanIndication) {
        let bss = match self.bss.as_mut() {
            Some(bss) => bss,
            None => {
                error!("received HW indication but BSS was not started yet");
                return;
            }
        };

        if let Err(e) = bss.handle_hw_indication(&mut self.ctx, ind) {
            error!("failed to handle HW indication {:?}: {}", ind, e)
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{
            buffer::FakeBufferProvider,
            device::FakeDevice,
            key::{KeyConfig, KeyType, Protection},
            timer::FakeScheduler,
        },
        banjo_ddk_protocol_wlan_info::{WlanChannel, WlanChannelBandwidth},
        fidl_fuchsia_wlan_common as fidl_common,
        wlan_common::{
            assert_variant, big_endian::BigEndianU16, test_utils::fake_frames::fake_wpa2_rsne,
        },
        wlan_frame_writer::write_frame_with_dynamic_buf,
    };
    const CLIENT_ADDR: MacAddr = [4u8; 6];
    const BSSID: Bssid = Bssid([2u8; 6]);
    const CLIENT_ADDR2: MacAddr = [6u8; 6];

    fn make_eth_frame(
        dst_addr: MacAddr,
        src_addr: MacAddr,
        protocol_id: u16,
        body: &[u8],
    ) -> Vec<u8> {
        let (mut buf, bytes_written) = write_frame_with_dynamic_buf!(vec![], {
            headers: {
                mac::EthernetIIHdr: &mac::EthernetIIHdr {
                    da: dst_addr,
                    sa: src_addr,
                    ether_type: BigEndianU16::from_native(protocol_id),
                },
            },
            payload: body,
        })
        .expect("writing to vec always succeeds");
        buf.truncate(bytes_written);
        buf
    }

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
                2,
                CapabilityInfo(0),
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

        ap.handle_eth_frame(&make_eth_frame(
            CLIENT_ADDR,
            CLIENT_ADDR2,
            0x1234,
            &[1, 2, 3, 4, 5][..],
        ));

        assert_eq!(fake_device.wlan_queue.len(), 1);
        assert_eq!(
            &fake_device.wlan_queue[0].0[..],
            &[
                // Mgmt header
                0b00001000, 0b00000010, // Frame Control
                0, 0, // Duration
                4, 4, 4, 4, 4, 4, // addr1
                2, 2, 2, 2, 2, 2, // addr2
                6, 6, 6, 6, 6, 6, // addr3
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
                2,
                CapabilityInfo(0),
                vec![0b11111000],
                1,
                None,
            )
            .expect("expected InfraBss::new ok"),
        );
        ap.handle_eth_frame(&make_eth_frame(
            CLIENT_ADDR2,
            CLIENT_ADDR,
            0x1234,
            &[1, 2, 3, 4, 5][..],
        ));
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
                2,
                CapabilityInfo(0),
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
                4, 4, 4, 4, 4, 4, // addr2
                2, 2, 2, 2, 2, 2, // addr3
                0x10, 0, // Sequence Control
                // Auth body
                0, 0, // Auth Algorithm Number
                1, 0, // Auth Txn Seq Number
                0, 0, // Status code
            ][..],
            None,
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
    fn ap_handle_mac_frame_ps_poll() {
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
                2,
                CapabilityInfo(0),
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

        // Put the client into dozing.
        ap.handle_mac_frame(
            &[
                0b01001000, 0b00010001, // Frame control.
                0, 0, // Duration.
                2, 2, 2, 2, 2, 2, // BSSID.
                4, 4, 4, 4, 4, 4, // MAC address.
                2, 2, 2, 2, 2, 2, // BSSID.
                0x10, 0, // Sequence control.
            ][..],
            None,
        );

        ap.handle_eth_frame(&make_eth_frame(
            CLIENT_ADDR,
            CLIENT_ADDR2,
            0x1234,
            &[1, 2, 3, 4, 5][..],
        ));
        assert_eq!(fake_device.wlan_queue.len(), 0);

        // Send a PS-Poll.
        ap.handle_mac_frame(
            &[
                // Ctrl header
                0b10100100, 0b00000000, // Frame Control
                0b00000001, 0b11000000, // Masked AID
                2, 2, 2, 2, 2, 2, // addr1
                4, 4, 4, 4, 4, 4, // addr2
            ][..],
            None,
        );

        assert_eq!(fake_device.wlan_queue.len(), 1);
        assert_eq!(
            &fake_device.wlan_queue[0].0[..],
            &[
                // Mgmt header
                0b00001000, 0b00000010, // Frame Control
                0, 0, // Duration
                4, 4, 4, 4, 4, 4, // addr1
                2, 2, 2, 2, 2, 2, // addr2
                6, 6, 6, 6, 6, 6, // addr3
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
                2,
                CapabilityInfo(0),
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
                4, 4, 4, 4, 4, 4, // addr2
                2, 2, 2, 2, 2, 2, // addr3
                0x10, 0, // Sequence Control
                // Disassoc header:
                8, 0, // reason code
            ][..],
            None,
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
                2,
                CapabilityInfo(0),
                vec![0b11111000],
                1,
                None,
            )
            .expect("expected InfraBss::new ok"),
        );
        ap.handle_mac_frame(&[0][..], None);
    }

    #[test]
    fn ap_handle_mac_frame_wrong_channel_drop() {
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
                2,
                CapabilityInfo(0),
                vec![0b11111000],
                1,
                None,
            )
            .expect("expected InfraBss::new ok"),
        );
        let probe_req = [
            // Mgmt header
            0b01000000, 0b00000000, // Frame Control
            0, 0, // Duration
            2, 2, 2, 2, 2, 2, // addr1
            4, 4, 4, 4, 4, 4, // addr2
            2, 2, 2, 2, 2, 2, // addr3
            0x10, 0, // Sequence Control
            // SSID
            0, 7, 0x63, 0x6f, 0x6f, 0x6c, 0x6e, 0x65, 0x74, 0x0a,
        ];
        let rx_info_wrong_channel = banjo_wlan_mac::WlanRxInfo {
            rx_flags: 0,
            valid_fields: 0,
            phy: 0,
            data_rate: 0,
            chan: WlanChannel { primary: 0, cbw: WlanChannelBandwidth::_20, secondary80: 0 },
            mcs: 0,
            rssi_dbm: 0,
            rcpi_dbmh: 0,
            snr_dbh: 0,
        };
        ap.handle_mac_frame(&probe_req[..], Some(rx_info_wrong_channel.clone()));

        // Probe Request from the wrong channel should be dropped and no probe response sent.
        assert_eq!(fake_device.wlan_queue.len(), 0);

        // Frame from unknown channel should be processed and a probe response sent.
        ap.handle_mac_frame(&probe_req[..], None);
        assert_eq!(fake_device.wlan_queue.len(), 1);

        // Frame from the same channel must be processed and a probe response sent.
        let rx_info_same_channel = banjo_wlan_mac::WlanRxInfo {
            chan: WlanChannel { primary: 1, cbw: WlanChannelBandwidth::_20, secondary80: 0 },
            ..rx_info_wrong_channel
        };
        fake_device.wlan_queue.clear();
        ap.handle_mac_frame(&probe_req[..], Some(rx_info_same_channel));
        assert_eq!(fake_device.wlan_queue.len(), 1);
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
            cap: CapabilityInfo(0).raw(),
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
                // TODO(fxbug.dev/40917): Correctly support this.
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
                2,
                CapabilityInfo(0),
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
            cap: CapabilityInfo(0).raw(),
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
                2,
                CapabilityInfo(0),
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
    fn ap_handle_mlme_setkeys_req() {
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
                2,
                CapabilityInfo(0),
                vec![0b11111000],
                1,
                Some(fake_wpa2_rsne()),
            )
            .expect("expected InfraBss::new ok"),
        );

        ap.handle_mlme_setkeys_req(fidl_mlme::SetKeysRequest {
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
        .expect("expected Ap::handle_mlme_setkeys_req OK");
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
    fn ap_handle_mlme_setkeys_req_no_bss() {
        let mut fake_device = FakeDevice::new();
        let mut fake_scheduler = FakeScheduler::new();
        let mut ap = Ap::new(
            fake_device.as_device(),
            FakeBufferProvider::new(),
            fake_scheduler.as_scheduler(),
            BSSID,
        );
        assert_variant!(
            ap.handle_mlme_setkeys_req(fidl_mlme::SetKeysRequest {
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
            .expect_err("expected Ap::handle_mlme_setkeys_req error"),
            Error::Status(_, zx::Status::BAD_STATE)
        );
    }

    #[test]
    fn ap_handle_mlme_setkeys_req_bss_no_rsne() {
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
                2,
                CapabilityInfo(0),
                vec![0b11111000],
                1,
                None,
            )
            .expect("expected InfraBss::new ok"),
        );

        assert_variant!(
            ap.handle_mlme_setkeys_req(fidl_mlme::SetKeysRequest {
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
            .expect_err("expected Ap::handle_mlme_setkeys_req error"),
            Error::Status(_, zx::Status::BAD_STATE)
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
                2,
                CapabilityInfo(0),
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
                4, 4, 4, 4, 4, 4, // addr1
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
                2,
                CapabilityInfo(0),
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
                2,
                CapabilityInfo(0),
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
                4, 4, 4, 4, 4, 4, // addr1
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
                2,
                CapabilityInfo(0),
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
                cap: CapabilityInfo(0).raw(),
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
                4, 4, 4, 4, 4, 4, // addr1
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
                2,
                CapabilityInfo(0),
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
                4, 4, 4, 4, 4, 4, // addr1
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
                2,
                CapabilityInfo(0),
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
                cap: CapabilityInfo(0).raw(),
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
                2,
                CapabilityInfo(0),
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
                4, 4, 4, 4, 4, 4, // addr1
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
