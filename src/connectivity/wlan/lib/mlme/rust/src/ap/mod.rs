// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod remote_client;
mod utils;

use {
    crate::{
        buffer::{BufferProvider, OutBuf},
        device::{Device, TxFlags},
        error::Error,
        write_eth_frame,
    },
    fidl_fuchsia_wlan_mlme as fidl_mlme,
    log::{error, info, log},
    std::{
        collections::{hash_map, HashMap},
        fmt,
    },
    wlan_common::{
        buffer_writer::BufferWriter,
        frame_len,
        mac::{
            self, Aid, AuthAlgorithmNumber, Bssid, MacAddr, OptionalField, Presence, StatusCode,
        },
        sequence::SequenceManager,
    },
    zerocopy::ByteSlice,
};

pub use remote_client::*;
pub use utils::*;

/// Rejection reasons for why a frame was not proceessed.
#[derive(Debug)]
pub enum Rejection {
    /// The frame was for another BSS.
    OtherBss,

    /// The to_ds bit was false, or the from_ds bit was true.
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
            _ => self.fmt(f),
        }
    }
}

impl From<failure::Error> for Rejection {
    fn from(e: failure::Error) -> Rejection {
        Self::Error(e)
    }
}

struct InfraBss {
    // TODO(37891): Consider removing this, as only the SME really needs to know about it.
    pub ssid: Vec<u8>,
    pub is_rsn: bool,
    pub clients: HashMap<MacAddr, RemoteClient>,
}

// TODO(37891): Use this code.
#[allow(dead_code)]
impl InfraBss {
    fn new(ssid: Vec<u8>, is_rsn: bool) -> Self {
        Self { ssid, is_rsn, clients: HashMap::new() }
    }

    fn handle_mgmt_frame<B: ByteSlice>(
        &mut self,
        ctx: &mut Context,
        mgmt_hdr: mac::MgmtHdr,
        body: B,
    ) -> Result<(), Rejection> {
        let mgmt_subtype = *&{ mgmt_hdr.frame_ctrl }.mgmt_subtype();

        if !*&{ mgmt_hdr.frame_ctrl }.to_ds() || *&{ mgmt_hdr.frame_ctrl }.from_ds() {
            // Frame was not sent to a distribution system (e.g. an AP), or was received from
            // another distribution system.
            return Err(Rejection::BadDsBits);
        }

        let to_bss = mgmt_hdr.addr1 == ctx.bssid.0 && mgmt_hdr.addr3 == ctx.bssid.0;

        // IEEE Std 802.11-2016, 11.1.4.3.4: Probe requests may be sent either directly to us or to
        // broadcast.
        if mgmt_subtype == mac::MgmtSubtype::PROBE_REQ {
            if !to_bss && (mgmt_hdr.addr1 != mac::BCAST_ADDR || mgmt_hdr.addr3 != mac::BCAST_ADDR) {
                // Probe request is not for this BSS.
                return Err(Rejection::OtherBss);
            }

            // TODO(37891): Respond to probes.
            return Ok(());
        }

        if !to_bss {
            // Frame is not for this BSS.
            return Err(Rejection::OtherBss);
        }

        let client_addr = mgmt_hdr.addr2;

        let (client, is_new) = match self.clients.entry(client_addr) {
            hash_map::Entry::Occupied(e) => (e.into_mut(), false),

            // If the client is not yet known, and the client is attempting authentication, we can
            // register them (and also report that they're a new client).
            hash_map::Entry::Vacant(e) => {
                if mgmt_subtype != mac::MgmtSubtype::AUTH {
                    return Err(Rejection::NoSuchClient(client_addr));
                }
                (e.insert(RemoteClient::new(client_addr.clone())), true)
            }
        };

        if let Err(e) = client.handle_mgmt_frame(ctx, Some(self.ssid.clone()), mgmt_hdr, body) {
            // If the client is new and we failed to handle a management frame for it, the SME would
            // never have been informed of it, so we need to forget it here.
            if is_new {
                self.clients.remove(&client_addr);
            }
            return Err(Rejection::Client(client_addr, e));
        }

        // The client may have been deauthenticated if a deauthenticate frame was received.
        if client.deauthenticated() {
            self.clients.remove(&client_addr);
        }

        Ok(())
    }

    /// Handles an incoming data frame.
    ///
    ///
    fn handle_data_frame<B: ByteSlice>(
        &mut self,
        ctx: &mut Context,
        fixed_fields: mac::FixedDataHdrFields,
        addr4: Option<mac::Addr4>,
        qos_ctrl: Option<mac::QosControl>,
        body: B,
    ) -> Result<(), Rejection> {
        if mac::data_receiver_addr(&fixed_fields) != ctx.bssid.0 {
            // Frame is not for this BSSID.
            return Err(Rejection::OtherBss);
        }

        if !*&{ fixed_fields.frame_ctrl }.to_ds() || *&{ fixed_fields.frame_ctrl }.from_ds() {
            // Frame was not sent to a distribution system (e.g. an AP), or was received from
            // another distribution system.
            return Err(Rejection::BadDsBits);
        }

        let src_addr = mac::data_src_addr(&fixed_fields, addr4).ok_or(Rejection::NoSrcAddr)?;

        let client = self.clients.get_mut(&src_addr).ok_or(Rejection::NoSuchClient(src_addr))?;

        client
            .handle_data_frame(ctx, fixed_fields, addr4, qos_ctrl, body)
            .map_err(|e| Rejection::Client(client.addr, e))
    }

