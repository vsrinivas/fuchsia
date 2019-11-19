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
        key::KeyConfig,
        timer::{EventId, Scheduler, Timer},
        write_eth_frame,
    },
    banjo_ddk_protocol_wlan_info::{WlanChannel, WlanChannelBandwidth},
    fidl_fuchsia_wlan_mlme as fidl_mlme, fuchsia_zircon as zx,
    log::{error, info, log},
    std::{collections::HashMap, fmt},
    wlan_common::{
        buffer_writer::BufferWriter,
        frame_len,
        mac::{
            self, Aid, AuthAlgorithmNumber, Bssid, CapabilityInfo, MacAddr, OptionalField,
            Presence, StatusCode,
        },
        sequence::SequenceManager,
        TimeUnit,
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

pub struct InfraBss {
    pub ssid: Vec<u8>,
    pub rsne: Option<Vec<u8>>,
    pub beacon_interval: TimeUnit,
    pub rates: Vec<u8>,
    pub channel: u8,
    pub clients: HashMap<MacAddr, RemoteClient>,
}

fn get_client(
    clients: &HashMap<MacAddr, RemoteClient>,
    addr: MacAddr,
) -> Result<&RemoteClient, Error> {
    clients
        .get(&addr)
        .ok_or(Error::Status(format!("client {:02X?} not found", addr), zx::Status::NOT_FOUND))
}

fn get_client_mut(
    clients: &mut HashMap<MacAddr, RemoteClient>,
    addr: MacAddr,
) -> Result<&mut RemoteClient, Error> {
    clients
        .get_mut(&addr)
        .ok_or(Error::Status(format!("client {:02X?} not found", addr), zx::Status::NOT_FOUND))
}

/// Prepends the client's MAC address to an error::Error.
///
/// This will discard any more specific error information (e.g. if it was a FIDL error or a
/// failure::Error error), but will still preserve the underlying zx::Status code.
fn make_client_error(addr: MacAddr, e: Error) -> Error {
    Error::Status(format!("client {:02X?}: {}", addr, e), e.into())
}

impl InfraBss {
    fn try_new(
        ctx: &mut Context,
        ssid: Vec<u8>,
        beacon_interval: TimeUnit,
        rates: Vec<u8>,
        channel: u8,
        rsne: Option<Vec<u8>>,
    ) -> Result<Self, Error> {
        let bss = Self { ssid, rsne, beacon_interval, rates, channel, clients: HashMap::new() };

        ctx.device
            .set_channel(WlanChannel {
                primary: channel,

                // TODO(40917): Correctly support this.
                cbw: WlanChannelBandwidth::_20,
                secondary80: 0,
            })
            .map_err(|s| Error::Status(format!("failed to set channel"), s))?;

        // TODO(37891): Support DTIM.

        // We only support drivers that perform beaconing in hardware, for now.
        let beacon_template = bss.make_beacon_template(ctx.bssid)?;
        ctx.device
            .enable_beaconing(&beacon_template.buf, beacon_template.tim_ele_offset, beacon_interval)
            .map_err(|s| Error::Status(format!("failed to enable beaconing"), s))?;

        Ok(bss)
    }

    pub fn stop(&self, ctx: &mut Context) -> Result<(), Error> {
        ctx.device
            .disable_beaconing()
            .map_err(|s| Error::Status(format!("failed to disable beaconing"), s))
    }

    pub fn handle_mlme_auth_resp(
        &mut self,
        ctx: &mut Context,
        resp: fidl_mlme::AuthenticateResponse,
    ) -> Result<(), Error> {
        let client = get_client_mut(&mut self.clients, resp.peer_sta_address)?;
        client
            .handle_mlme_auth_resp(ctx, resp.result_code)
            .map_err(|e| make_client_error(client.addr, e))
    }

    pub fn handle_mlme_deauth_req(
        &mut self,
        ctx: &mut Context,
        req: fidl_mlme::DeauthenticateRequest,
    ) -> Result<(), Error> {
        let client = get_client_mut(&mut self.clients, req.peer_sta_address)?;
        client
            .handle_mlme_deauth_req(ctx, req.reason_code)
            .map_err(|e| make_client_error(client.addr, e))?;
        if client.deauthenticated() {
            self.clients.remove(&req.peer_sta_address);
        }
        Ok(())
    }

    pub fn handle_mlme_assoc_resp(
        &mut self,
        ctx: &mut Context,
        resp: fidl_mlme::AssociateResponse,
    ) -> Result<(), Error> {
        let client = get_client_mut(&mut self.clients, resp.peer_sta_address)?;
        client
            .handle_mlme_assoc_resp(
                ctx,
                self.rsne.is_some(),
                // TODO(37891): Actually implement capability negotiation.
                mac::CapabilityInfo(0),
                resp.result_code,
                resp.association_id,
                // TODO(37891): Actually implement negotiation for various IEs.
                &[][..],
            )
            .map_err(|e| make_client_error(client.addr, e))
    }

    pub fn handle_mlme_disassoc_req(
        &mut self,
        ctx: &mut Context,
        req: fidl_mlme::DisassociateRequest,
    ) -> Result<(), Error> {
        let client = get_client_mut(&mut self.clients, req.peer_sta_address)?;
        client
            .handle_mlme_disassoc_req(ctx, req.reason_code)
            .map_err(|e| make_client_error(client.addr, e))
    }

    pub fn handle_mlme_set_controlled_port_req(
        &mut self,
        req: fidl_mlme::SetControlledPortRequest,
    ) -> Result<(), Error> {
        let client = get_client_mut(&mut self.clients, req.peer_sta_address)?;
        client
            .handle_mlme_set_controlled_port_req(req.state)
            .map_err(|e| make_client_error(client.addr, e))
    }

    pub fn handle_mlme_eapol_req(
        &self,
        ctx: &mut Context,
        req: fidl_mlme::EapolRequest,
    ) -> Result<(), Error> {
        let client = get_client(&self.clients, req.dst_addr)?;
        client
            .handle_mlme_eapol_req(ctx, req.src_addr, &req.data)
            .map_err(|e| make_client_error(client.addr, e))
    }

    fn make_beacon_template(&self, bssid: Bssid) -> Result<BeaconTemplate, Error> {
        make_beacon_template(
            bssid,
            self.beacon_interval,
            CapabilityInfo(0)
                // IEEE Std 802.11-2016, 9.4.1.4: An AP sets the ESS subfield to 1 and the IBSS
                // subfield to 0 within transmitted Beacon or Probe Response frames.
                .with_ess(true)
                .with_ibss(false)
                // IEEE Std 802.11-2016, 9.4.1.4: An AP sets the Privacy subfield to 1 within
                // transmitted Beacon, Probe Response, (Re)Association Response frames if data
                // confidentiality is required for all Data frames exchanged within the BSS.
                .with_privacy(self.rsne.is_some()),
            &self.ssid,
            &self.rates,
            self.channel,
            self.rsne.as_ref().map_or(&[], |rsne| &rsne),
        )
    }

    fn handle_mgmt_frame<B: ByteSlice>(
        &mut self,
        ctx: &mut Context,
        mgmt_hdr: mac::MgmtHdr,
        body: B,
    ) -> Result<(), Rejection> {
        if mgmt_hdr.addr1 != ctx.bssid.0 || mgmt_hdr.addr3 != ctx.bssid.0 {
            // Frame is not for this BSS.
            return Err(Rejection::OtherBss);
        }

        if *&{ mgmt_hdr.frame_ctrl }.to_ds() || *&{ mgmt_hdr.frame_ctrl }.from_ds() {
            // IEEE Std 802.11-2016, 9.2.4.1.4 and Table 9-4: The To DS bit is only set for QMF
            // (QoS Management frame) management frames, and the From DS bit is reserved.
            return Err(Rejection::BadDsBits);
        }

        let client_addr = mgmt_hdr.addr2;

        // We might allocate a client into the Option if there is none present in the map. We do not
        // allocate directly into the map as we do not know yet if the client will even be added
        // (e.g. if the frame being handled is bogus, or the client did not even authenticate).
        let mut new_client = None;
        let client = match self.clients.get_mut(&client_addr) {
            Some(client) => client,
            None => new_client.get_or_insert(RemoteClient::new(client_addr)),
        };

        if let Err(e) = client.handle_mgmt_frame(ctx, Some(self.ssid.clone()), mgmt_hdr, body) {
            return Err(Rejection::Client(client_addr, e));
        }

        if client.deauthenticated() {
            if new_client.is_none() {
                // The client needs to be removed from the map, as it was not freshly allocated from
                // handling this frame.
                self.clients.remove(&client_addr);
            }
        } else {
            // The client was successfully authenticated! Remember it here.
            if let Some(client) = new_client.take() {
                self.clients.insert(client_addr, client);
            }
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
            // IEEE Std 802.11-2016, 9.2.4.1.4 and Table 9-3: Frame was not sent to a distribution
            // system (e.g. an AP), or was received from another distribution system.
            return Err(Rejection::BadDsBits);
        }

        let src_addr = mac::data_src_addr(&fixed_fields, addr4).ok_or(Rejection::NoSrcAddr)?;

        // Handle the frame, pretending that the client is an unauthenticated client if we don't
        // know about it.
        let mut maybe_client = None;
        let client = self
            .clients
            .get_mut(&src_addr)
            .unwrap_or_else(|| maybe_client.get_or_insert(RemoteClient::new(src_addr)));
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
        // Handle the frame, pretending that the client is an unauthenticated client if we don't
        // know about it.
        let mut maybe_client = None;
        let client = self
            .clients
            .get_mut(&dst_addr)
            .unwrap_or_else(|| maybe_client.get_or_insert(RemoteClient::new(dst_addr)));
        client
            .handle_eth_frame(ctx, dst_addr, src_addr, ether_type, body)
            .map_err(|e| Rejection::Client(client.addr, e))
    }

    // Timed event functions

    /// Handles timed events.
    fn handle_timed_event(
        &mut self,
        ctx: &mut Context,
        event_id: EventId,
        event: TimedEvent,
    ) -> Result<(), Rejection> {
        match event {
            TimedEvent::ClientEvent(addr, event) => {
                let client = self.clients.get_mut(&addr).ok_or(Rejection::NoSuchClient(addr))?;

                client
                    .handle_event(ctx, event_id, event)
                    .map_err(|e| Rejection::Client(client.addr, e))
            }
        }
    }
}

#[derive(Debug)]
pub enum ClientEvent {
    /// This is the timeout that fires after dot11BssMaxIdlePeriod (IEEE Std 802.11-2016, 11.24.13
    /// and Annex C.3) elapses and no activity was detected, at which point the client is
    /// disassociated.
    BssIdleTimeout,
}

#[derive(Debug)]
pub enum TimedEvent {
    /// Events that are destined for a client to handle.
    ClientEvent(MacAddr, ClientEvent),
}

pub struct Context {
    device: Device,
    buf_provider: BufferProvider,
    timer: Timer<TimedEvent>,
    seq_mgr: SequenceManager,
    bssid: Bssid,
}

impl Context {
    pub fn new(
        device: Device,
        buf_provider: BufferProvider,
        timer: Timer<TimedEvent>,
        bssid: Bssid,
    ) -> Self {
        Self { device, timer, buf_provider, seq_mgr: SequenceManager::new(), bssid }
    }

    pub fn schedule_after(&mut self, duration: zx::Duration, event: TimedEvent) -> EventId {
        self.timer.schedule_event(self.timer.now() + duration, event)
    }

    pub fn cancel_event(&mut self, event_id: EventId) {
        self.timer.cancel_event(event_id)
    }

    // MLME sender functions.

    /// Sends MLME-START.confirm (IEEE Std 802.11-2016, 6.3.11.3) to the SME.
    pub fn send_mlme_start_conf(
        &self,
        result_code: fidl_mlme::StartResultCodes,
    ) -> Result<(), Error> {
        self.device.access_sme_sender(|sender| {
            sender.send_start_conf(&mut fidl_mlme::StartConfirm { result_code })
        })
    }

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
        protected: bool,
        qos_ctrl: bool,
        ether_type: u16,
        payload: &[u8],
    ) -> Result<(), Error> {
        let qos_presence = Presence::from_bool(qos_ctrl);
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
            protected,
            qos_ctrl,
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

        self.bss.replace(InfraBss::try_new(
            &mut self.ctx,
            req.ssid.clone(),
            req.beacon_period.into(),
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
        fidl_fuchsia_wlan_common as fidl_common,
        wlan_common::{assert_variant, test_utils::fake_frames::fake_wpa2_rsne},
    };
    const CLIENT_ADDR: MacAddr = [1u8; 6];
    const BSSID: Bssid = Bssid([2u8; 6]);
    const CLIENT_ADDR2: MacAddr = [3u8; 6];

    fn make_context(device: Device, scheduler: Scheduler) -> Context {
        Context::new(device, FakeBufferProvider::new(), Timer::<TimedEvent>::new(scheduler), BSSID)
    }

    #[test]
    fn send_mlme_auth_ind() {
        let mut fake_device = FakeDevice::new();
        let mut fake_scheduler = FakeScheduler::new();
        let ctx = make_context(fake_device.as_device(), fake_scheduler.as_scheduler());
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
        let mut fake_scheduler = FakeScheduler::new();
        let ctx = make_context(fake_device.as_device(), fake_scheduler.as_scheduler());
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
        let mut fake_scheduler = FakeScheduler::new();
        let ctx = make_context(fake_device.as_device(), fake_scheduler.as_scheduler());
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
        let mut fake_scheduler = FakeScheduler::new();
        let ctx = make_context(fake_device.as_device(), fake_scheduler.as_scheduler());
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
        let mut fake_scheduler = FakeScheduler::new();
        let ctx = make_context(fake_device.as_device(), fake_scheduler.as_scheduler());
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
    fn ctx_schedule_after() {
        let mut fake_device = FakeDevice::new();
        let mut fake_scheduler = FakeScheduler::new();
        let mut ctx = make_context(fake_device.as_device(), fake_scheduler.as_scheduler());
        let event_id = ctx.schedule_after(
            zx::Duration::from_seconds(5),
            TimedEvent::ClientEvent([1, 1, 1, 1, 1, 1], ClientEvent::BssIdleTimeout),
        );
        assert_variant!(
            ctx.timer.triggered(&event_id),
            Some(TimedEvent::ClientEvent([1, 1, 1, 1, 1, 1], ClientEvent::BssIdleTimeout))
        );
        assert_variant!(ctx.timer.triggered(&event_id), None);
    }

    #[test]
    fn ctx_cancel_event() {
        let mut fake_device = FakeDevice::new();
        let mut fake_scheduler = FakeScheduler::new();
        let mut ctx = make_context(fake_device.as_device(), fake_scheduler.as_scheduler());
        let event_id = ctx.schedule_after(
            zx::Duration::from_seconds(5),
            TimedEvent::ClientEvent([1, 1, 1, 1, 1, 1], ClientEvent::BssIdleTimeout),
        );
        ctx.cancel_event(event_id);
        assert_variant!(ctx.timer.triggered(&event_id), None);
    }

    #[test]
    fn ctx_send_auth_frame() {
        let mut fake_device = FakeDevice::new();
        let mut fake_scheduler = FakeScheduler::new();
        let mut ctx = make_context(fake_device.as_device(), fake_scheduler.as_scheduler());
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
        let mut fake_scheduler = FakeScheduler::new();
        let mut ctx = make_context(fake_device.as_device(), fake_scheduler.as_scheduler());
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
        let mut fake_scheduler = FakeScheduler::new();
        let mut ctx = make_context(fake_device.as_device(), fake_scheduler.as_scheduler());
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
        let mut fake_scheduler = FakeScheduler::new();
        let mut ctx = make_context(fake_device.as_device(), fake_scheduler.as_scheduler());
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
        let mut fake_scheduler = FakeScheduler::new();
        let mut ctx = make_context(fake_device.as_device(), fake_scheduler.as_scheduler());
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
        let mut fake_scheduler = FakeScheduler::new();
        let mut ctx = make_context(fake_device.as_device(), fake_scheduler.as_scheduler());
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
    fn bss_try_new() {
        let mut fake_device = FakeDevice::new();
        let mut fake_scheduler = FakeScheduler::new();
        let mut ctx = make_context(fake_device.as_device(), fake_scheduler.as_scheduler());
        InfraBss::try_new(
            &mut ctx,
            vec![1, 2, 3, 4, 5],
            TimeUnit::DEFAULT_BEACON_INTERVAL,
            vec![0b11111000],
            1,
            None,
        )
        .expect("expected InfraBss::try_new ok");

        assert_eq!(
            fake_device.wlan_channel,
            WlanChannel {
                primary: 1,
                cbw: WlanChannelBandwidth::_20,
                secondary80: 0,
            }
        );

        let beacon_tmpl = vec![
            // Mgmt header
            0b10000000, 0, // Frame Control
            0, 0, // Duration
            255, 255, 255, 255, 255, 255, // addr1
            2, 2, 2, 2, 2, 2, // addr2
            2, 2, 2, 2, 2, 2, // addr3
            0, 0, // Sequence Control
            // Beacon header:
            0, 0, 0, 0, 0, 0, 0, 0, // Timestamp
            100, 0, // Beacon interval
            1, 0, // Capabilities
            // IEs:
            0, 5, 1, 2, 3, 4, 5, // SSID
            1, 1, 0b11111000, // Basic rates
            3, 1, 1, // DSSS parameter set
            5, 0, // TIM
        ];

        assert_eq!(
            fake_device.bcn_cfg.expect("expected bcn_cfg"),
            (beacon_tmpl, 49, TimeUnit::DEFAULT_BEACON_INTERVAL)
        );
    }

    #[test]
    fn bss_stop() {
        let mut fake_device = FakeDevice::new();
        let mut fake_scheduler = FakeScheduler::new();
        let mut ctx = make_context(fake_device.as_device(), fake_scheduler.as_scheduler());
        let bss = InfraBss::try_new(
            &mut ctx,
            vec![1, 2, 3, 4, 5],
            TimeUnit::DEFAULT_BEACON_INTERVAL,
            vec![0b11111000],
            1,
            None,
        )
        .expect("expected InfraBss::try_new ok");
        bss.stop(&mut ctx).expect("expected InfraBss::stop ok");
        assert!(fake_device.bcn_cfg.is_none());
    }

    #[test]
    fn bss_handle_mlme_auth_resp() {
        let mut fake_device = FakeDevice::new();
        let mut fake_scheduler = FakeScheduler::new();
        let mut ctx = make_context(fake_device.as_device(), fake_scheduler.as_scheduler());
        let mut bss = InfraBss::try_new(
            &mut ctx,
            b"coolnet".to_vec(),
            TimeUnit::DEFAULT_BEACON_INTERVAL,
            vec![0b11111000],
            1,
            None,
        )
        .expect("expected InfraBss::try_new ok");

        bss.clients.insert(CLIENT_ADDR, RemoteClient::new(CLIENT_ADDR));

        bss.handle_mlme_auth_resp(
            &mut ctx,
            fidl_mlme::AuthenticateResponse {
                peer_sta_address: CLIENT_ADDR,
                result_code: fidl_mlme::AuthenticateResultCodes::AntiCloggingTokenRequired,
            },
        )
        .expect("expected InfraBss::handle_mlme_auth_resp ok");
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
    fn bss_handle_mlme_auth_resp_no_such_client() {
        let mut fake_device = FakeDevice::new();
        let mut fake_scheduler = FakeScheduler::new();
        let mut ctx = make_context(fake_device.as_device(), fake_scheduler.as_scheduler());
        let mut bss = InfraBss::try_new(
            &mut ctx,
            b"coolnet".to_vec(),
            TimeUnit::DEFAULT_BEACON_INTERVAL,
            vec![0b11111000],
            1,
            None,
        )
        .expect("expected InfraBss::try_new ok");

        assert_eq!(
            zx::Status::from(
                bss.handle_mlme_auth_resp(
                    &mut ctx,
                    fidl_mlme::AuthenticateResponse {
                        peer_sta_address: CLIENT_ADDR,
                        result_code: fidl_mlme::AuthenticateResultCodes::AntiCloggingTokenRequired,
                    },
                )
                .expect_err("expected InfraBss::handle_mlme_auth_resp error")
            ),
            zx::Status::NOT_FOUND
        );
    }

    #[test]
    fn bss_handle_mlme_deauth_req() {
        let mut fake_device = FakeDevice::new();
        let mut fake_scheduler = FakeScheduler::new();
        let mut ctx = make_context(fake_device.as_device(), fake_scheduler.as_scheduler());
        let mut bss = InfraBss::try_new(
            &mut ctx,
            b"coolnet".to_vec(),
            TimeUnit::DEFAULT_BEACON_INTERVAL,
            vec![0b11111000],
            1,
            None,
        )
        .expect("expected InfraBss::try_new ok");

        bss.clients.insert(CLIENT_ADDR, RemoteClient::new(CLIENT_ADDR));

        bss.handle_mlme_deauth_req(
            &mut ctx,
            fidl_mlme::DeauthenticateRequest {
                peer_sta_address: CLIENT_ADDR,
                reason_code: fidl_mlme::ReasonCode::LeavingNetworkDeauth,
            },
        )
        .expect("expected InfraBss::handle_mlme_deauth_req ok");
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

        assert!(!bss.clients.contains_key(&CLIENT_ADDR));
    }

    #[test]
    fn bss_handle_mlme_assoc_resp() {
        let mut fake_device = FakeDevice::new();
        let mut fake_scheduler = FakeScheduler::new();
        let mut ctx = make_context(fake_device.as_device(), fake_scheduler.as_scheduler());
        let mut bss = InfraBss::try_new(
            &mut ctx,
            b"coolnet".to_vec(),
            TimeUnit::DEFAULT_BEACON_INTERVAL,
            vec![0b11111000],
            1,
            None,
        )
        .expect("expected InfraBss::try_new ok");

        bss.clients.insert(CLIENT_ADDR, RemoteClient::new(CLIENT_ADDR));

        bss.handle_mlme_assoc_resp(
            &mut ctx,
            fidl_mlme::AssociateResponse {
                peer_sta_address: CLIENT_ADDR,
                result_code: fidl_mlme::AssociateResultCodes::Success,
                association_id: 1,
            },
        )
        .expect("expected InfraBss::handle_mlme_assoc_resp ok");
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
            ][..]
        );
    }

    #[test]
    fn bss_handle_mlme_disassoc_req() {
        let mut fake_device = FakeDevice::new();
        let mut fake_scheduler = FakeScheduler::new();
        let mut ctx = make_context(fake_device.as_device(), fake_scheduler.as_scheduler());
        let mut bss = InfraBss::try_new(
            &mut ctx,
            b"coolnet".to_vec(),
            TimeUnit::DEFAULT_BEACON_INTERVAL,
            vec![0b11111000],
            1,
            None,
        )
        .expect("expected InfraBss::try_new ok");

        bss.clients.insert(CLIENT_ADDR, RemoteClient::new(CLIENT_ADDR));

        bss.handle_mlme_disassoc_req(
            &mut ctx,
            fidl_mlme::DisassociateRequest {
                peer_sta_address: CLIENT_ADDR,
                reason_code: fidl_mlme::ReasonCode::LeavingNetworkDisassoc as u16,
            },
        )
        .expect("expected InfraBss::handle_mlme_disassoc_req ok");
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
    fn bss_handle_mlme_set_controlled_port_req() {
        let mut fake_device = FakeDevice::new();
        let mut fake_scheduler = FakeScheduler::new();
        let mut ctx = make_context(fake_device.as_device(), fake_scheduler.as_scheduler());
        let mut bss = InfraBss::try_new(
            &mut ctx,
            b"coolnet".to_vec(),
            TimeUnit::DEFAULT_BEACON_INTERVAL,
            vec![0b11111000],
            1,
            Some(fake_wpa2_rsne()),
        )
        .expect("expected InfraBss::try_new ok");

        bss.clients.insert(CLIENT_ADDR, RemoteClient::new(CLIENT_ADDR));

        bss.handle_mlme_assoc_resp(
            &mut ctx,
            fidl_mlme::AssociateResponse {
                peer_sta_address: CLIENT_ADDR,
                result_code: fidl_mlme::AssociateResultCodes::Success,
                association_id: 1,
            },
        )
        .expect("expected InfraBss::handle_mlme_assoc_resp ok");

        bss.handle_mlme_set_controlled_port_req(fidl_mlme::SetControlledPortRequest {
            peer_sta_address: CLIENT_ADDR,
            state: fidl_mlme::ControlledPortState::Open,
        })
        .expect("expected InfraBss::handle_mlme_set_controlled_port_req ok");
    }

    #[test]
    fn bss_handle_mlme_eapol_req() {
        let mut fake_device = FakeDevice::new();
        let mut fake_scheduler = FakeScheduler::new();
        let mut ctx = make_context(fake_device.as_device(), fake_scheduler.as_scheduler());
        let mut bss = InfraBss::try_new(
            &mut ctx,
            b"coolnet".to_vec(),
            TimeUnit::DEFAULT_BEACON_INTERVAL,
            vec![0b11111000],
            1,
            None,
        )
        .expect("expected InfraBss::try_new ok");

        bss.clients.insert(CLIENT_ADDR, RemoteClient::new(CLIENT_ADDR));

        bss.handle_mlme_eapol_req(
            &mut ctx,
            fidl_mlme::EapolRequest {
                dst_addr: CLIENT_ADDR,
                src_addr: BSSID.0,
                data: vec![1, 2, 3],
            },
        )
        .expect("expected InfraBss::handle_mlme_eapol_req ok");
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
    fn bss_handle_mgmt_frame_auth() {
        let mut fake_device = FakeDevice::new();
        let mut fake_scheduler = FakeScheduler::new();
        let mut ctx = make_context(fake_device.as_device(), fake_scheduler.as_scheduler());
        let mut bss = InfraBss::try_new(
            &mut ctx,
            b"coolnet".to_vec(),
            TimeUnit::DEFAULT_BEACON_INTERVAL,
            vec![0b11111000],
            1,
            None,
        )
        .expect("expected InfraBss::try_new ok");

        bss.handle_mgmt_frame(
            &mut ctx,
            mac::MgmtHdr {
                frame_ctrl: mac::FrameControl(0)
                    .with_frame_type(mac::FrameType::MGMT)
                    .with_mgmt_subtype(mac::MgmtSubtype::AUTH),
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
    fn bss_handle_mgmt_frame_bad_ds_bits_to_ds() {
        let mut fake_device = FakeDevice::new();
        let mut fake_scheduler = FakeScheduler::new();
        let mut ctx = make_context(fake_device.as_device(), fake_scheduler.as_scheduler());
        let mut bss = InfraBss::try_new(
            &mut ctx,
            b"coolnet".to_vec(),
            TimeUnit::DEFAULT_BEACON_INTERVAL,
            vec![0b11111000],
            1,
            None,
        )
        .expect("expected InfraBss::try_new ok");

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
    fn bss_handle_mgmt_frame_bad_ds_bits_from_ds() {
        let mut fake_device = FakeDevice::new();
        let mut fake_scheduler = FakeScheduler::new();
        let mut ctx = make_context(fake_device.as_device(), fake_scheduler.as_scheduler());
        let mut bss = InfraBss::try_new(
            &mut ctx,
            b"coolnet".to_vec(),
            TimeUnit::DEFAULT_BEACON_INTERVAL,
            vec![0b11111000],
            1,
            None,
        )
        .expect("expected InfraBss::try_new ok");

        assert_variant!(
            bss.handle_mgmt_frame(
                &mut ctx,
                mac::MgmtHdr {
                    frame_ctrl: mac::FrameControl(0)
                        .with_frame_type(mac::FrameType::MGMT)
                        .with_mgmt_subtype(mac::MgmtSubtype::AUTH)
                        .with_from_ds(true),
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
        let mut fake_scheduler = FakeScheduler::new();
        let mut ctx = make_context(fake_device.as_device(), fake_scheduler.as_scheduler());
        let mut bss = InfraBss::try_new(
            &mut ctx,
            b"coolnet".to_vec(),
            TimeUnit::DEFAULT_BEACON_INTERVAL,
            vec![0b11111000],
            1,
            None,
        )
        .expect("expected InfraBss::try_new ok");

        assert_variant!(
            bss.handle_mgmt_frame(
                &mut ctx,
                mac::MgmtHdr {
                    frame_ctrl: mac::FrameControl(0)
                        .with_frame_type(mac::FrameType::MGMT)
                        .with_mgmt_subtype(mac::MgmtSubtype::DISASSOC),
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
            Rejection::Client(_, ClientRejection::NotPermitted)
        );

        assert_eq!(bss.clients.contains_key(&CLIENT_ADDR), false);
    }

    #[test]
    fn bss_handle_mgmt_frame_bogus() {
        let mut fake_device = FakeDevice::new();
        let mut fake_scheduler = FakeScheduler::new();
        let mut ctx = make_context(fake_device.as_device(), fake_scheduler.as_scheduler());
        let mut bss = InfraBss::try_new(
            &mut ctx,
            b"coolnet".to_vec(),
            TimeUnit::DEFAULT_BEACON_INTERVAL,
            vec![0b11111000],
            1,
            None,
        )
        .expect("expected InfraBss::try_new ok");

        assert_variant!(
            bss.handle_mgmt_frame(
                &mut ctx,
                mac::MgmtHdr {
                    frame_ctrl: mac::FrameControl(0)
                        .with_frame_type(mac::FrameType::MGMT)
                        .with_mgmt_subtype(mac::MgmtSubtype::AUTH),
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
        let mut fake_scheduler = FakeScheduler::new();
        let mut ctx = make_context(fake_device.as_device(), fake_scheduler.as_scheduler());
        let mut bss = InfraBss::try_new(
            &mut ctx,
            b"coolnet".to_vec(),
            TimeUnit::DEFAULT_BEACON_INTERVAL,
            vec![0b11111000],
            1,
            None,
        )
        .expect("expected InfraBss::try_new ok");

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
        let mut fake_scheduler = FakeScheduler::new();
        let mut ctx = make_context(fake_device.as_device(), fake_scheduler.as_scheduler());
        let mut bss = InfraBss::try_new(
            &mut ctx,
            b"coolnet".to_vec(),
            TimeUnit::DEFAULT_BEACON_INTERVAL,
            vec![0b11111000],
            1,
            None,
        )
        .expect("expected InfraBss::try_new ok");

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
    fn bss_handle_client_event() {
        let mut fake_device = FakeDevice::new();
        let mut fake_scheduler = FakeScheduler::new();
        let mut ctx = make_context(fake_device.as_device(), fake_scheduler.as_scheduler());
        let mut bss = InfraBss::try_new(
            &mut ctx,
            b"coolnet".to_vec(),
            TimeUnit::DEFAULT_BEACON_INTERVAL,
            vec![0b11111000],
            1,
            None,
        )
        .expect("expected InfraBss::try_new ok");

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

        fake_device.wlan_queue.clear();

        bss.handle_timed_event(
            &mut ctx,
            fake_scheduler.next_id.into(),
            TimedEvent::ClientEvent(CLIENT_ADDR, ClientEvent::BssIdleTimeout),
        )
        .expect("expected OK");

        // Check that we received a disassociation frame.
        assert_eq!(fake_device.wlan_queue.len(), 1);
        #[rustfmt::skip]
        assert_eq!(&fake_device.wlan_queue[0].0[..], &[
            // Mgmt header
            0b10100000, 0, // Frame Control
            0, 0, // Duration
            1, 1, 1, 1, 1, 1, // addr1
            2, 2, 2, 2, 2, 2, // addr2
            2, 2, 2, 2, 2, 2, // addr3
            0x30, 0, // Sequence Control
            // Disassoc header:
            4, 0, // reason code
        ][..]);

        let msg = fake_device
            .next_mlme_msg::<fidl_mlme::DisassociateIndication>()
            .expect("expected MLME message");
        assert_eq!(
            msg,
            fidl_mlme::DisassociateIndication {
                peer_sta_address: CLIENT_ADDR,
                reason_code: fidl_mlme::ReasonCode::ReasonInactivity as u16,
            },
        );
    }

    #[test]
    fn bss_handle_data_frame_no_such_client() {
        let mut fake_device = FakeDevice::new();
        let mut fake_scheduler = FakeScheduler::new();
        let mut ctx = make_context(fake_device.as_device(), fake_scheduler.as_scheduler());
        let mut bss = InfraBss::try_new(
            &mut ctx,
            b"coolnet".to_vec(),
            TimeUnit::DEFAULT_BEACON_INTERVAL,
            vec![0b11111000],
            1,
            None,
        )
        .expect("expected InfraBss::try_new ok");

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

        assert_eq!(fake_device.wlan_queue.len(), 1);
        assert_eq!(
            fake_device.wlan_queue[0].0,
            &[
                // Mgmt header
                0b11000000, 0b00000000, // Frame Control
                0, 0, // Duration
                1, 1, 1, 1, 1, 1, // addr1
                2, 2, 2, 2, 2, 2, // addr2
                2, 2, 2, 2, 2, 2, // addr3
                0x10, 0, // Sequence Control
                // Disassoc header:
                7, 0, // reason code
            ][..]
        );
    }

    #[test]
    fn bss_handle_data_frame_client_not_associated() {
        let mut fake_device = FakeDevice::new();
        let mut fake_scheduler = FakeScheduler::new();
        let mut ctx = make_context(fake_device.as_device(), fake_scheduler.as_scheduler());
        let mut bss = InfraBss::try_new(
            &mut ctx,
            b"coolnet".to_vec(),
            TimeUnit::DEFAULT_BEACON_INTERVAL,
            vec![0b11111000],
            1,
            None,
        )
        .expect("expected InfraBss::try_new ok");

        bss.clients.insert(CLIENT_ADDR, RemoteClient::new(CLIENT_ADDR));
        let client = bss.clients.get_mut(&CLIENT_ADDR).unwrap();

        // Move the client to authenticated, but not associated: data frames are still not
        // permitted.
        client
            .handle_mlme_auth_resp(&mut ctx, fidl_mlme::AuthenticateResultCodes::Success)
            .expect("expected OK");

        fake_device.wlan_queue.clear();

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

        assert_eq!(fake_device.wlan_queue.len(), 1);
        assert_eq!(
            fake_device.wlan_queue[0].0,
            &[
                // Mgmt header
                0b10100000, 0b00000000, // Frame Control
                0, 0, // Duration
                1, 1, 1, 1, 1, 1, // addr1
                2, 2, 2, 2, 2, 2, // addr2
                2, 2, 2, 2, 2, 2, // addr3
                0x20, 0, // Sequence Control
                // Disassoc header:
                7, 0, // reason code
            ][..]
        );
    }

    #[test]
    fn bss_handle_eth_frame_no_rsn() {
        let mut fake_device = FakeDevice::new();
        let mut fake_scheduler = FakeScheduler::new();
        let mut ctx = make_context(fake_device.as_device(), fake_scheduler.as_scheduler());
        let mut bss = InfraBss::try_new(
            &mut ctx,
            b"coolnet".to_vec(),
            TimeUnit::DEFAULT_BEACON_INTERVAL,
            vec![0b11111000],
            1,
            None,
        )
        .expect("expected InfraBss::try_new ok");
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
    fn bss_handle_eth_frame_no_client() {
        let mut fake_device = FakeDevice::new();
        let mut fake_scheduler = FakeScheduler::new();
        let mut ctx = make_context(fake_device.as_device(), fake_scheduler.as_scheduler());
        let mut bss = InfraBss::try_new(
            &mut ctx,
            b"coolnet".to_vec(),
            TimeUnit::DEFAULT_BEACON_INTERVAL,
            vec![0b11111000],
            1,
            None,
        )
        .expect("expected InfraBss::try_new ok");

        assert_variant!(
            bss.handle_eth_frame(&mut ctx, CLIENT_ADDR, CLIENT_ADDR2, 0x1234, &[1, 2, 3, 4, 5][..])
                .expect_err("expected error"),
            Rejection::Client(_, ClientRejection::NotAssociated)
        );

        assert_eq!(fake_device.wlan_queue.len(), 0);
    }

    #[test]
    fn bss_handle_eth_frame_is_rsn_eapol_controlled_port_closed() {
        let mut fake_device = FakeDevice::new();
        let mut fake_scheduler = FakeScheduler::new();
        let mut ctx = make_context(fake_device.as_device(), fake_scheduler.as_scheduler());
        let mut bss = InfraBss::try_new(
            &mut ctx,
            b"coolnet".to_vec(),
            TimeUnit::DEFAULT_BEACON_INTERVAL,
            vec![0b11111000],
            1,
            Some(fake_wpa2_rsne()),
        )
        .expect("expected InfraBss::try_new ok");
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
        let mut fake_scheduler = FakeScheduler::new();
        let mut ctx = make_context(fake_device.as_device(), fake_scheduler.as_scheduler());
        let mut bss = InfraBss::try_new(
            &mut ctx,
            b"coolnet".to_vec(),
            TimeUnit::DEFAULT_BEACON_INTERVAL,
            vec![0b11111000],
            1,
            Some(fake_wpa2_rsne()),
        )
        .expect("expected InfraBss::try_new ok");
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
            InfraBss::try_new(
                &mut ap.ctx,
                b"coolnet".to_vec(),
                TimeUnit::DEFAULT_BEACON_INTERVAL,
                vec![0b11111000],
                1,
                None,
            )
            .expect("expected InfraBss::try_new ok"),
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
                mac::CapabilityInfo(0),
                fidl_mlme::AssociateResultCodes::Success,
                1,
                &[][..],
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
            InfraBss::try_new(
                &mut ap.ctx,
                b"coolnet".to_vec(),
                TimeUnit::DEFAULT_BEACON_INTERVAL,
                vec![0b11111000],
                1,
                None,
            )
            .expect("expected InfraBss::try_new ok"),
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
            InfraBss::try_new(
                &mut ap.ctx,
                b"coolnet".to_vec(),
                TimeUnit::DEFAULT_BEACON_INTERVAL,
                vec![0b11111000],
                1,
                None,
            )
            .expect("expected InfraBss::try_new ok"),
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
            InfraBss::try_new(
                &mut ap.ctx,
                b"coolnet".to_vec(),
                TimeUnit::DEFAULT_BEACON_INTERVAL,
                vec![0b11111000],
                1,
                None,
            )
            .expect("expected InfraBss::try_new ok"),
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
            InfraBss::try_new(
                &mut ap.ctx,
                b"coolnet".to_vec(),
                TimeUnit::DEFAULT_BEACON_INTERVAL,
                vec![0b11111000],
                1,
                None,
            )
            .expect("expected InfraBss::try_new ok"),
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
            InfraBss::try_new(
                &mut ap.ctx,
                b"coolnet".to_vec(),
                TimeUnit::DEFAULT_BEACON_INTERVAL,
                vec![0b11111000],
                1,
                None,
            )
            .expect("expected InfraBss::try_new ok"),
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
            InfraBss::try_new(
                &mut ap.ctx,
                b"coolnet".to_vec(),
                TimeUnit::DEFAULT_BEACON_INTERVAL,
                vec![0b11111000],
                1,
                None,
            )
            .expect("expected InfraBss::try_new ok"),
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
            InfraBss::try_new(
                &mut ap.ctx,
                b"coolnet".to_vec(),
                TimeUnit::DEFAULT_BEACON_INTERVAL,
                vec![0b11111000],
                1,
                None,
            )
            .expect("expected InfraBss::try_new ok"),
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
            InfraBss::try_new(
                &mut ap.ctx,
                b"coolnet".to_vec(),
                TimeUnit::DEFAULT_BEACON_INTERVAL,
                vec![0b11111000],
                1,
                None,
            )
            .expect("expected InfraBss::try_new ok"),
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
            InfraBss::try_new(
                &mut ap.ctx,
                b"coolnet".to_vec(),
                TimeUnit::DEFAULT_BEACON_INTERVAL,
                vec![0b11111000],
                1,
                None,
            )
            .expect("expected InfraBss::try_new ok"),
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
            InfraBss::try_new(
                &mut ap.ctx,
                b"coolnet".to_vec(),
                TimeUnit::DEFAULT_BEACON_INTERVAL,
                vec![0b11111000],
                1,
                None,
            )
            .expect("expected InfraBss::try_new ok"),
        );
        ap.bss.as_mut().unwrap().clients.insert(CLIENT_ADDR, RemoteClient::new(CLIENT_ADDR));

        #[allow(deprecated)]
        ap.handle_mlme_msg(fidl_mlme::MlmeRequestMessage::AssociateResp {
            resp: fidl_mlme::AssociateResponse {
                peer_sta_address: CLIENT_ADDR,
                result_code: fidl_mlme::AssociateResultCodes::Success,
                association_id: 1,
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
            InfraBss::try_new(
                &mut ap.ctx,
                b"coolnet".to_vec(),
                TimeUnit::DEFAULT_BEACON_INTERVAL,
                vec![0b11111000],
                1,
                None,
            )
            .expect("expected InfraBss::try_new ok"),
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
            InfraBss::try_new(
                &mut ap.ctx,
                b"coolnet".to_vec(),
                TimeUnit::DEFAULT_BEACON_INTERVAL,
                vec![0b11111000],
                1,
                Some(fake_wpa2_rsne()),
            )
            .expect("expected InfraBss::try_new ok"),
        );
        ap.bss.as_mut().unwrap().clients.insert(CLIENT_ADDR, RemoteClient::new(CLIENT_ADDR));

        #[allow(deprecated)]
        ap.handle_mlme_msg(fidl_mlme::MlmeRequestMessage::AssociateResp {
            resp: fidl_mlme::AssociateResponse {
                peer_sta_address: CLIENT_ADDR,
                result_code: fidl_mlme::AssociateResultCodes::Success,
                association_id: 1,
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
            InfraBss::try_new(
                &mut ap.ctx,
                b"coolnet".to_vec(),
                TimeUnit::DEFAULT_BEACON_INTERVAL,
                vec![0b11111000],
                1,
                None,
            )
            .expect("expected InfraBss::try_new ok"),
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