    fn handle_eth_frame(
        &mut self,
        ctx: &mut Context,
        dst_addr: MacAddr,
        src_addr: MacAddr,
        ether_type: u16,
        body: &[u8],
    ) -> Result<(), Rejection> {
        let client = self.clients.get_mut(&dst_addr).ok_or(Rejection::NoSuchClient(dst_addr))?;

        client
            .handle_eth_frame(ctx, dst_addr, src_addr, ether_type, body)
            .map_err(|e| Rejection::Client(client.addr, e))
    }
}

pub struct Context {
    device: Device,
    buf_provider: BufferProvider,
    seq_mgr: SequenceManager,
    bssid: Bssid,
}

impl Context {
    pub fn new(device: Device, buf_provider: BufferProvider, bssid: Bssid) -> Self {
        Self { device, buf_provider, seq_mgr: SequenceManager::new(), bssid }
    }

    // MLME sender functions.

    /// Sends MLME-AUTHENTICATE.indication (IEEE Std 802.11-2016, 6.3.5.4) to the SME.
    pub fn send_mlme_auth_ind(
        &self,
        peer_sta_address: MacAddr,
        auth_type: fidl_mlme::AuthenticationTypes,
    ) -> Result<(), Error> {
        self.device.access_sme_sender(|sender| {
            sender.send_authenticate_ind(&mut fidl_mlme::AuthenticateIndication {
                peer_sta_address,
                auth_type,
            })
        })
    }

    /// Sends MLME-DEAUTHENTICATE.indication (IEEE Std 802.11-2016, 6.3.6.4) to the SME.
    pub fn send_mlme_deauth_ind(
        &self,
        peer_sta_address: MacAddr,
        reason_code: fidl_mlme::ReasonCode,
    ) -> Result<(), Error> {
        self.device.access_sme_sender(|sender| {
            sender.send_deauthenticate_ind(&mut fidl_mlme::DeauthenticateIndication {
                peer_sta_address,
                reason_code,
            })
        })
    }

    /// Sends MLME-ASSOCIATE.indication (IEEE Std 802.11-2016, 6.3.7.4) to the SME.
    pub fn send_mlme_assoc_ind(
        &self,
        peer_sta_address: MacAddr,
        listen_interval: u16,
        ssid: Option<Vec<u8>>,
        rsne: Option<Vec<u8>>,
    ) -> Result<(), Error> {
        self.device.access_sme_sender(|sender| {
            sender.send_associate_ind(&mut fidl_mlme::AssociateIndication {
                peer_sta_address,
                listen_interval,
                ssid,
                rsne,
                // TODO(37891): Send everything else (e.g. HT capabilities).
            })
        })
    }

    /// Sends MLME-DISASSOCIATE.indication (IEEE Std 802.11-2016, 6.3.9.3) to the SME.
    pub fn send_mlme_disassoc_ind(
        &self,
        peer_sta_address: MacAddr,
        reason_code: u16,
    ) -> Result<(), Error> {
        self.device.access_sme_sender(|sender| {
            sender.send_disassociate_ind(&mut fidl_mlme::DisassociateIndication {
                peer_sta_address,
                reason_code,
            })
        })
    }

    /// Sends EAPOL.indication (fuchsia.wlan.mlme.EapolIndication) to the SME.
    pub fn send_mlme_eapol_ind(
        &self,
        dst_addr: MacAddr,
        src_addr: MacAddr,
        data: &[u8],
    ) -> Result<(), Error> {
        self.device.access_sme_sender(|sender| {
            sender.send_eapol_ind(&mut fidl_mlme::EapolIndication {
                dst_addr,
                src_addr,
                data: data.to_vec(),
            })
        })
    }

    // WLAN frame sender functions.

    /// Sends a WLAN authentication frame (IEEE Std 802.11-2016, 9.3.3.12) to the PHY.
    pub fn send_auth_frame(
        &mut self,
        addr: MacAddr,
        auth_alg_num: AuthAlgorithmNumber,
        auth_txn_seq_num: u16,
        status_code: StatusCode,
    ) -> Result<(), Error> {
        const FRAME_LEN: usize = frame_len!(mac::MgmtHdr, mac::AuthHdr);
        let mut buf = self.buf_provider.get_buffer(FRAME_LEN)?;
        let mut w = BufferWriter::new(&mut buf[..]);
        write_auth_frame(
            &mut w,
            addr,
            self.bssid.clone(),
            &mut self.seq_mgr,
            auth_alg_num,
            auth_txn_seq_num,
            status_code,
        )?;
        let bytes_written = w.bytes_written();
        let out_buf = OutBuf::from(buf, bytes_written);
        self.device
            .send_wlan_frame(out_buf, TxFlags::NONE)
            .map_err(|s| Error::Status(format!("error sending auth frame"), s))
    }

    /// Sends a WLAN association response frame (IEEE Std 802.11-2016, 9.3.3.7) to the PHY.
    fn send_assoc_resp_frame(
        &mut self,
        addr: MacAddr,
        capabilities: mac::CapabilityInfo,
        status_code: StatusCode,
        aid: Aid,
        ies: &[u8],
    ) -> Result<(), Error> {
        let mut buf =
            self.buf_provider.get_buffer(frame_len!(mac::MgmtHdr, mac::AuthHdr) + ies.len())?;
        let mut w = BufferWriter::new(&mut buf[..]);
        write_assoc_resp_frame(
            &mut w,
            addr,
            self.bssid.clone(),
            &mut self.seq_mgr,
            capabilities,
            status_code,
            aid,
            ies,
        )?;
        let bytes_written = w.bytes_written();
        let out_buf = OutBuf::from(buf, bytes_written);
        self.device
            .send_wlan_frame(out_buf, TxFlags::NONE)
            .map_err(|s| Error::Status(format!("error sending open auth frame"), s))
    }

    /// Sends a WLAN deauthentication frame (IEEE Std 802.11-2016, 9.3.3.1) to the PHY.
    fn send_deauth_frame(
        &mut self,
        addr: MacAddr,
        reason_code: mac::ReasonCode,
    ) -> Result<(), Error> {
        let mut buf = self.buf_provider.get_buffer(frame_len!(mac::MgmtHdr, mac::DeauthHdr))?;
        let mut w = BufferWriter::new(&mut buf[..]);
        write_deauth_frame(&mut w, addr, self.bssid.clone(), &mut self.seq_mgr, reason_code)?;
        let bytes_written = w.bytes_written();
        let out_buf = OutBuf::from(buf, bytes_written);
        self.device
            .send_wlan_frame(out_buf, TxFlags::NONE)
            .map_err(|s| Error::Status(format!("error sending open auth frame"), s))
    }

    /// Sends a WLAN disassociation frame (IEEE Std 802.11-2016, 9.3.3.5) to the PHY.
    fn send_disassoc_frame(
        &mut self,
        addr: MacAddr,
        reason_code: mac::ReasonCode,
    ) -> Result<(), Error> {
        let mut buf = self.buf_provider.get_buffer(frame_len!(mac::MgmtHdr, mac::DeauthHdr))?;
        let mut w = BufferWriter::new(&mut buf[..]);
        write_disassoc_frame(&mut w, addr, self.bssid.clone(), &mut self.seq_mgr, reason_code)?;
        let bytes_written = w.bytes_written();
        let out_buf = OutBuf::from(buf, bytes_written);
        self.device
            .send_wlan_frame(out_buf, TxFlags::NONE)
            .map_err(|s| Error::Status(format!("error sending open auth frame"), s))
    }

    /// Sends a WLAN data frame (IEEE Std 802.11-2016, 9.3.2) to the PHY.
    fn send_data_frame(
        &mut self,
        dst_addr: MacAddr,
        src_addr: MacAddr,
        is_protected: bool,
        is_qos: bool,
        ether_type: u16,
        payload: &[u8],
    ) -> Result<(), Error> {
        let qos_presence = Presence::from_bool(is_qos);
        let data_hdr_len =
            mac::FixedDataHdrFields::len(mac::Addr4::ABSENT, qos_presence, mac::HtControl::ABSENT);
        let frame_len = data_hdr_len + std::mem::size_of::<mac::LlcHdr>() + payload.len();
        let mut buf = self.buf_provider.get_buffer(frame_len)?;
        let mut w = BufferWriter::new(&mut buf[..]);
        write_data_frame(
            &mut w,
            &mut self.seq_mgr,
            dst_addr,
            self.bssid.clone(),
            src_addr,
            is_protected,
            is_qos,
            ether_type,
            payload,
        )?;
        let bytes_written = w.bytes_written();
        let out_buf = OutBuf::from(buf, bytes_written);
        self.device
            .send_wlan_frame(
                out_buf,
                match ether_type {
                    mac::ETHER_TYPE_EAPOL => TxFlags::FAVOR_RELIABILITY,
                    _ => TxFlags::NONE,
                },
            )
            .map_err(|s| Error::Status(format!("error sending data frame"), s))
    }

    /// Sends an EAPoL data frame (IEEE Std 802.1X, 11.3) to the PHY.
    fn send_eapol_frame(
        &mut self,
        dst_addr: MacAddr,
        src_addr: MacAddr,
        is_protected: bool,
        eapol_frame: &[u8],
    ) -> Result<(), Error> {
        self.send_data_frame(
            dst_addr,
            src_addr,
            is_protected,
            false, // TODO(37891): Support QoS.
            mac::ETHER_TYPE_EAPOL,
            eapol_frame,
        )
    }

    // Netstack delivery functions.

    /// Delivers the Ethernet II frame to the netstack.
    fn deliver_eth_frame(
        &mut self,
        dst_addr: MacAddr,
        src_addr: MacAddr,
        protocol_id: u16,
        body: &[u8],
    ) -> Result<(), Error> {
        let mut buf = [0u8; mac::MAX_ETH_FRAME_LEN];
        let mut writer = BufferWriter::new(&mut buf[..]);
        write_eth_frame(&mut writer, dst_addr, src_addr, protocol_id, body)?;
        self.device
            .deliver_eth_frame(writer.into_written())
            .map_err(|s| Error::Status(format!("could not deliver Ethernet II frame"), s))
    }
}

pub struct Ap {
    // TODO(37891): Make this private once we no longer need to depend on this in C bindings.
    pub ctx: Context,
    bss: Option<InfraBss>,
}

// TODO(37891): Use this code.
#[allow(dead_code)]
impl Ap {
    pub fn new(device: Device, buf_provider: BufferProvider, bssid: Bssid) -> Self {
        Self { ctx: Context::new(device, buf_provider, bssid), bss: None }
    }

    // MLME handler functions.

    /// Handles MLME.START.request (IEEE Std 802.11-2016, 6.3.11.2) from the SME.
    pub fn handle_mlme_start_req(&mut self, ssid: Vec<u8>) -> Result<(), failure::Error> {
        if self.bss.is_some() {
            info!("MLME-START.request: BSS already started");
            return Ok(());
        }

        // TODO(37891): Support starting a BSS with RSN.
        self.bss.replace(InfraBss::new(ssid, false));

        // TODO(37891): Respond to the SME with status code.

        Ok(())
    }

    pub fn handle_mlme_stop_req(&mut self) -> Result<(), failure::Error> {
        if self.bss.is_none() {
            info!("MLME-STOP.request: BSS not started");
        }

        self.bss = None;

        // TODO(37891): Respond to the SME with status code.

        Ok(())
    }

    fn on_eth_frame(&mut self, dst_addr: MacAddr, src_addr: MacAddr, ether_type: u16, body: &[u8]) {
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

    fn on_mac_frame<B: ByteSlice>(&mut self, bytes: B, body_aligned: bool) {
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
        crate::{buffer::FakeBufferProvider, device::FakeDevice},
        wlan_common::assert_variant,
    };
    const CLIENT_ADDR: MacAddr = [1u8; 6];
    const BSSID: Bssid = Bssid([2u8; 6]);
    const CLIENT_ADDR2: MacAddr = [3u8; 6];

    fn make_context(device: Device) -> Context {
        Context::new(device, FakeBufferProvider::new(), BSSID)
    }

    #[test]
    fn send_mlme_auth_ind() {
        let mut fake_device = FakeDevice::new();
        let ctx = make_context(fake_device.as_device());
        ctx.send_mlme_auth_ind(CLIENT_ADDR, fidl_mlme::AuthenticationTypes::OpenSystem)
            .expect("expected OK");
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
    fn send_mlme_deauth_ind() {
        let mut fake_device = FakeDevice::new();
        let ctx = make_context(fake_device.as_device());
        ctx.send_mlme_deauth_ind(CLIENT_ADDR, fidl_mlme::ReasonCode::LeavingNetworkDeauth)
            .expect("expected OK");
        let msg = fake_device
            .next_mlme_msg::<fidl_mlme::DeauthenticateIndication>()
            .expect("expected MLME message");
        assert_eq!(
            msg,
            fidl_mlme::DeauthenticateIndication {
                peer_sta_address: CLIENT_ADDR,
                reason_code: fidl_mlme::ReasonCode::LeavingNetworkDeauth,
            },
        );
    }

    #[test]
    fn send_mlme_assoc_ind() {
        let mut fake_device = FakeDevice::new();
        let ctx = make_context(fake_device.as_device());
        ctx.send_mlme_assoc_ind(CLIENT_ADDR, 1, Some(b"coolnet".to_vec()), None)
            .expect("expected OK");
        let msg = fake_device
            .next_mlme_msg::<fidl_mlme::AssociateIndication>()
            .expect("expected MLME message");
        assert_eq!(
            msg,
            fidl_mlme::AssociateIndication {
                peer_sta_address: CLIENT_ADDR,
                listen_interval: 1,
                ssid: Some(b"coolnet".to_vec()),
                rsne: None,
            },
        );
    }

    #[test]
    fn send_mlme_disassoc_ind() {
        let mut fake_device = FakeDevice::new();
        let ctx = make_context(fake_device.as_device());
        ctx.send_mlme_disassoc_ind(
            CLIENT_ADDR,
            fidl_mlme::ReasonCode::LeavingNetworkDisassoc as u16,
        )
        .expect("expected OK");
        let msg = fake_device
            .next_mlme_msg::<fidl_mlme::DisassociateIndication>()
            .expect("expected MLME message");
        assert_eq!(
            msg,
            fidl_mlme::DisassociateIndication {
                peer_sta_address: CLIENT_ADDR,
                reason_code: fidl_mlme::ReasonCode::LeavingNetworkDisassoc as u16,
            },
        );
    }

    #[test]
    fn send_mlme_eapol_ind() {
        let mut fake_device = FakeDevice::new();
        let ctx = make_context(fake_device.as_device());
        ctx.send_mlme_eapol_ind(CLIENT_ADDR2, CLIENT_ADDR, &[1, 2, 3, 4, 5][..])
            .expect("expected OK");
        let msg = fake_device
            .next_mlme_msg::<fidl_mlme::EapolIndication>()
            .expect("expected MLME message");
        assert_eq!(
            msg,
            fidl_mlme::EapolIndication {
                dst_addr: CLIENT_ADDR2,
                src_addr: CLIENT_ADDR,
                data: vec![1, 2, 3, 4, 5],
            },
        );
    }

    #[test]
    fn ctx_send_auth_frame() {
        let mut fake_device = FakeDevice::new();
        let mut ctx = make_context(fake_device.as_device());
        ctx.send_auth_frame(
            CLIENT_ADDR,
            AuthAlgorithmNumber::FAST_BSS_TRANSITION,
            3,
            StatusCode::TRANSACTION_SEQUENCE_ERROR,
        )
        .expect("error delivering WLAN frame");
        assert_eq!(fake_device.wlan_queue.len(), 1);
        #[rustfmt::skip]
        assert_eq!(&fake_device.wlan_queue[0].0[..], &[
            // Mgmt header
            0b10110000, 0, // Frame Control
            0, 0, // Duration
            1, 1, 1, 1, 1, 1, // addr1
            2, 2, 2, 2, 2, 2, // addr2
            2, 2, 2, 2, 2, 2, // addr3
            0x10, 0, // Sequence Control
            // Auth header:
            2, 0, // auth algorithm
            3, 0, // auth txn seq num
            14, 0, // Status code
        ][..]);
    }

    #[test]
    fn ctx_send_assoc_resp_frame() {
        let mut fake_device = FakeDevice::new();
        let mut ctx = make_context(fake_device.as_device());
        ctx.send_assoc_resp_frame(
            CLIENT_ADDR,
            mac::CapabilityInfo(0),
            StatusCode::REJECTED_EMERGENCY_SERVICES_NOT_SUPPORTED,
            1,
            &[0, 4, 1, 2, 3, 4],
        )
        .expect("error delivering WLAN frame");
        assert_eq!(fake_device.wlan_queue.len(), 1);
        #[rustfmt::skip]
        assert_eq!(&fake_device.wlan_queue[0].0[..], &[
            // Mgmt header
            0b00010000, 0, // Frame Control
            0, 0, // Duration
            1, 1, 1, 1, 1, 1, // addr1
            2, 2, 2, 2, 2, 2, // addr2
            2, 2, 2, 2, 2, 2, // addr3
            0x10, 0, // Sequence Control
            // Association response header:
            0, 0, // Capabilities
            94, 0, // status code
            1, 0, // AID
            0, 4, 1, 2, 3, 4, // SSID
        ][..]);
    }

    #[test]
    fn ctx_send_disassoc_frame() {
        let mut fake_device = FakeDevice::new();
        let mut ctx = make_context(fake_device.as_device());
        ctx.send_disassoc_frame(CLIENT_ADDR, mac::ReasonCode::LEAVING_NETWORK_DISASSOC)
            .expect("error delivering WLAN frame");
        assert_eq!(fake_device.wlan_queue.len(), 1);
        #[rustfmt::skip]
        assert_eq!(&fake_device.wlan_queue[0].0[..], &[
            // Mgmt header
            0b10100000, 0, // Frame Control
            0, 0, // Duration
            1, 1, 1, 1, 1, 1, // addr1
            2, 2, 2, 2, 2, 2, // addr2
            2, 2, 2, 2, 2, 2, // addr3
            0x10, 0, // Sequence Control
            // Disassoc header:
            8, 0, // reason code
        ][..]);
    }

    #[test]
    fn ctx_send_data_frame() {
        let mut fake_device = FakeDevice::new();
        let mut ctx = make_context(fake_device.as_device());
        ctx.send_data_frame(CLIENT_ADDR2, CLIENT_ADDR, false, false, 0x1234, &[1, 2, 3, 4, 5])
            .expect("error delivering WLAN frame");
        assert_eq!(fake_device.wlan_queue.len(), 1);
        #[rustfmt::skip]
        assert_eq!(&fake_device.wlan_queue[0].0[..], &[
            // Mgmt header
            0b00001000, 0b00000010, // Frame Control
            0, 0, // Duration
            3, 3, 3, 3, 3, 3, // addr1
            2, 2, 2, 2, 2, 2, // addr2
            1, 1, 1, 1, 1, 1, // addr3
            0x10, 0, // Sequence Control
            0xAA, 0xAA, 0x03, // DSAP, SSAP, Control, OUI
            0, 0, 0, // OUI
            0x12, 0x34, // Protocol ID
            // Data
            1, 2, 3, 4, 5,
        ][..]);
    }

    #[test]
    fn ctx_send_eapol_frame() {
        let mut fake_device = FakeDevice::new();
        let mut ctx = make_context(fake_device.as_device());
        ctx.send_eapol_frame(CLIENT_ADDR2, CLIENT_ADDR, false, &[1, 2, 3, 4, 5])
            .expect("error delivering WLAN frame");
        assert_eq!(fake_device.wlan_queue.len(), 1);
        #[rustfmt::skip]
        assert_eq!(&fake_device.wlan_queue[0].0[..], &[
            // Mgmt header
            0b00001000, 0b00000010, // Frame Control
            0, 0, // Duration
            3, 3, 3, 3, 3, 3, // addr1
            2, 2, 2, 2, 2, 2, // addr2
            1, 1, 1, 1, 1, 1, // addr3
            0x10, 0, // Sequence Control
            0xAA, 0xAA, 0x03, // DSAP, SSAP, Control, OUI
            0, 0, 0, // OUI
            0x88, 0x8E, // EAPOL protocol ID
            // Data
            1, 2, 3, 4, 5,
        ][..]);
    }

    #[test]
    fn ctx_deliver_eth_frame() {
        let mut fake_device = FakeDevice::new();
        let mut ctx = make_context(fake_device.as_device());
        ctx.deliver_eth_frame(CLIENT_ADDR2, CLIENT_ADDR, 0x1234, &[1, 2, 3, 4, 5][..])
            .expect("expected OK");
        assert_eq!(fake_device.eth_queue.len(), 1);
        #[rustfmt::skip]
        assert_eq!(&fake_device.eth_queue[0][..], &[
            3, 3, 3, 3, 3, 3,  // dest
            1, 1, 1, 1, 1, 1,  // src
            0x12, 0x34,        // ether_type
            // Data
            1, 2, 3, 4, 5,
        ][..]);
    }

    #[test]
    fn bss_handle_mgmt_frame_auth() {
        let mut fake_device = FakeDevice::new();
        let mut ctx = make_context(fake_device.as_device());
        let mut bss = InfraBss::new(b"coolnet".to_vec(), false);

        bss.handle_mgmt_frame(
            &mut ctx,
            mac::MgmtHdr {
                frame_ctrl: mac::FrameControl(0)
                    .with_frame_type(mac::FrameType::MGMT)
                    .with_mgmt_subtype(mac::MgmtSubtype::AUTH)
                    .with_to_ds(true),
                duration: 0,
                addr1: BSSID.0,
                addr2: CLIENT_ADDR,
                addr3: BSSID.0,
                seq_ctrl: mac::SequenceControl(10),
            },
            &[
                // Auth body
                0, 0, // Auth Algorithm Number
                1, 0, // Auth Txn Seq Number
                0, 0, // Status code
            ][..],
        )
        .expect("expected OK");

        assert_eq!(bss.clients.contains_key(&CLIENT_ADDR), true);

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
    fn bss_handle_mgmt_frame_bad_ds_bits() {
        let mut fake_device = FakeDevice::new();
        let mut ctx = make_context(fake_device.as_device());
        let mut bss = InfraBss::new(b"coolnet".to_vec(), false);

        assert_variant!(
            bss.handle_mgmt_frame(
                &mut ctx,
                mac::MgmtHdr {
                    frame_ctrl: mac::FrameControl(0)
                        .with_frame_type(mac::FrameType::MGMT)
                        .with_mgmt_subtype(mac::MgmtSubtype::AUTH)
                        .with_to_ds(false),
                    duration: 0,
                    addr1: BSSID.0,
                    addr2: CLIENT_ADDR,
                    addr3: BSSID.0,
                    seq_ctrl: mac::SequenceControl(10),
                },
                &[
                    // Auth body
                    0, 0, // Auth Algorithm Number
                    1, 0, // Auth Txn Seq Number
                    0, 0, // Status code
                ][..],
            )
            .expect_err("expected error"),
            Rejection::BadDsBits
        );

        assert_eq!(bss.clients.contains_key(&CLIENT_ADDR), false);
    }

    #[test]
    fn bss_handle_mgmt_frame_no_such_client() {
        let mut fake_device = FakeDevice::new();
        let mut ctx = make_context(fake_device.as_device());
        let mut bss = InfraBss::new(b"coolnet".to_vec(), false);

        assert_variant!(
            bss.handle_mgmt_frame(
                &mut ctx,
                mac::MgmtHdr {
                    frame_ctrl: mac::FrameControl(0)
                        .with_frame_type(mac::FrameType::MGMT)
                        .with_mgmt_subtype(mac::MgmtSubtype::DISASSOC)
                        .with_to_ds(true),
                    duration: 0,
                    addr1: BSSID.0,
                    addr2: CLIENT_ADDR,
                    addr3: BSSID.0,
                    seq_ctrl: mac::SequenceControl(10),
                },
                &[
                    // Disassoc header:
                    8, 0, // reason code
                ][..],
            )
            .expect_err("expected error"),
            Rejection::NoSuchClient(..)
        );

        assert_eq!(bss.clients.contains_key(&CLIENT_ADDR), false);
    }

    #[test]
    fn bss_handle_mgmt_frame_bogus() {
        let mut fake_device = FakeDevice::new();
        let mut ctx = make_context(fake_device.as_device());
        let mut bss = InfraBss::new(b"coolnet".to_vec(), false);

        assert_variant!(
            bss.handle_mgmt_frame(
                &mut ctx,
                mac::MgmtHdr {
                    frame_ctrl: mac::FrameControl(0)
                        .with_frame_type(mac::FrameType::MGMT)
                        .with_mgmt_subtype(mac::MgmtSubtype::AUTH)
                        .with_to_ds(true),
                    duration: 0,
                    addr1: BSSID.0,
                    addr2: CLIENT_ADDR,
                    addr3: BSSID.0,
                    seq_ctrl: mac::SequenceControl(10),
                },
                &[
                // Auth frame should have a header; doesn't.
            ][..],
            )
            .expect_err("expected error"),
            Rejection::Client(_, ClientRejection::ParseFailed)
        );

        assert_eq!(bss.clients.contains_key(&CLIENT_ADDR), false);
    }

    #[test]
    fn bss_handle_data_frame() {
        let mut fake_device = FakeDevice::new();
        let mut ctx = make_context(fake_device.as_device());
        let mut bss = InfraBss::new(b"coolnet".to_vec(), false);

        bss.clients.insert(CLIENT_ADDR, RemoteClient::new(CLIENT_ADDR));
        let client = bss.clients.get_mut(&CLIENT_ADDR).unwrap();

        // Move the client to associated so it can handle data frames.
        client
            .handle_mlme_auth_resp(&mut ctx, fidl_mlme::AuthenticateResultCodes::Success)
            .expect("expected OK");
        client
            .handle_mlme_assoc_resp(
                &mut ctx,
                false,
                mac::CapabilityInfo(0),
                fidl_mlme::AssociateResultCodes::Success,
                1,
                &[][..],
            )
            .expect("expected OK");

        bss.handle_data_frame(
            &mut ctx,
            mac::FixedDataHdrFields {
                frame_ctrl: mac::FrameControl(0)
                    .with_frame_type(mac::FrameType::DATA)
                    .with_to_ds(true),
                duration: 0,
                addr1: BSSID.0.clone(),
                addr2: CLIENT_ADDR,
                addr3: CLIENT_ADDR2,
                seq_ctrl: mac::SequenceControl(10),
            },
            None,
            None,
            &[
                7, 7, 7, // DSAP, SSAP & control
                8, 8, 8, // OUI
                0x12, 0x34, // eth type
                // Trailing bytes
                1, 2, 3, 4, 5,
            ][..],
        )
        .expect("expected OK");

        assert_eq!(fake_device.eth_queue.len(), 1);
        assert_eq!(
            &fake_device.eth_queue[0][..],
            &[
                3, 3, 3, 3, 3, 3, // dest
                1, 1, 1, 1, 1, 1, // src
                0x12, 0x34, // ether_type
                // Data
                1, 2, 3, 4, 5,
            ][..]
        );
    }

    #[test]
    fn bss_handle_data_frame_bad_ds_bits() {
        let mut fake_device = FakeDevice::new();
        let mut ctx = make_context(fake_device.as_device());
        let mut bss = InfraBss::new(b"coolnet".to_vec(), false);

        assert_variant!(
            bss.handle_data_frame(
                &mut ctx,
                mac::FixedDataHdrFields {
                    frame_ctrl: mac::FrameControl(0)
                        .with_frame_type(mac::FrameType::DATA)
                        .with_to_ds(false),
                    duration: 0,
                    addr1: BSSID.0.clone(),
                    addr2: CLIENT_ADDR,
                    addr3: CLIENT_ADDR2,
                    seq_ctrl: mac::SequenceControl(10),
                },
                None,
                None,
                &[
                    7, 7, 7, // DSAP, SSAP & control
                    8, 8, 8, // OUI
                    0x12, 0x34, // eth type
                    // Trailing bytes
                    1, 2, 3, 4, 5,
                ][..],
            )
            .expect_err("expected error"),
            Rejection::BadDsBits
        );

        assert_eq!(fake_device.eth_queue.len(), 0);
    }

    #[test]
    fn bss_handle_data_frame_no_such_client() {
        let mut fake_device = FakeDevice::new();
        let mut ctx = make_context(fake_device.as_device());
        let mut bss = InfraBss::new(b"coolnet".to_vec(), false);

        assert_variant!(
            bss.handle_data_frame(
                &mut ctx,
                mac::FixedDataHdrFields {
                    frame_ctrl: mac::FrameControl(0)
                        .with_frame_type(mac::FrameType::DATA)
                        .with_to_ds(true),
                    duration: 0,
                    addr1: BSSID.0.clone(),
                    addr2: CLIENT_ADDR,
                    addr3: CLIENT_ADDR2,
                    seq_ctrl: mac::SequenceControl(10),
                },
                None,
                None,
                &[
                    7, 7, 7, // DSAP, SSAP & control
                    8, 8, 8, // OUI
                    0x12, 0x34, // eth type
                    // Trailing bytes
                    1, 2, 3, 4, 5,
                ][..],
            )
            .expect_err("expected error"),
            Rejection::NoSuchClient(..)
        );

        assert_eq!(fake_device.eth_queue.len(), 0);
    }

    #[test]
    fn bss_handle_data_frame_client_not_associated() {
        let mut fake_device = FakeDevice::new();
        let mut ctx = make_context(fake_device.as_device());
        let mut bss = InfraBss::new(b"coolnet".to_vec(), false);

        bss.clients.insert(CLIENT_ADDR, RemoteClient::new(CLIENT_ADDR));
        let client = bss.clients.get_mut(&CLIENT_ADDR).unwrap();

        // Move the client to authenticated, but not associated: data frames are still not
        // permitted.
        client
            .handle_mlme_auth_resp(&mut ctx, fidl_mlme::AuthenticateResultCodes::Success)
            .expect("expected OK");

        assert_variant!(
            bss.handle_data_frame(
                &mut ctx,
                mac::FixedDataHdrFields {
                    frame_ctrl: mac::FrameControl(0)
                        .with_frame_type(mac::FrameType::DATA)
                        .with_to_ds(true),
                    duration: 0,
                    addr1: BSSID.0.clone(),
                    addr2: CLIENT_ADDR,
                    addr3: CLIENT_ADDR2,
                    seq_ctrl: mac::SequenceControl(10),
                },
                None,
                None,
                &[
                    7, 7, 7, // DSAP, SSAP & control
                    8, 8, 8, // OUI
                    0x12, 0x34, // eth type
                    // Trailing bytes
                    1, 2, 3, 4, 5,
                ][..],
            )
            .expect_err("expected error"),
            Rejection::Client(_, ClientRejection::NotPermitted)
        );

        assert_eq!(fake_device.eth_queue.len(), 0);
    }

    #[test]
    fn bss_handle_eth_frame_no_rsn() {
        let mut fake_device = FakeDevice::new();
        let mut ctx = make_context(fake_device.as_device());
        let mut bss = InfraBss::new(b"coolnet".to_vec(), false);
        bss.clients.insert(CLIENT_ADDR, RemoteClient::new(CLIENT_ADDR));

        let client = bss.clients.get_mut(&CLIENT_ADDR).unwrap();
        client
            .handle_mlme_auth_resp(&mut ctx, fidl_mlme::AuthenticateResultCodes::Success)
            .expect("expected OK");
        client
            .handle_mlme_assoc_resp(
                &mut ctx,
                false,
                mac::CapabilityInfo(0),
                fidl_mlme::AssociateResultCodes::Success,
                1,
                &[][..],
            )
            .expect("expected OK");
        fake_device.wlan_queue.clear();

        bss.handle_eth_frame(&mut ctx, CLIENT_ADDR, CLIENT_ADDR2, 0x1234, &[1, 2, 3, 4, 5][..])
            .expect("expected OK");

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
    fn bss_handle_eth_frame_is_rsn_eapol_controlled_port_closed() {
        let mut fake_device = FakeDevice::new();
        let mut ctx = make_context(fake_device.as_device());
        let mut bss = InfraBss::new(b"coolnet".to_vec(), true);
        bss.clients.insert(CLIENT_ADDR, RemoteClient::new(CLIENT_ADDR));

        let client = bss.clients.get_mut(&CLIENT_ADDR).unwrap();
        client
            .handle_mlme_auth_resp(&mut ctx, fidl_mlme::AuthenticateResultCodes::Success)
            .expect("expected OK");
        client
            .handle_mlme_assoc_resp(
                &mut ctx,
                true,
                mac::CapabilityInfo(0),
                fidl_mlme::AssociateResultCodes::Success,
                1,
                &[][..],
            )
            .expect("expected OK");
        fake_device.wlan_queue.clear();

        assert_variant!(
            bss.handle_eth_frame(&mut ctx, CLIENT_ADDR, CLIENT_ADDR2, 0x1234, &[1, 2, 3, 4, 5][..])
                .expect_err("expected error"),
            Rejection::Client(_, ClientRejection::ControlledPortClosed)
        );
    }

    #[test]
    fn bss_handle_eth_frame_is_rsn_eapol_controlled_port_open() {
        let mut fake_device = FakeDevice::new();
        let mut ctx = make_context(fake_device.as_device());
        let mut bss = InfraBss::new(b"coolnet".to_vec(), true);
        bss.clients.insert(CLIENT_ADDR, RemoteClient::new(CLIENT_ADDR));

        let client = bss.clients.get_mut(&CLIENT_ADDR).unwrap();
        client
            .handle_mlme_auth_resp(&mut ctx, fidl_mlme::AuthenticateResultCodes::Success)
            .expect("expected OK");
        client
            .handle_mlme_assoc_resp(
                &mut ctx,
                true,
                mac::CapabilityInfo(0),
                fidl_mlme::AssociateResultCodes::Success,
                1,
                &[][..],
            )
            .expect("expected OK");
        fake_device.wlan_queue.clear();

        client
            .handle_mlme_set_controlled_port_req(fidl_mlme::ControlledPortState::Open)
            .expect("expected OK");

        bss.handle_eth_frame(&mut ctx, CLIENT_ADDR, CLIENT_ADDR2, 0x1234, &[1, 2, 3, 4, 5][..])
            .expect("expected OK");

        assert_eq!(fake_device.wlan_queue.len(), 1);
        assert_eq!(
            &fake_device.wlan_queue[0].0[..],
            &[
                // Mgmt header
                0b00001000, 0b01000010, // Frame Control
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
    fn ap_on_eth_frame() {
        let mut fake_device = FakeDevice::new();
        let mut ap = Ap::new(fake_device.as_device(), FakeBufferProvider::new(), BSSID);
        ap.handle_mlme_start_req(b"coolnet".to_vec()).expect("expected OK");
        ap.bss.as_mut().unwrap().clients.insert(CLIENT_ADDR, RemoteClient::new(CLIENT_ADDR));

        let client = ap.bss.as_mut().unwrap().clients.get_mut(&CLIENT_ADDR).unwrap();
        client
            .handle_mlme_auth_resp(&mut ap.ctx, fidl_mlme::AuthenticateResultCodes::Success)
            .expect("expected OK");
        client
            .handle_mlme_assoc_resp(
                &mut ap.ctx,
                false,
                mac::CapabilityInfo(0),
                fidl_mlme::AssociateResultCodes::Success,
                1,
                &[][..],
            )
            .expect("expected OK");
        fake_device.wlan_queue.clear();

        ap.on_eth_frame(CLIENT_ADDR, CLIENT_ADDR2, 0x1234, &[1, 2, 3, 4, 5][..]);

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
    fn ap_on_eth_frame_no_such_client() {
        let mut fake_device = FakeDevice::new();
        let mut ap = Ap::new(fake_device.as_device(), FakeBufferProvider::new(), BSSID);
        ap.handle_mlme_start_req(b"coolnet".to_vec()).expect("expected OK");
        ap.on_eth_frame(CLIENT_ADDR2, CLIENT_ADDR, 0x1234, &[1, 2, 3, 4, 5][..]);
    }

    #[test]
    fn ap_on_mac_frame() {
        let mut fake_device = FakeDevice::new();
        let mut ap = Ap::new(fake_device.as_device(), FakeBufferProvider::new(), BSSID);
        ap.handle_mlme_start_req(b"coolnet".to_vec()).expect("expected OK");
        ap.on_mac_frame(
            &[
                // Mgmt header
                0b10110000, 0b00000001, // Frame Control
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
    fn ap_on_mac_frame_no_such_client() {
        let mut fake_device = FakeDevice::new();
        let mut ap = Ap::new(fake_device.as_device(), FakeBufferProvider::new(), BSSID);
        ap.handle_mlme_start_req(b"coolnet".to_vec()).expect("expected OK");
        ap.on_mac_frame(
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
    fn ap_on_mac_frame_bogus() {
        let mut fake_device = FakeDevice::new();
        let mut ap = Ap::new(fake_device.as_device(), FakeBufferProvider::new(), BSSID);
        ap.handle_mlme_start_req(b"coolnet".to_vec()).expect("expected OK");
        ap.on_mac_frame(&[0][..], false);
    }
}
