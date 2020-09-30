// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod channel_listener;
mod channel_scheduler;
mod convert_beacon;
mod lost_bss;
mod scanner;
mod state;
mod stats;

use {
    crate::{
        akm_algorithm,
        block_ack::BlockAckTx,
        buffer::{BufferProvider, OutBuf},
        device::{Device, TxFlags},
        disconnect::LocallyInitiated,
        error::Error,
        logger,
        timer::*,
    },
    anyhow::{self, format_err},
    banjo_ddk_protocol_wlan_info as banjo_wlan_info, banjo_ddk_protocol_wlan_mac as banjo_wlan_mac,
    channel_listener::{ChannelListenerSource, ChannelListenerState},
    channel_scheduler::ChannelScheduler,
    fidl_fuchsia_wlan_mlme as fidl_mlme, fuchsia_zircon as zx,
    log::{error, warn},
    scanner::Scanner,
    state::States,
    static_assertions::assert_eq_size,
    std::convert::TryInto,
    wlan_common::{
        appendable::Appendable,
        buffer_writer::BufferWriter,
        data_writer,
        ie::{self, parse_ht_capabilities, parse_vht_capabilities, rsn::rsne, Id, Reader},
        mac::{self, Aid, Bssid, MacAddr, PowerState},
        mgmt_writer,
        sequence::SequenceManager,
        time::TimeUnit,
        wmm,
    },
    wlan_frame_writer::{write_frame, write_frame_with_dynamic_buf, write_frame_with_fixed_buf},
    zerocopy::{AsBytes, ByteSlice},
};

pub use scanner::ScanError;

/// Maximum size of EAPOL frames forwarded to SME.
/// TODO(fxbug.dev/34845): Evaluate whether EAPOL size restriction is needed.
const MAX_EAPOL_FRAME_LEN: usize = 255;

#[derive(Debug, PartialEq)]
pub enum TimedEvent {
    /// Authentication timed out. AP did not complete authentication in time.
    Authenticating,
    /// Association timed out. AP did not complete association in time.
    Associating,
    ChannelScheduler,
    ScannerProbeDelay(banjo_wlan_info::WlanChannel),
    /// Association status update includes checking for auto deauthentication due to beacon loss
    /// and report signal strength
    AssociationStatusCheck,
}

/// ClientConfig affects time duration used for different timeouts.
/// Originally added to more easily control behavior in tests.
#[repr(C)]
#[derive(Debug, Clone)]
pub struct ClientConfig {
    ensure_on_channel_time: zx::sys::zx_duration_t,
}

pub struct Context {
    config: ClientConfig,
    device: Device,
    buf_provider: BufferProvider,
    timer: Timer<TimedEvent>,
    seq_mgr: SequenceManager,
}

pub struct ClientMlme {
    sta: Option<Client>,
    ctx: Context,
    scanner: Scanner,
    chan_sched: ChannelScheduler,
    channel_state: ChannelListenerState,
}

impl ClientMlme {
    pub fn new(
        config: ClientConfig,
        device: Device,
        buf_provider: BufferProvider,
        scheduler: Scheduler,
    ) -> Self {
        // TODO(fxbug.dev/41417): Remove this once devmgr installs a Rust logger.
        logger::install();

        let iface_mac = device.wlan_info().ifc_info.mac_addr;
        let timer = Timer::<TimedEvent>::new(scheduler);
        Self {
            sta: None,
            ctx: Context { config, device, buf_provider, timer, seq_mgr: SequenceManager::new() },
            scanner: Scanner::new(iface_mac),
            chan_sched: ChannelScheduler::new(),
            channel_state: Default::default(),
        }
    }

    pub fn seq_mgr(&mut self) -> &mut SequenceManager {
        &mut self.ctx.seq_mgr
    }

    pub fn set_main_channel(
        &mut self,
        channel: banjo_wlan_info::WlanChannel,
    ) -> Result<(), zx::Status> {
        self.ctx.device.set_channel(channel)?;
        self.channel_state.main_channel = Some(channel);
        Ok(())
    }

    pub fn on_channel(&self) -> bool {
        let channel = self.ctx.device.channel();
        self.channel_state.main_channel.map(|c| c == channel).unwrap_or(false)
    }

    pub fn on_mac_frame(&mut self, frame: &[u8], rx_info: Option<banjo_wlan_mac::WlanRxInfo>) {
        // TODO(fxbug.dev/44487): Send the entire frame to scanner.
        match mac::MacFrame::parse(frame, false) {
            Some(mac::MacFrame::Mgmt { mgmt_hdr, body, .. }) => {
                let bssid = Bssid(mgmt_hdr.addr3);
                let frame_ctrl = mgmt_hdr.frame_ctrl;
                match mac::MgmtBody::parse(frame_ctrl.mgmt_subtype(), body) {
                    Some(mac::MgmtBody::Beacon { bcn_hdr, elements }) => {
                        self.scanner.bind(&mut self.ctx).handle_beacon_or_probe_response(
                            bssid,
                            bcn_hdr.timestamp,
                            bcn_hdr.beacon_interval,
                            bcn_hdr.capabilities,
                            elements,
                            rx_info,
                        );
                    }
                    Some(mac::MgmtBody::ProbeResp { probe_resp_hdr, elements }) => {
                        self.scanner.bind(&mut self.ctx).handle_beacon_or_probe_response(
                            bssid,
                            probe_resp_hdr.timestamp,
                            probe_resp_hdr.beacon_interval,
                            probe_resp_hdr.capabilities,
                            elements,
                            rx_info,
                        )
                    }
                    _ => (),
                }
            }
            _ => (),
        }

        if let Some(sta) = self.sta.as_mut() {
            sta.bind(
                &mut self.ctx,
                &mut self.scanner,
                &mut self.chan_sched,
                &mut self.channel_state,
            )
            .on_mac_frame(frame, rx_info)
        }
    }

    #[allow(deprecated)] // Allow until main message loop is in Rust.
    pub fn handle_mlme_msg(&mut self, msg: fidl_mlme::MlmeRequestMessage) -> Result<(), Error> {
        use fidl_mlme::MlmeRequestMessage as MlmeMsg;

        match msg {
            // Handle non station specific MLME messages first (Join, Scan, etc.)
            MlmeMsg::StartScan { req } => Ok(self.on_sme_scan(req)),
            MlmeMsg::JoinReq { req } => self.on_sme_join(req),
            MlmeMsg::StatsQueryReq {} => self.on_sme_stats_query(),
            other_message => match &mut self.sta {
                None => Err(Error::Status(format!("No client sta."), zx::Status::BAD_STATE)),
                Some(sta) => Ok(sta
                    .bind(
                        &mut self.ctx,
                        &mut self.scanner,
                        &mut self.chan_sched,
                        &mut self.channel_state,
                    )
                    .handle_mlme_msg(other_message)),
            },
        }
    }

    fn on_sme_scan(&mut self, req: fidl_mlme::ScanRequest) {
        let channel_state = &mut self.channel_state;
        let sta = self.sta.as_mut();
        // No need to handle result because scanner already send ScanEnd if it errors out
        let _result = self.scanner.bind(&mut self.ctx).on_sme_scan(
            req,
            |ctx, scanner| channel_state.bind(ctx, scanner, sta),
            &mut self.chan_sched,
        );
    }

    pub fn handle_hw_scan_complete(&mut self, status: banjo_wlan_mac::WlanHwScan) {
        self.scanner.bind(&mut self.ctx).handle_hw_scan_complete(status);
    }

    fn on_sme_join(&mut self, req: fidl_mlme::JoinRequest) -> Result<(), Error> {
        let bss = req.selected_bss;
        match self.join_device(&bss) {
            Ok(()) => {
                self.sta.replace(Client::new(
                    bss.ssid.clone(),
                    Bssid(bss.bssid),
                    self.ctx.device.wlan_info().ifc_info.mac_addr,
                    bss.beacon_period,
                    bss.rsne.is_some(),
                ));
                self.ctx.device.access_sme_sender(|sender| {
                    sender.send_join_conf(&mut fidl_mlme::JoinConfirm {
                        result_code: fidl_mlme::JoinResultCodes::Success,
                    })
                })
            }
            Err(e) => {
                error!("Error setting up device for join: {}", e);
                self.ctx.device.access_sme_sender(|sender| {
                    sender.send_join_conf(&mut fidl_mlme::JoinConfirm {
                        // TODO(fxbug.dev/44317): Only one failure code defined in IEEE 802.11-2016 6.3.4.3
                        // Can we do better?
                        result_code: fidl_mlme::JoinResultCodes::JoinFailureTimeout,
                    })
                })?;
                Err(e)
            }
        }
    }

    fn join_device(&mut self, bss: &fidl_mlme::BssDescription) -> Result<(), Error> {
        let channel = crate::ddk_converter::ddk_channel_from_fidl(bss.chan);
        self.set_main_channel(channel)
            .map_err(|status| Error::Status(format!("Error setting device channel"), status))?;

        let bss_config = banjo_wlan_info::WlanBssConfig {
            bssid: bss.bssid.clone(),
            bss_type: banjo_wlan_info::WlanBssType::INFRASTRUCTURE,
            remote: true,
        };

        // Configure driver to pass frames from this BSS to MLME. Otherwise they will be dropped.
        self.ctx
            .device
            .configure_bss(bss_config)
            .map_err(|status| Error::Status(format!("Error setting BSS in driver"), status))
    }

    fn on_sme_stats_query(&self) -> Result<(), Error> {
        // TODO(fxbug.dev/43456): Implement stats
        let mut resp = stats::empty_stats_query_response();
        self.ctx.device.access_sme_sender(|sender| sender.send_stats_query_resp(&mut resp))
    }

    pub fn on_eth_frame<B: ByteSlice>(&mut self, bytes: B) -> Result<(), Error> {
        match self.sta.as_mut() {
            None => Err(Error::Status(
                format!("Ethernet frame dropped (Client does not exist)."),
                zx::Status::BAD_STATE,
            )),
            Some(sta) => sta
                .bind(
                    &mut self.ctx,
                    &mut self.scanner,
                    &mut self.chan_sched,
                    &mut self.channel_state,
                )
                .on_eth_frame(bytes),
        }
    }

    /// Called when a previously scheduled `TimedEvent` fired.
    /// Return true if auto-deauth has triggered. Return false otherwise.
    pub fn handle_timed_event(&mut self, event_id: EventId) {
        let event = match self.ctx.timer.triggered(&event_id) {
            Some(event) => event,
            None => {
                error!(
                    "event for given ID {:?} already consumed;\
                     this should NOT happen - ignoring event",
                    event_id
                );
                return;
            }
        };

        match event {
            TimedEvent::ChannelScheduler => {
                let mut listener =
                    self.channel_state.bind(&mut self.ctx, &mut self.scanner, self.sta.as_mut());
                // We are not scheduling new event, so it doesn't matter what source we bind here
                let mut chan_sched =
                    self.chan_sched.bind(&mut listener, ChannelListenerSource::Others);
                chan_sched.handle_timeout();
            }
            TimedEvent::ScannerProbeDelay(channel) => {
                self.scanner.bind(&mut self.ctx).handle_probe_delay_timeout(channel);
            }
            other_event => {
                if let Some(sta) = self.sta.as_mut() {
                    return sta
                        .bind(
                            &mut self.ctx,
                            &mut self.scanner,
                            &mut self.chan_sched,
                            &mut self.channel_state,
                        )
                        .handle_timed_event(other_event, event_id);
                }
            }
        }
    }
}

/// A STA running in Client mode.
/// The Client STA is in its early development process and does not yet manage its internal state
/// machine or track negotiated capabilities.
pub struct Client {
    state: Option<States>,
    pub ssid: Vec<u8>,
    pub bssid: Bssid,
    pub iface_mac: MacAddr,
    beacon_period: u16,
    pub is_rsn: bool,
}

impl Client {
    pub fn new(
        ssid: Vec<u8>,
        bssid: Bssid,
        iface_mac: MacAddr,
        beacon_period: u16,
        is_rsn: bool,
    ) -> Self {
        Self {
            state: Some(States::new_initial()),
            ssid: ssid.to_vec(),
            bssid,
            iface_mac,
            beacon_period,
            is_rsn,
        }
    }

    pub fn bind<'a>(
        &'a mut self,
        ctx: &'a mut Context,
        scanner: &'a mut Scanner,
        chan_sched: &'a mut ChannelScheduler,
        channel_state: &'a mut ChannelListenerState,
    ) -> BoundClient<'a> {
        BoundClient { sta: self, ctx, scanner, chan_sched, channel_state }
    }

    pub fn pre_switch_off_channel(&mut self, ctx: &mut Context) {
        // Safe to unwrap() because state is never None.
        let mut state = self.state.take().unwrap();
        state.pre_switch_off_channel(self, ctx);
        self.state.replace(state);
    }

    pub fn handle_back_on_channel(&mut self, ctx: &mut Context) {
        // Safe to unwrap() because state is never None.
        let mut state = self.state.take().unwrap();
        state.handle_back_on_channel(self, ctx);
        self.state.replace(state);
    }

    /// Sends a power management data frame to the associated AP indicating that the client has
    /// entered the given power state. See `PowerState`.
    ///
    /// # Errors
    ///
    /// Returns an error if the data frame cannot be sent to the AP.
    fn send_power_state_frame(
        &mut self,
        ctx: &mut Context,
        state: PowerState,
    ) -> Result<(), Error> {
        let (buf, bytes_written) = write_frame!(&mut ctx.buf_provider, {
            headers: {
                mac::FixedDataHdrFields: &mac::FixedDataHdrFields {
                    frame_ctrl: mac::FrameControl(0)
                        .with_frame_type(mac::FrameType::DATA)
                        .with_data_subtype(mac::DataSubtype(0).with_null(true))
                        .with_power_mgmt(state)
                        .with_to_ds(true),
                    duration: 0,
                    addr1: self.bssid.0,
                    addr2: self.iface_mac,
                    addr3: self.bssid.0,
                    seq_ctrl: mac::SequenceControl(0)
                        .with_seq_num(ctx.seq_mgr.next_sns1(&self.bssid.0) as u16)
                },
            },
        })?;
        let out_buf = OutBuf::from(buf, bytes_written);
        ctx.device
            .send_wlan_frame(out_buf, TxFlags::NONE)
            .map_err(|error| Error::Status(format!("error sending power management frame"), error))
    }

    /// Only management and data frames should be processed. Furthermore, the source address should
    /// be the BSSID the client associated to and the receiver address should either be non-unicast
    /// or the client's MAC address.
    fn should_handle_frame<B: ByteSlice>(&self, mac_frame: &mac::MacFrame<B>) -> bool {
        // Technically, |transmitter_addr| and |receiver_addr| would be more accurate but using src
        // src and dst to be consistent with |data_dst_addr()|.
        let (src_addr, dst_addr) = match mac_frame {
            mac::MacFrame::Mgmt { mgmt_hdr, .. } => (Some(mgmt_hdr.addr3), mgmt_hdr.addr1),
            mac::MacFrame::Data { fixed_fields, .. } => {
                (mac::data_bssid(&fixed_fields), mac::data_dst_addr(&fixed_fields))
            }
            // Control frames are not supported. Drop them.
            _ => return false,
        };
        src_addr.map_or(false, |src_addr| src_addr == self.bssid.0)
            && (!is_unicast(dst_addr) || dst_addr == self.iface_mac)
    }
}

/// A MAC address is a unicast address if the least significant bit of the first octet is 0.
/// See "individual/group bit" in
/// https://standards.ieee.org/content/dam/ieee-standards/standards/web/documents/tutorials/macgrp.pdf
fn is_unicast(addr: MacAddr) -> bool {
    addr[0] & 1 == 0
}

pub struct BoundClient<'a> {
    sta: &'a mut Client,
    // TODO(fxbug.dev/44079): pull everything out of Context and plop them here.
    ctx: &'a mut Context,
    scanner: &'a mut Scanner,
    chan_sched: &'a mut ChannelScheduler,
    channel_state: &'a mut ChannelListenerState,
}

impl<'a> akm_algorithm::AkmAction for BoundClient<'a> {
    type EventId = EventId;

    fn send_auth_frame(
        &mut self,
        auth_type: mac::AuthAlgorithmNumber,
        seq_num: u16,
        result_code: mac::StatusCode,
        auth_content: &[u8],
    ) -> Result<(), anyhow::Error> {
        self.send_auth_frame(auth_type, seq_num, result_code, auth_content).map_err(|e| e.into())
    }

    fn forward_sme_sae_rx(
        &mut self,
        seq_num: u16,
        result_code: fidl_mlme::AuthenticateResultCodes,
        sae_fields: Vec<u8>,
    ) {
        self.forward_sae_frame_rx(seq_num, result_code, sae_fields)
    }

    fn forward_sae_handshake_ind(&mut self) {
        self.forward_sae_handshake_ind()
    }

    fn schedule_auth_timeout(&mut self, duration: TimeUnit) -> EventId {
        let deadline = self.ctx.timer.now() + duration.into();
        self.ctx.timer.schedule_event(deadline, TimedEvent::Authenticating)
    }

    fn cancel_auth_timeout(&mut self, id: EventId) {
        self.ctx.timer.cancel_event(id)
    }
}

impl<'a> BoundClient<'a> {
    /// Delivers a single MSDU to the STA's underlying device. The MSDU is delivered as an
    /// Ethernet II frame.
    /// Returns Err(_) if writing or delivering the Ethernet II frame failed.
    fn deliver_msdu<B: ByteSlice>(&mut self, msdu: mac::Msdu<B>) -> Result<(), Error> {
        let mac::Msdu { dst_addr, src_addr, llc_frame } = msdu;

        let (buf, bytes_written) = write_frame_with_fixed_buf!([0u8; mac::MAX_ETH_FRAME_LEN], {
            headers: {
                mac::EthernetIIHdr: &mac::EthernetIIHdr {
                    da: dst_addr,
                    sa: src_addr,
                    ether_type: llc_frame.hdr.protocol_id,
                },
            },
            payload: &llc_frame.body,
        })?;
        let (written, _remaining) = buf.split_at(bytes_written);
        self.ctx
            .device
            .deliver_eth_frame(written)
            .map_err(|s| Error::Status(format!("could not deliver Ethernet II frame"), s))
    }

    pub fn send_auth_frame(
        &mut self,
        auth_type: mac::AuthAlgorithmNumber,
        seq_num: u16,
        result_code: mac::StatusCode,
        auth_content: &[u8],
    ) -> Result<(), Error> {
        let (buf, bytes_written) = write_frame!(&mut self.ctx.buf_provider, {
            headers: {
                mac::MgmtHdr: &mgmt_writer::mgmt_hdr_to_ap(
                    mac::FrameControl(0)
                        .with_frame_type(mac::FrameType::MGMT)
                        .with_mgmt_subtype(mac::MgmtSubtype::AUTH),
                    self.sta.bssid,
                    self.sta.iface_mac,
                    mac::SequenceControl(0)
                        .with_seq_num(self.ctx.seq_mgr.next_sns1(&self.sta.bssid.0) as u16)
                ),
                mac::AuthHdr: &mac::AuthHdr {
                    auth_alg_num: auth_type,
                    auth_txn_seq_num: seq_num,
                    status_code: result_code,
                },
            },
            body: auth_content,
        })?;
        let out_buf = OutBuf::from(buf, bytes_written);
        self.send_mgmt_or_ctrl_frame(out_buf)
            .map_err(|s| Error::Status(format!("error sending open auth frame"), s))
    }

    /// Sends an authentication frame using Open System authentication.
    pub fn send_open_auth_frame(&mut self) -> Result<(), Error> {
        self.send_auth_frame(mac::AuthAlgorithmNumber::OPEN, 1, mac::StatusCode::SUCCESS, &[])
    }

    /// Sends an association request frame based on device capability.
    // TODO(fxbug.dev/39148): Use an IE set instead of individual IEs.
    pub fn send_assoc_req_frame(
        &mut self,
        cap_info: u16,
        rates: &[u8],
        rsne: &[u8],
        ht_cap: &[u8],
        vht_cap: &[u8],
    ) -> Result<(), Error> {
        let ssid = self.sta.ssid.clone();
        let (buf, bytes_written) = write_frame!(&mut self.ctx.buf_provider, {
            headers: {
                mac::MgmtHdr: &mgmt_writer::mgmt_hdr_to_ap(
                    mac::FrameControl(0)
                        .with_frame_type(mac::FrameType::MGMT)
                        .with_mgmt_subtype(mac::MgmtSubtype::ASSOC_REQ),
                    self.sta.bssid,
                    self.sta.iface_mac,
                    mac::SequenceControl(0)
                        .with_seq_num(self.ctx.seq_mgr.next_sns1(&self.sta.bssid.0) as u16)
                ),
                mac::AssocReqHdr: &mac::AssocReqHdr {
                    capabilities: mac::CapabilityInfo(cap_info),
                    listen_interval: 0,
                },
            },
            ies: {
                ssid: ssid,
                supported_rates: rates,
                extended_supported_rates: {/* continue rates */},
                rsne?: if !rsne.is_empty() {
                    rsne::from_bytes(rsne)
                        .map_err(|e| format_err!("error parsing rsne {:?} : {:?}", rsne, e))?
                        .1
                },
                ht_cap?: if !ht_cap.is_empty() { *parse_ht_capabilities(ht_cap)? },
                vht_cap?: if !vht_cap.is_empty() { *parse_vht_capabilities(vht_cap)? },
            },
        })?;
        let out_buf = OutBuf::from(buf, bytes_written);
        self.send_mgmt_or_ctrl_frame(out_buf)
            .map_err(|s| Error::Status(format!("error sending assoc req frame"), s))
    }

    /// Sends a "keep alive" response to the BSS. A keep alive response is a NULL data frame sent as
    /// a response to the AP transmitting NULL data frames to the client.
    // Note: This function was introduced to meet C++ MLME feature parity. However, there needs to
    // be some investigation, whether these "keep alive" frames are the right way of keeping a
    // client associated to legacy APs.
    fn send_keep_alive_resp_frame(&mut self) -> Result<(), Error> {
        let (buf, bytes_written) = write_frame!(&mut self.ctx.buf_provider, {
            headers: {
                mac::FixedDataHdrFields: &data_writer::data_hdr_client_to_ap(
                    mac::FrameControl(0)
                        .with_frame_type(mac::FrameType::DATA)
                        .with_data_subtype(mac::DataSubtype(0).with_null(true)),
                    self.sta.bssid,
                    self.sta.iface_mac,
                    mac::SequenceControl(0)
                        .with_seq_num(self.ctx.seq_mgr.next_sns1(&self.sta.bssid.0) as u16)
                ),
            },
        })?;
        let out_buf = OutBuf::from(buf, bytes_written);
        self.ctx
            .device
            .send_wlan_frame(out_buf, TxFlags::NONE)
            .map_err(|s| Error::Status(format!("error sending keep alive frame"), s))
    }

    pub fn send_deauth_frame(&mut self, reason_code: mac::ReasonCode) -> Result<(), Error> {
        let (buf, bytes_written) = write_frame!(&mut self.ctx.buf_provider, {
            headers: {
                mac::MgmtHdr: &mgmt_writer::mgmt_hdr_to_ap(
                    mac::FrameControl(0)
                        .with_frame_type(mac::FrameType::MGMT)
                        .with_mgmt_subtype(mac::MgmtSubtype::DEAUTH),
                    self.sta.bssid,
                    self.sta.iface_mac,
                    mac::SequenceControl(0)
                        .with_seq_num(self.ctx.seq_mgr.next_sns1(&self.sta.bssid.0) as u16)
                ),
                mac::DeauthHdr: &mac::DeauthHdr {
                    reason_code,
                },
            },
        })?;
        let out_buf = OutBuf::from(buf, bytes_written);
        let result = self
            .send_mgmt_or_ctrl_frame(out_buf)
            .map_err(|s| Error::Status(format!("error sending deauthenticate frame"), s));

        // Clear main_channel since there is no "main channel" after deauthenticating
        self.channel_state.main_channel = None;

        result
    }

    /// Sends the given payload as a data frame over the air.
    pub fn send_data_frame(
        &mut self,
        src: MacAddr,
        dst: MacAddr,
        is_protected: bool,
        qos_ctrl: bool,
        ether_type: u16,
        payload: &[u8],
    ) -> Result<(), Error> {
        let qos_ctrl = if qos_ctrl {
            Some(
                wmm::derive_tid(ether_type, payload)
                    .map_or(mac::QosControl(0), |tid| mac::QosControl(0).with_tid(tid as u16)),
            )
        } else {
            None
        };

        let (buf, bytes_written) = write_frame!(&mut self.ctx.buf_provider, {
            headers: {
                mac::FixedDataHdrFields: &mac::FixedDataHdrFields {
                    frame_ctrl: mac::FrameControl(0)
                        .with_frame_type(mac::FrameType::DATA)
                        .with_data_subtype(mac::DataSubtype(0).with_qos(qos_ctrl.is_some()))
                        .with_protected(is_protected)
                        .with_to_ds(true),
                    duration: 0,
                    addr1: self.sta.bssid.0,
                    addr2: src,
                    addr3: dst,
                    seq_ctrl: mac::SequenceControl(0).with_seq_num(
                        match qos_ctrl.as_ref() {
                            None => self.ctx.seq_mgr.next_sns1(&dst),
                            Some(qos_ctrl) => self.ctx.seq_mgr.next_sns2(&dst, qos_ctrl.tid()),
                        } as u16
                    )
                },
                mac::QosControl?: qos_ctrl,
                mac::LlcHdr: &data_writer::make_snap_llc_hdr(ether_type),
            },
            payload: payload,
        })?;
        let out_buf = OutBuf::from(buf, bytes_written);
        let tx_flags = match ether_type {
            mac::ETHER_TYPE_EAPOL => TxFlags::FAVOR_RELIABILITY,
            _ => TxFlags::NONE,
        };
        self.ctx
            .device
            .send_wlan_frame(out_buf, tx_flags)
            .map_err(|s| Error::Status(format!("error sending data frame"), s))
    }

    /// Sends an MLME-EAPOL.indication to MLME's SME peer.
    /// Note: MLME-EAPOL.indication is a custom Fuchsia primitive and not defined in IEEE 802.11.
    fn send_eapol_indication(
        &mut self,
        src_addr: MacAddr,
        dst_addr: MacAddr,
        eapol_frame: &[u8],
    ) -> Result<(), Error> {
        if eapol_frame.len() > MAX_EAPOL_FRAME_LEN {
            return Err(Error::Internal(format_err!(
                "EAPOL frame too large: {}",
                eapol_frame.len()
            )));
        }
        self.ctx.device.access_sme_sender(|sender| {
            sender.send_eapol_ind(&mut fidl_mlme::EapolIndication {
                src_addr,
                dst_addr,
                data: eapol_frame.to_vec(),
            })
        })
    }

    /// Sends an EAPoL frame over the air and reports transmission status to SME via an
    /// MLME-EAPOL.confirm message.
    pub fn send_eapol_frame(
        &mut self,
        src: MacAddr,
        dst: MacAddr,
        is_protected: bool,
        eapol_frame: &[u8],
    ) {
        // TODO(fxbug.dev/34910): EAPoL frames can be send in QoS data frames. However, Fuchsia's old C++
        // MLME never sent EAPoL frames in QoS data frames. For feature parity do the same.
        let result = self.send_data_frame(
            src,
            dst,
            is_protected,
            false, /* don't use QoS */
            mac::ETHER_TYPE_EAPOL,
            eapol_frame,
        );
        let result_code = match result {
            Ok(()) => fidl_mlme::EapolResultCodes::Success,
            Err(e) => {
                error!("error sending EAPoL frame: {}", e);
                fidl_mlme::EapolResultCodes::TransmissionFailure
            }
        };

        // Report transmission result to SME.
        let result = self.ctx.device.access_sme_sender(|sender| {
            sender.send_eapol_conf(&mut fidl_mlme::EapolConfirm { result_code })
        });
        if let Err(e) = result {
            error!("error sending MLME-EAPOL.confirm message: {}", e);
        }
    }

    pub fn send_ps_poll_frame(&mut self, aid: Aid) -> Result<(), Error> {
        const PS_POLL_ID_MASK: u16 = 0b11000000_00000000;

        let (buf, bytes_written) = write_frame!(&mut self.ctx.buf_provider, {
            headers: {
                mac::FrameControl: &mac::FrameControl(0)
                    .with_frame_type(mac::FrameType::CTRL)
                    .with_ctrl_subtype(mac::CtrlSubtype::PS_POLL),
                mac::PsPoll: &mac::PsPoll {
                    // IEEE 802.11-2016 9.3.1.5 states the ID in the PS-Poll frame is the
                    // association ID with the 2 MSBs set to 1.
                    masked_aid: aid | PS_POLL_ID_MASK,
                    bssid: self.sta.bssid,
                    ta: self.sta.iface_mac,
                },
            },
        })?;
        let out_buf = OutBuf::from(buf, bytes_written);
        self.send_mgmt_or_ctrl_frame(out_buf)
            .map_err(|s| Error::Status(format!("error sending PS-Poll frame"), s))
    }

    /// Called when a previously scheduled `TimedEvent` fired.
    pub fn handle_timed_event(&mut self, event: TimedEvent, event_id: EventId) {
        self.sta.state = Some(self.sta.state.take().unwrap().on_timed_event(self, event, event_id))
    }

    /// Called when an arbitrary frame was received over the air.
    pub fn on_mac_frame<B: ByteSlice>(
        &mut self,
        bytes: B,
        rx_info: Option<banjo_wlan_mac::WlanRxInfo>,
    ) {
        // Safe: |state| is never None and always replaced with Some(..).
        self.sta.state = Some(self.sta.state.take().unwrap().on_mac_frame(self, bytes, rx_info));
    }

    pub fn on_eth_frame<B: ByteSlice>(&mut self, frame: B) -> Result<(), Error> {
        // Safe: |state| is never None and always replaced with Some(..).
        let state = self.sta.state.take().unwrap();
        let result = state.on_eth_frame(self, frame);
        self.sta.state.replace(state);
        result
    }

    #[allow(deprecated)] // Allow until main message loop is in Rust.
    pub fn handle_mlme_msg(&mut self, msg: fidl_mlme::MlmeRequestMessage) {
        // Safe: |state| is never None and always replaced with Some(..).
        let next_state = self.sta.state.take().unwrap().handle_mlme_msg(self, msg);
        self.sta.state.replace(next_state);
    }

    /// Sends an MLME-AUTHENTICATE.confirm message to the SME with authentication type
    /// `Open System` as only open authentication is supported.
    fn send_authenticate_conf(
        &mut self,
        auth_type: fidl_mlme::AuthenticationTypes,
        result_code: fidl_mlme::AuthenticateResultCodes,
    ) {
        let result = self.ctx.device.access_sme_sender(|sender| {
            sender.send_authenticate_conf(&mut fidl_mlme::AuthenticateConfirm {
                peer_sta_address: self.sta.bssid.0,
                auth_type,
                result_code,
            })
        });
        if let Err(e) = result {
            error!("error sending MLME-AUTHENTICATE.confirm: {}", e);
        }
    }

    /// Sends an MLME-ASSOCIATE.confirm message to the SME.
    fn send_associate_conf_failure(&mut self, result_code: fidl_mlme::AssociateResultCodes) {
        // AID used for reporting failed associations to SME.
        const FAILED_ASSOCIATION_AID: mac::Aid = 0;

        let mut assoc_conf = fidl_mlme::AssociateConfirm {
            association_id: FAILED_ASSOCIATION_AID,
            cap_info: 0,
            result_code,
            rates: vec![],
            wmm_param: None,
            ht_cap: None,
            vht_cap: None,
        };

        let result =
            self.ctx.device.access_sme_sender(|sender| sender.send_associate_conf(&mut assoc_conf));
        if let Err(e) = result {
            error!("error sending MLME-AUTHENTICATE.confirm: {}", e);
        }
    }

    fn send_associate_conf_success<B: ByteSlice>(
        &mut self,
        association_id: mac::Aid,
        cap_info: mac::CapabilityInfo,
        elements: B,
    ) {
        type HtCapArray = [u8; fidl_mlme::HT_CAP_LEN as usize];
        type VhtCapArray = [u8; fidl_mlme::VHT_CAP_LEN as usize];

        let mut assoc_conf = fidl_mlme::AssociateConfirm {
            association_id,
            cap_info: cap_info.raw(),
            result_code: fidl_mlme::AssociateResultCodes::Success,
            rates: vec![],
            wmm_param: None,
            ht_cap: None,
            vht_cap: None,
        };

        for (id, body) in Reader::new(elements) {
            match id {
                Id::SUPPORTED_RATES => {
                    // safe to unwrap because supported rate is 1-byte long thus always aligned
                    assoc_conf.rates.extend_from_slice(&body);
                }
                Id::EXT_SUPPORTED_RATES => {
                    // safe to unwrap because supported rate is 1-byte thus always aligned
                    assoc_conf.rates.extend_from_slice(&body);
                }
                Id::HT_CAPABILITIES => match ie::parse_ht_capabilities(body) {
                    Err(e) => error!("invalid HT Capabilities: {}", e),
                    Ok(ht_cap) => {
                        assert_eq_size!(ie::HtCapabilities, HtCapArray);
                        let bytes: HtCapArray = ht_cap.as_bytes().try_into().unwrap();
                        assoc_conf.ht_cap = Some(Box::new(fidl_mlme::HtCapabilities { bytes }))
                    }
                },
                Id::VHT_CAPABILITIES => match ie::parse_vht_capabilities(body) {
                    Err(e) => error!("invalid VHT Capabilities: {}", e),
                    Ok(vht_cap) => {
                        assert_eq_size!(ie::VhtCapabilities, VhtCapArray);
                        let bytes: VhtCapArray = vht_cap.as_bytes().try_into().unwrap();
                        assoc_conf.vht_cap = Some(Box::new(fidl_mlme::VhtCapabilities { bytes }))
                    }
                },
                // TODO(fxbug.dev/43938): parse vendor ID and include WMM param if exists
                _ => {}
            }
        }

        let result =
            self.ctx.device.access_sme_sender(|sender| sender.send_associate_conf(&mut assoc_conf));
        if let Err(e) = result {
            error!("error sending MLME-AUTHENTICATE.confirm: {}", e);
        }
    }

    /// Sends an MLME-DEAUTHENTICATE.indication message to the joined BSS.
    fn send_deauthenticate_ind(
        &mut self,
        reason_code: fidl_mlme::ReasonCode,
        locally_initiated: LocallyInitiated,
    ) {
        // Clear main_channel since there is no "main channel" after deauthenticating
        self.channel_state.main_channel = None;

        let result = self.ctx.device.access_sme_sender(|sender| {
            sender.send_deauthenticate_ind(&mut fidl_mlme::DeauthenticateIndication {
                peer_sta_address: self.sta.bssid.0,
                reason_code,
                locally_initiated: locally_initiated.0,
            })
        });
        if let Err(e) = result {
            error!("error sending MLME-DEAUTHENTICATE.indication: {}", e);
        }
    }

    /// Sends an MLME-DISASSOCIATE.indication message to the joined BSS.
    fn send_disassoc_ind(
        &mut self,
        reason_code: fidl_mlme::ReasonCode,
        locally_initiated: LocallyInitiated,
    ) {
        let result = self.ctx.device.access_sme_sender(|sender| {
            sender.send_disassociate_ind(&mut fidl_mlme::DisassociateIndication {
                peer_sta_address: self.sta.bssid.0,
                reason_code: reason_code.into_primitive(),
                locally_initiated: locally_initiated.0,
            })
        });
        if let Err(e) = result {
            error!("error sending MLME-DEAUTHENTICATE.indication: {}", e);
        }
    }

    /// Sends an sae frame rx message to the SME.
    fn forward_sae_frame_rx(
        &mut self,
        seq_num: u16,
        result_code: fidl_mlme::AuthenticateResultCodes,
        sae_fields: Vec<u8>,
    ) {
        let result = self.ctx.device.access_sme_sender(|sender| {
            sender.send_on_sae_frame_rx(&mut fidl_mlme::SaeFrame {
                peer_sta_address: self.sta.bssid.0,
                seq_num,
                result_code,
                sae_fields,
            })
        });
        if let Err(e) = result {
            error!("error sending OnSaeFrameRx: {}", e);
        }
    }

    fn forward_sae_handshake_ind(&mut self) {
        let result = self.ctx.device.access_sme_sender(|sender| {
            sender.send_on_sae_handshake_ind(&mut fidl_mlme::SaeHandshakeIndication {
                peer_sta_address: self.sta.bssid.0,
            })
        });
        if let Err(e) = result {
            error!("error sending OnSaeHandshakeInd: {}", e);
        }
    }

    fn is_on_channel(&self) -> bool {
        let channel = self.ctx.device.channel();
        self.channel_state.main_channel.map(|c| c == channel).unwrap_or(false)
    }

    fn send_mgmt_or_ctrl_frame(&mut self, out_buf: OutBuf) -> Result<(), zx::Status> {
        self.ensure_on_channel();
        self.ctx.device.send_wlan_frame(out_buf, TxFlags::NONE)
    }

    fn ensure_on_channel(&mut self) {
        match self.channel_state.main_channel {
            Some(main_channel) => {
                let duration = zx::Duration::from_nanos(self.ctx.config.ensure_on_channel_time);
                let mut listener = self.channel_state.bind(self.ctx, self.scanner, Some(self.sta));
                self.chan_sched
                    .bind(&mut listener, ChannelListenerSource::Others)
                    .schedule_immediate(main_channel, duration);
            }
            None => warn!("main channel not set, cannot ensure on channel"),
        }
    }
}

impl<'a> BlockAckTx for BoundClient<'a> {
    /// Sends a BlockAck frame to the associated AP.
    ///
    /// BlockAck frames are described by 802.11-2016, section 9.6.5.2, 9.6.5.3, and 9.6.5.4.
    fn send_block_ack_frame(&mut self, n: usize, body: &[u8]) -> Result<(), Error> {
        let mut buffer = self.ctx.buf_provider.get_buffer(n)?;
        let mut writer = BufferWriter::new(&mut buffer[..]);
        write_block_ack_hdr(&mut writer, self.sta.bssid, self.sta.iface_mac, &mut self.ctx.seq_mgr)
            .and_then(|_| writer.append_bytes(body).map_err(Into::into))?;
        let n = writer.bytes_written();
        let buffer = OutBuf::from(buffer, n);
        self.send_mgmt_or_ctrl_frame(buffer)
            .map_err(|status| Error::Status(format!("error sending BlockAck frame"), status))
    }
}

/// Writes the header of the management frame for BlockAck frames to the given buffer.
///
/// The address may be that of the originator or recipient. The frame formats are described by IEEE
/// Std 802.11-2016, 9.6.5.
fn write_block_ack_hdr<B: Appendable>(
    buffer: &mut B,
    bssid: Bssid,
    addr: MacAddr,
    seq_mgr: &mut SequenceManager,
) -> Result<usize, Error> {
    // The management header differs for APs and clients. The frame control and management header
    // are constructed here, but AP and client STAs share the code that constructs the body. See
    // the `block_ack` module.
    write_frame_with_dynamic_buf!(
        buffer,
        {
            headers: {
                mac::MgmtHdr: &mgmt_writer::mgmt_hdr_to_ap(
                    mac::FrameControl(0)
                        .with_frame_type(mac::FrameType::MGMT)
                        .with_mgmt_subtype(mac::MgmtSubtype::ACTION),
                    bssid,
                    addr,
                    mac::SequenceControl(0)
                        .with_seq_num(seq_mgr.next_sns1(&bssid.0) as u16),
                ),
            },
        }
    )
    .map(|(_, n)| n)
}

#[cfg(test)]
mod tests {
    use {
        super::{state::DEFAULT_AUTO_DEAUTH_TIMEOUT_BEACON_COUNT, *},
        crate::{
            block_ack::{self, BlockAckState, Closed, ADDBA_REQ_FRAME_LEN, ADDBA_RESP_FRAME_LEN},
            buffer::FakeBufferProvider,
            client::lost_bss::LostBssCounter,
            device::FakeDevice,
        },
        fidl_fuchsia_wlan_common as fidl_common,
        wlan_common::{
            assert_variant, ie, stats::SignalStrengthAverage, test_utils::fake_frames::*, TimeUnit,
        },
        wlan_statemachine::*,
    };
    const BSSID: Bssid = Bssid([6u8; 6]);
    const IFACE_MAC: MacAddr = [7u8; 6];
    const RSNE: &[u8] = &[
        0x30, 0x14, //  ID and len
        1, 0, //  version
        0x00, 0x0f, 0xac, 0x04, //  group data cipher suite
        0x01, 0x00, //  pairwise cipher suite count
        0x00, 0x0f, 0xac, 0x04, //  pairwise cipher suite list
        0x01, 0x00, //  akm suite count
        0x00, 0x0f, 0xac, 0x02, //  akm suite list
        0xa8, 0x04, //  rsn capabilities
    ];
    const MAIN_CHANNEL: banjo_wlan_info::WlanChannel = banjo_wlan_info::WlanChannel {
        primary: 11,
        cbw: banjo_wlan_info::WlanChannelBandwidth::_20,
        secondary80: 0,
    };
    const SCAN_CHANNEL_PRIMARY: u8 = 6;
    // Note: not necessarily valid beacon frame.
    #[rustfmt::skip]
    const BEACON_FRAME: &'static [u8] = &[
        // Mgmt header
        0b10000000, 0, // Frame Control
        0, 0, // Duration
        255, 255, 255, 255, 255, 255, // addr1
        6, 6, 6, 6, 6, 6, // addr2
        6, 6, 6, 6, 6, 6, // addr3
        0, 0, // Sequence Control
        // Beacon header:
        0, 0, 0, 0, 0, 0, 0, 0, // Timestamp
        10, 0, // Beacon interval
        33, 0, // Capabilities
        // IEs:
        0, 4, 0x73, 0x73, 0x69, 0x64, // SSID - "ssid"
        1, 8, 1, 2, 3, 4, 5, 6, 7, 8, // Supported rates
        3, 1, 11, // DSSS parameter set - channel 11
        5, 4, 0, 0, 0, 0, // TIM
    ];

    struct MockObjects {
        fake_device: FakeDevice,
        fake_scheduler: FakeScheduler,
    }

    impl MockObjects {
        fn new() -> Self {
            Self { fake_device: FakeDevice::new(), fake_scheduler: FakeScheduler::new() }
        }

        fn make_mlme(&mut self) -> ClientMlme {
            let device = self.fake_device.as_device();
            self.make_mlme_with_device(device)
        }

        fn make_mlme_with_device(&mut self, device: Device) -> ClientMlme {
            let config = ClientConfig { ensure_on_channel_time: 0 };
            let mut mlme = ClientMlme::new(
                config,
                device,
                FakeBufferProvider::new(),
                self.fake_scheduler.as_scheduler(),
            );
            mlme.set_main_channel(MAIN_CHANNEL).expect("unable to set main channel");
            mlme
        }
    }

    fn scan_req() -> fidl_mlme::ScanRequest {
        fidl_mlme::ScanRequest {
            txn_id: 1337,
            bss_type: fidl_mlme::BssTypes::Infrastructure,
            bssid: BSSID.0,
            ssid: b"ssid".to_vec(),
            scan_type: fidl_mlme::ScanTypes::Passive,
            probe_delay: 0,
            channel_list: Some(vec![SCAN_CHANNEL_PRIMARY]),
            min_channel_time: 100,
            max_channel_time: 300,
            ssid_list: None,
        }
    }

    fn make_client_station() -> Client {
        Client::new(vec![], BSSID, IFACE_MAC, TimeUnit::DEFAULT_BEACON_INTERVAL.0, false)
    }

    impl ClientMlme {
        fn make_client_station(&mut self) {
            self.sta.replace(make_client_station());
        }

        fn get_bound_client(&mut self) -> Option<BoundClient<'_>> {
            match self.sta.as_mut() {
                None => None,
                Some(sta) => Some(sta.bind(
                    &mut self.ctx,
                    &mut self.scanner,
                    &mut self.chan_sched,
                    &mut self.channel_state,
                )),
            }
        }
    }

    impl BoundClient<'_> {
        fn move_to_associated_state(&mut self) {
            use super::state::*;
            let status_check_timeout =
                schedule_association_status_timeout(self.sta.beacon_period, &mut self.ctx.timer);
            let state =
                States::from(wlan_statemachine::testing::new_state(Associated(Association {
                    aid: 42,
                    controlled_port_open: true,
                    ap_ht_op: None,
                    ap_vht_op: None,
                    qos: Qos::Disabled,
                    lost_bss_counter: LostBssCounter::start(
                        self.sta.beacon_period,
                        DEFAULT_AUTO_DEAUTH_TIMEOUT_BEACON_COUNT,
                    ),
                    status_check_timeout,
                    signal_strength_average: SignalStrengthAverage::new(),
                    block_ack_state: StateMachine::new(BlockAckState::from(State::new(Closed))),
                })));
            self.sta.state.replace(state);
        }

        #[allow(deprecated)] // MlmeRequestMessage is deprecated
        fn close_controlled_port(&mut self) {
            self.sta.is_rsn = true;
            self.handle_mlme_msg(fidl_mlme::MlmeRequestMessage::SetControlledPort {
                req: fidl_mlme::SetControlledPortRequest {
                    peer_sta_address: BSSID.0,
                    state: fidl_mlme::ControlledPortState::Closed,
                },
            });
        }
    }

    #[test]
    fn spawns_new_sta_on_join_request_from_sme() {
        let mut m = MockObjects::new();
        let mut me = m.make_mlme();
        assert!(me.get_bound_client().is_none(), "MLME should not contain client, yet");
        me.on_sme_join(fidl_mlme::JoinRequest {
            selected_bss: wlan_common::test_utils::fake_stas::fake_bss_description(
                vec![],
                None,
                None,
            ),
            join_failure_timeout: 42,
            nav_sync_delay: 42,
            op_rates: vec![1, 2, 3],
            phy: fidl_common::Phy::Erp,
            cbw: fidl_common::Cbw::Cbw20,
        })
        .expect("valid JoinRequest should be handled successfully");
        me.get_bound_client().expect("client sta should have been created by now.");
    }

    #[test]
    fn test_ensure_on_channel_followed_by_scheduled_scan() {
        let mut m = MockObjects::new();
        let mut me = m.make_mlme();
        me.make_client_station();
        let mut client = me.get_bound_client().expect("client should be present");
        client.send_open_auth_frame().expect("error delivering WLAN frame");
        assert_eq!(m.fake_device.wlan_queue.len(), 1);

        // Verify ensure_on_channel. That is, scheduling scan request would not cause channel to be
        // switched right away, while frame is still being sent.
        me.on_sme_scan(scan_req());
        assert_eq!(me.ctx.device.channel(), MAIN_CHANNEL);

        // Verify that triggering scheduled timeout by channel scheduler would switch channel
        assert_eq!(m.fake_scheduler.deadlines.len(), 1);
        me.handle_timed_event(m.fake_scheduler.next_id.into());
        assert_eq!(me.ctx.device.channel().primary, SCAN_CHANNEL_PRIMARY);
    }

    #[test]
    fn test_active_scan_scheduling() {
        let mut m = MockObjects::new();
        let mut me = m.make_mlme();
        me.make_client_station();

        let scan_req = fidl_mlme::ScanRequest {
            scan_type: fidl_mlme::ScanTypes::Active,
            probe_delay: 5,
            ..scan_req()
        };
        let scan_txn_id = scan_req.txn_id;
        me.on_sme_scan(scan_req);
        assert_eq!(me.ctx.device.channel().primary, SCAN_CHANNEL_PRIMARY);

        // There should be two scheduled events, one by channel scheduler for scanned channel,
        // another by scanner for delayed sending of probe request
        assert_eq!(m.fake_scheduler.deadlines.len(), 2);
        let (id, _deadline) = m.fake_scheduler.next_event().expect("expect scheduled event [1]");
        me.handle_timed_event(id);

        // Verify that probe delay is sent.
        assert_eq!(m.fake_device.wlan_queue.len(), 1);
        #[rustfmt::skip]
        assert_eq!(&m.fake_device.wlan_queue[0].0[..], &[
            // Mgmt header:
            0b0100_00_00, 0b00000000, // FC
            0, 0, // Duration
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, // addr1
            7, 7, 7, 7, 7, 7, // addr2
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, // addr3
            0x10, 0, // Sequence Control
            // IEs
            0, 4, // SSID id and length
            115, 115, 105, 100, // SSID
            1, 6, // supp_rates id and length
            12, 24, 48, 54, 96, 108, // supp_rates
        ][..]);
        m.fake_device.wlan_queue.clear();
        assert_eq!(me.ctx.device.channel().primary, SCAN_CHANNEL_PRIMARY);

        // Trigger timeout by channel scheduler, indicating end of scan request
        let (id, _deadline) = m.fake_scheduler.next_event().expect("expect scheduled event [2]");
        me.handle_timed_event(id);
        assert_eq!(me.ctx.device.channel(), MAIN_CHANNEL);
        let msg =
            m.fake_device.next_mlme_msg::<fidl_mlme::ScanEnd>().expect("error reading SCAN.end");
        assert_eq!(msg.txn_id, scan_txn_id);
        assert_eq!(msg.code, fidl_mlme::ScanResultCodes::Success);
    }

    #[test]
    fn test_no_power_state_frame_when_client_is_not_connected() {
        let mut m = MockObjects::new();
        let mut me = m.make_mlme();
        me.make_client_station();

        me.on_sme_scan(scan_req());
        assert_eq!(me.ctx.device.channel().primary, SCAN_CHANNEL_PRIMARY);
        // Verify no power state frame is sent
        assert_eq!(m.fake_device.wlan_queue.len(), 0);

        // There should be one scheduled event for end of channel period
        assert_eq!(m.fake_scheduler.deadlines.len(), 1);
        let (id, _deadline) = m.fake_scheduler.next_event().expect("expect scheduled event");
        me.handle_timed_event(id);
        assert_eq!(me.ctx.device.channel(), MAIN_CHANNEL);

        // Verify no power state frame is sent
        assert_eq!(m.fake_device.wlan_queue.len(), 0);
    }

    #[test]
    fn test_send_power_state_frame_when_switching_channel_while_connected() {
        let mut m = MockObjects::new();
        let mut me = m.make_mlme();
        me.make_client_station();
        let mut client = me.get_bound_client().expect("client should be present");

        // Pretend that client is associated by starting LostBssCounter
        client.move_to_associated_state();
        // clear the LostBssCounter timeout.
        m.fake_scheduler.deadlines.clear();

        // Send scan request to trigger channel switch
        me.on_sme_scan(scan_req());
        assert_eq!(me.ctx.device.channel().primary, SCAN_CHANNEL_PRIMARY);

        // Verify that power state frame is sent
        assert_eq!(m.fake_device.wlan_queue.len(), 1);
        #[rustfmt::skip]
        assert_eq!(&m.fake_device.wlan_queue[0].0[..], &[
            // Data header:
            0b0100_10_00, 0b00010001, // FC
            0, 0, // Duration
            6, 6, 6, 6, 6, 6, // addr1
            7, 7, 7, 7, 7, 7, // addr2
            6, 6, 6, 6, 6, 6, // addr3
            0x10, 0, // Sequence Control
        ][..]);
        m.fake_device.wlan_queue.clear();

        // There should be one scheduled event for end of channel period
        assert_eq!(m.fake_scheduler.deadlines.len(), 1);
        let (id, _deadline) = m.fake_scheduler.next_event().expect("expect scheduled event");
        me.handle_timed_event(id);
        assert_eq!(me.ctx.device.channel(), MAIN_CHANNEL);

        // Verify that power state frame is sent
        assert_eq!(m.fake_device.wlan_queue.len(), 1);
        #[rustfmt::skip]
        assert_eq!(&m.fake_device.wlan_queue[0].0[..], &[
            // Data header:
            0b0100_10_00, 0b00000001, // FC
            0, 0, // Duration
            6, 6, 6, 6, 6, 6, // addr1
            7, 7, 7, 7, 7, 7, // addr2
            6, 6, 6, 6, 6, 6, // addr3
            0x20, 0, // Sequence Control
        ][..]);
    }

    // Auto-deauth is tied to singal report by AssociationStatusCheck timeout
    fn advance_auto_deauth(m: &mut MockObjects, me: &mut ClientMlme, beacon_count: u32) {
        for _ in 0..beacon_count / super::state::ASSOCIATION_STATUS_TIMEOUT_BEACON_COUNT {
            let (id, deadline) = assert_variant!(m.fake_scheduler.next_event(), Some(ev) => ev);
            m.fake_scheduler.set_time(deadline);
            me.handle_timed_event(id);
            assert_eq!(m.fake_device.wlan_queue.len(), 0);
            m.fake_device
                .next_mlme_msg::<fidl_mlme::SignalReportIndication>()
                .expect("error reading SignalReport.indication");
        }
    }

    #[test]
    fn test_auto_deauth_uninterrupted_interval() {
        let mut m = MockObjects::new();
        let mut me = m.make_mlme();
        me.make_client_station();
        let mut client = me.get_bound_client().expect("client should be present");

        client.move_to_associated_state();

        // Verify timer is scheduled and move the time to immediately before auto deauth is triggered.
        advance_auto_deauth(&mut m, &mut me, DEFAULT_AUTO_DEAUTH_TIMEOUT_BEACON_COUNT);

        // One more timeout to trigger the auto deauth
        let (id, deadline) = assert_variant!(m.fake_scheduler.next_event(), Some(ev) => ev);

        // Verify that triggering event at deadline causes deauth
        m.fake_scheduler.set_time(deadline);
        me.handle_timed_event(id);
        m.fake_device
            .next_mlme_msg::<fidl_mlme::SignalReportIndication>()
            .expect("error reading SignalReport.indication");
        assert_eq!(m.fake_device.wlan_queue.len(), 1);
        #[rustfmt::skip]
        assert_eq!(&m.fake_device.wlan_queue[0].0[..], &[
            // Mgmt header:
            0b1100_00_00, 0b00000000, // FC
            0, 0, // Duration
            6, 6, 6, 6, 6, 6, // addr1
            7, 7, 7, 7, 7, 7, // addr2
            6, 6, 6, 6, 6, 6, // addr3
            0x10, 0, // Sequence Control
            3, 0, // reason code
        ][..]);
        let deauth_ind = m
            .fake_device
            .next_mlme_msg::<fidl_mlme::DeauthenticateIndication>()
            .expect("error reading DEAUTHENTICATE.indication");
        assert_eq!(
            deauth_ind,
            fidl_mlme::DeauthenticateIndication {
                peer_sta_address: BSSID.0,
                reason_code: fidl_mlme::ReasonCode::LeavingNetworkDeauth,
                locally_initiated: true,
            }
        );
    }

    #[test]
    fn test_auto_deauth_received_beacon() {
        let mut m = MockObjects::new();
        let mut me = m.make_mlme();
        me.make_client_station();
        let mut client = me.get_bound_client().expect("client should be present");

        client.move_to_associated_state();

        // Move the countdown to just about to cause auto deauth.
        advance_auto_deauth(&mut m, &mut me, DEFAULT_AUTO_DEAUTH_TIMEOUT_BEACON_COUNT);

        // Receive beacon midway, so lost bss countdown is reset.
        // If this beacon is not received, the next timeout will trigger auto deauth.
        me.on_mac_frame(BEACON_FRAME, None);

        // Verify auto deauth is not triggered for the entire duration.
        advance_auto_deauth(&mut m, &mut me, DEFAULT_AUTO_DEAUTH_TIMEOUT_BEACON_COUNT);

        // Verify more timer is scheduled
        let (id2, deadline2) = assert_variant!(m.fake_scheduler.next_event(), Some(ev) => ev);

        // Verify that triggering event at new deadline causes deauth
        m.fake_scheduler.set_time(deadline2);
        me.handle_timed_event(id2);
        m.fake_device
            .next_mlme_msg::<fidl_mlme::SignalReportIndication>()
            .expect("error reading SignalReport.indication");
        assert_eq!(m.fake_device.wlan_queue.len(), 1);
        #[rustfmt::skip]
        assert_eq!(&m.fake_device.wlan_queue[0].0[..], &[
            // Mgmt header:
            0b1100_00_00, 0b00000000, // FC
            0, 0, // Duration
            6, 6, 6, 6, 6, 6, // addr1
            7, 7, 7, 7, 7, 7, // addr2
            6, 6, 6, 6, 6, 6, // addr3
            0x10, 0, // Sequence Control
            3, 0, // reason code
        ][..]);
        let deauth_ind = m
            .fake_device
            .next_mlme_msg::<fidl_mlme::DeauthenticateIndication>()
            .expect("error reading DEAUTHENTICATE.indication");
        assert_eq!(
            deauth_ind,
            fidl_mlme::DeauthenticateIndication {
                peer_sta_address: BSSID.0,
                reason_code: fidl_mlme::ReasonCode::LeavingNetworkDeauth,
                locally_initiated: true,
            }
        );
    }

    #[test]
    fn client_send_open_auth_frame() {
        let mut m = MockObjects::new();
        let mut me = m.make_mlme();
        me.make_client_station();
        let mut client = me.get_bound_client().expect("client should be present");
        client.send_open_auth_frame().expect("error delivering WLAN frame");
        assert_eq!(m.fake_device.wlan_queue.len(), 1);
        #[rustfmt::skip]
        assert_eq!(&m.fake_device.wlan_queue[0].0[..], &[
            // Mgmt header:
            0b1011_00_00, 0b00000000, // FC
            0, 0, // Duration
            6, 6, 6, 6, 6, 6, // addr1
            7, 7, 7, 7, 7, 7, // addr2
            6, 6, 6, 6, 6, 6, // addr3
            0x10, 0, // Sequence Control
            // Auth header:
            0, 0, // auth algorithm
            1, 0, // auth txn seq num
            0, 0, // status code
        ][..]);

        // Verify ensure_on_channel. That is, scheduling scan request would not cause channel to be
        // switched right away, while frame is still being sent.
        me.on_sme_scan(scan_req());
        assert_eq!(me.ctx.device.channel(), MAIN_CHANNEL);
    }

    #[test]
    fn client_send_assoc_req_frame() {
        let mut m = MockObjects::new();
        let mut me = m.make_mlme();
        me.make_client_station();
        let mut client = me.get_bound_client().expect("client should be present");
        client.sta.ssid = [11, 22, 33, 44].to_vec();
        client
            .send_assoc_req_frame(
                0x1234,                               // capability info
                &[8, 7, 6, 5, 4, 3, 2, 1, 0],         // rates
                RSNE,                                 // RSNE (including ID and len)
                &(0..26).collect::<Vec<u8>>()[..],    // HT Capabilities
                &(100..112).collect::<Vec<u8>>()[..], // VHT Capabilities
            )
            .expect("error delivering WLAN frame");
        assert_eq!(m.fake_device.wlan_queue.len(), 1);
        assert_eq!(
            &m.fake_device.wlan_queue[0].0[..],
            &[
                // Mgmt header:
                0, 0, // FC
                0, 0, // Duration
                6, 6, 6, 6, 6, 6, // addr1
                7, 7, 7, 7, 7, 7, // addr2
                6, 6, 6, 6, 6, 6, // addr3
                0x10, 0, // Sequence Control
                // Association Request header:
                0x34, 0x12, // capability info
                0, 0, // listen interval
                // IEs
                0, 4, // SSID id and length
                11, 22, 33, 44, // SSID
                1, 8, // supp rates id and length
                8, 7, 6, 5, 4, 3, 2, 1, // supp rates
                50, 1, // ext supp rates and length
                0, // ext supp rates
                0x30, 0x14, // RSNE ID and len
                1, 0, // RSNE version
                0x00, 0x0f, 0xac, 0x04, // RSNE group data cipher suite
                0x01, 0x00, // RSNE pairwise cipher suite count
                0x00, 0x0f, 0xac, 0x04, // RSNE pairwise cipher suite list
                0x01, 0x00, // RSNE akm suite count
                0x00, 0x0f, 0xac, 0x02, // RSNE akm suite list
                0xa8, 0x04, // RSNE rsn capabilities
                45, 26, // HT Cap id and length
                0, 1, 2, 3, 4, 5, 6, 7, // HT Cap \
                8, 9, 10, 11, 12, 13, 14, 15, // HT Cap \
                16, 17, 18, 19, 20, 21, 22, 23, // HT Cap \
                24, 25, // HT Cap (26 bytes)
                191, 12, // VHT Cap id and length
                100, 101, 102, 103, 104, 105, 106, 107, // VHT Cap \
                108, 109, 110, 111, // VHT Cap (12 bytes)
            ][..]
        );

        // Verify ensure_on_channel. That is, scheduling scan request would not cause channel to be
        // switched right away, while frame is still being sent.
        me.on_sme_scan(scan_req());
        assert_eq!(me.ctx.device.channel(), MAIN_CHANNEL);
    }

    #[test]
    fn client_send_keep_alive_resp_frame() {
        let mut m = MockObjects::new();
        let mut me = m.make_mlme();
        me.make_client_station();
        let mut client = me.get_bound_client().expect("client should be present");
        client.send_keep_alive_resp_frame().expect("error delivering WLAN frame");
        assert_eq!(m.fake_device.wlan_queue.len(), 1);
        #[rustfmt::skip]
        assert_eq!(&m.fake_device.wlan_queue[0].0[..], &[
            // Data header:
            0b0100_10_00, 0b0000000_1, // FC
            0, 0, // Duration
            6, 6, 6, 6, 6, 6, // addr1
            7, 7, 7, 7, 7, 7, // addr2
            6, 6, 6, 6, 6, 6, // addr3
            0x10, 0, // Sequence Control
        ][..]);

        // Verify no ensure_on_channel. That is, scheduling scan request would cause channel to be
        // switched right away.
        me.on_sme_scan(scan_req());
        assert_eq!(me.ctx.device.channel().primary, SCAN_CHANNEL_PRIMARY);
    }

    #[test]
    fn client_send_data_frame() {
        let payload = vec![5; 8];
        let mut m = MockObjects::new();
        let mut me = m.make_mlme();
        me.make_client_station();
        let mut client = me.get_bound_client().expect("client should be present");
        client
            .send_data_frame([2; 6], [3; 6], false, false, 0x1234, &payload[..])
            .expect("error delivering WLAN frame");
        assert_eq!(m.fake_device.wlan_queue.len(), 1);
        #[rustfmt::skip]
        assert_eq!(&m.fake_device.wlan_queue[0].0[..], &[
            // Data header:
            0b0000_10_00, 0b0000000_1, // FC
            0, 0, // Duration
            6, 6, 6, 6, 6, 6, // addr1
            2, 2, 2, 2, 2, 2, // addr2
            3, 3, 3, 3, 3, 3, // addr3
            0x10, 0, // Sequence Control
            // LLC header:
            0xAA, 0xAA, 0x03, // DSAP, SSAP, Control
            0, 0, 0, // OUI
            0x12, 0x34, // Protocol ID
            // Payload
            5, 5, 5, 5, 5, 5, 5, 5,
        ][..]);

        // Verify no ensure_on_channel. That is, scheduling scan request would cause channel to be
        // switched right away.
        me.on_sme_scan(scan_req());
        assert_eq!(me.ctx.device.channel().primary, SCAN_CHANNEL_PRIMARY);
    }

    #[test]
    fn client_send_data_frame_ipv4_qos() {
        let mut m = MockObjects::new();
        let mut me = m.make_mlme();
        let mut client = make_client_station();
        client
            .bind(&mut me.ctx, &mut me.scanner, &mut me.chan_sched, &mut me.channel_state)
            .send_data_frame(
                [2; 6],
                [3; 6],
                false,
                true,
                0x0800,              // IPv4
                &[1, 0xB0, 3, 4, 5], // DSCP = 0b101100 (i.e. VOICE-ADMIT)
            )
            .expect("error delivering WLAN frame");
        assert_eq!(m.fake_device.wlan_queue.len(), 1);
        #[rustfmt::skip]
        assert_eq!(&m.fake_device.wlan_queue[0].0[..], &[
            // Data header:
            0b1000_10_00, 0b0000000_1, // FC
            0, 0, // Duration
            6, 6, 6, 6, 6, 6, // addr1
            2, 2, 2, 2, 2, 2, // addr2
            3, 3, 3, 3, 3, 3, // addr3
            0x10, 0, // Sequence Control
            0x06, 0, // QoS Control - TID = 6
            // LLC header:
            0xAA, 0xAA, 0x03, // DSAP, SSAP, Control
            0, 0, 0, // OUI
            0x08, 0x00, // Protocol ID
            // Payload
            1, 0xB0, 3, 4, 5,
        ][..]);

        // Verify no ensure_on_channel. That is, scheduling scan request would cause channel to be
        // switched right away.
        me.on_sme_scan(scan_req());
        assert_eq!(me.ctx.device.channel().primary, SCAN_CHANNEL_PRIMARY);
    }

    #[test]
    fn client_send_data_frame_ipv6_qos() {
        let mut m = MockObjects::new();
        let mut me = m.make_mlme();
        let mut client = make_client_station();
        client
            .bind(&mut me.ctx, &mut me.scanner, &mut me.chan_sched, &mut me.channel_state)
            .send_data_frame(
                [2; 6],
                [3; 6],
                false,
                true,
                0x86DD,                         // IPv6
                &[0b0101, 0b10000000, 3, 4, 5], // DSCP = 0b010110 (i.e. AF23)
            )
            .expect("error delivering WLAN frame");
        assert_eq!(m.fake_device.wlan_queue.len(), 1);
        #[rustfmt::skip]
        assert_eq!(&m.fake_device.wlan_queue[0].0[..], &[
            // Data header:
            0b1000_10_00, 0b0000000_1, // FC
            0, 0, // Duration
            6, 6, 6, 6, 6, 6, // addr1
            2, 2, 2, 2, 2, 2, // addr2
            3, 3, 3, 3, 3, 3, // addr3
            0x10, 0, // Sequence Control
            0x03, 0, // QoS Control - TID = 3
            // LLC header:
            0xAA, 0xAA, 0x03, // DSAP, SSAP, Control
            0, 0, 0, // OUI
            0x86, 0xDD, // Protocol ID
            // Payload
            0b0101, 0b10000000, 3, 4, 5,
        ][..]);

        // Verify no ensure_on_channel. That is, scheduling scan request would cause channel to be
        // switched right away.
        me.on_sme_scan(scan_req());
        assert_eq!(me.ctx.device.channel().primary, SCAN_CHANNEL_PRIMARY);
    }

    #[test]
    fn client_send_deauthentication_notification() {
        let mut m = MockObjects::new();
        let mut me = m.make_mlme();
        me.make_client_station();
        let mut client = me.get_bound_client().expect("client should be present");

        client
            .send_deauth_frame(mac::ReasonCode::AP_INITIATED)
            .expect("error delivering WLAN frame");
        assert_eq!(m.fake_device.wlan_queue.len(), 1);
        #[rustfmt::skip]
        assert_eq!(&m.fake_device.wlan_queue[0].0[..], &[
            // Mgmt header:
            0b1100_00_00, 0b00000000, // FC
            0, 0, // Duration
            6, 6, 6, 6, 6, 6, // addr1
            7, 7, 7, 7, 7, 7, // addr2
            6, 6, 6, 6, 6, 6, // addr3
            0x10, 0, // Sequence Control
            47, 0, // reason code
        ][..]);

        // Verify ensure_on_channel. That is, scheduling scan request would not cause channel to be
        // switched right away, while frame is still being sent.
        me.on_sme_scan(scan_req());
        assert_eq!(me.ctx.device.channel(), MAIN_CHANNEL);
    }

    #[test]
    fn respond_to_keep_alive_request() {
        #[rustfmt::skip]
        let data_frame = vec![
            // Data header:
            0b0100_10_00, 0b000000_1_0, // FC
            0, 0, // Duration
            7, 7, 7, 7, 7, 7, // addr1
            6, 6, 6, 6, 6, 6, // addr2
            42, 42, 42, 42, 42, 42, // addr3
            0x10, 0, // Sequence Control
        ];
        let mut m = MockObjects::new();
        let mut me = m.make_mlme();
        me.make_client_station();
        let mut client = me.get_bound_client().expect("client should be present");
        client.move_to_associated_state();

        client.on_mac_frame(&data_frame[..], None);

        assert_eq!(m.fake_device.wlan_queue.len(), 1);
        #[rustfmt::skip]
        assert_eq!(&m.fake_device.wlan_queue[0].0[..], &[
            // Data header:
            0b0100_10_00, 0b0000000_1, // FC
            0, 0, // Duration
            6, 6, 6, 6, 6, 6, // addr1
            7, 7, 7, 7, 7, 7, // addr2
            6, 6, 6, 6, 6, 6, // addr3
            0x10, 0, // Sequence Control
        ][..]);

        // Verify no ensure_on_channel. That is, scheduling scan request would cause channel to be
        // switched right away.
        me.on_sme_scan(scan_req());
        assert_eq!(me.ctx.device.channel().primary, SCAN_CHANNEL_PRIMARY);
    }

    #[test]
    fn data_frame_to_ethernet_single_llc() {
        let mut data_frame = make_data_frame_single_llc(None, None);
        data_frame[1] = 0b00000010; // from_ds = 1, to_ds = 0 when AP sends to client (us)
        data_frame[4..10].copy_from_slice(&IFACE_MAC); // addr1 - receiver - client (us)
        data_frame[10..16].copy_from_slice(&BSSID.0); // addr2 - bssid

        let mut m = MockObjects::new();
        let mut me = m.make_mlme();
        me.make_client_station();
        let mut client = me.get_bound_client().expect("client should be present");
        client.move_to_associated_state();

        client.on_mac_frame(&data_frame[..], None);

        assert_eq!(m.fake_device.eth_queue.len(), 1);
        #[rustfmt::skip]
        assert_eq!(m.fake_device.eth_queue[0], [
            7, 7, 7, 7, 7, 7, // dst_addr
            5, 5, 5, 5, 5, 5, // src_addr
            9, 10, // ether_type
            11, 11, 11, // payload
        ]);
    }

    #[test]
    fn data_frame_to_ethernet_amsdu() {
        let mut data_frame = make_data_frame_amsdu();
        data_frame[1] = 0b00000010; // from_ds = 1, to_ds = 0 when AP sends to client (us)
        data_frame[4..10].copy_from_slice(&IFACE_MAC); // addr1 - receiver - client (us)
        data_frame[10..16].copy_from_slice(&BSSID.0); // addr2 - bssid

        let mut m = MockObjects::new();
        let mut me = m.make_mlme();
        me.make_client_station();
        let mut client = me.get_bound_client().expect("client should be present");
        client.move_to_associated_state();

        client.on_mac_frame(&data_frame[..], None);

        let queue = &m.fake_device.eth_queue;
        assert_eq!(queue.len(), 2);
        #[rustfmt::skip]
        let mut expected_first_eth_frame = vec![
            0x78, 0x8a, 0x20, 0x0d, 0x67, 0x03, // dst_addr
            0xb4, 0xf7, 0xa1, 0xbe, 0xb9, 0xab, // src_addr
            0x08, 0x00, // ether_type
        ];
        expected_first_eth_frame.extend_from_slice(MSDU_1_PAYLOAD);
        assert_eq!(queue[0], &expected_first_eth_frame[..]);
        #[rustfmt::skip]
        let mut expected_second_eth_frame = vec![
            0x78, 0x8a, 0x20, 0x0d, 0x67, 0x04, // dst_addr
            0xb4, 0xf7, 0xa1, 0xbe, 0xb9, 0xac, // src_addr
            0x08, 0x01, // ether_type
        ];
        expected_second_eth_frame.extend_from_slice(MSDU_2_PAYLOAD);
        assert_eq!(queue[1], &expected_second_eth_frame[..]);
    }

    #[test]
    fn data_frame_to_ethernet_amsdu_padding_too_short() {
        let mut data_frame = make_data_frame_amsdu_padding_too_short();
        data_frame[1] = 0b00000010; // from_ds = 1, to_ds = 0 when AP sends to client (us)
        data_frame[4..10].copy_from_slice(&IFACE_MAC); // addr1 - receiver - client (us)
        data_frame[10..16].copy_from_slice(&BSSID.0); // addr2 - bssid

        let mut m = MockObjects::new();
        let mut me = m.make_mlme();
        me.make_client_station();
        let mut client = me.get_bound_client().expect("client should be present");
        client.move_to_associated_state();

        client.on_mac_frame(&data_frame[..], None);

        let queue = &m.fake_device.eth_queue;
        assert_eq!(queue.len(), 1);
        #[rustfmt::skip]
            let mut expected_first_eth_frame = vec![
            0x78, 0x8a, 0x20, 0x0d, 0x67, 0x03, // dst_addr
            0xb4, 0xf7, 0xa1, 0xbe, 0xb9, 0xab, // src_addr
            0x08, 0x00, // ether_type
        ];
        expected_first_eth_frame.extend_from_slice(MSDU_1_PAYLOAD);
        assert_eq!(queue[0], &expected_first_eth_frame[..]);
    }

    #[test]
    fn data_frame_controlled_port_closed() {
        let mut data_frame = make_data_frame_single_llc(None, None);
        data_frame[1] = 0b00000010; // from_ds = 1, to_ds = 0 when AP sends to client (us)
        data_frame[4..10].copy_from_slice(&IFACE_MAC); // addr1 - receiver - client (us)
        data_frame[10..16].copy_from_slice(&BSSID.0); // addr2 - bssid

        let mut m = MockObjects::new();
        let mut me = m.make_mlme();
        me.make_client_station();
        let mut client = me.get_bound_client().expect("client should be present");
        client.move_to_associated_state();
        client.close_controlled_port();

        client.on_mac_frame(&data_frame[..], None);

        // Verify frame was not sent to netstack.
        assert_eq!(m.fake_device.eth_queue.len(), 0);
    }

    #[test]
    fn eapol_frame_controlled_port_closed() {
        let (src_addr, dst_addr, mut eapol_frame) = make_eapol_frame(IFACE_MAC);
        eapol_frame[1] = 0b00000010; // from_ds = 1, to_ds = 0 when AP sends to client (us)
        eapol_frame[4..10].copy_from_slice(&IFACE_MAC); // addr1 - receiver - client (us)
        eapol_frame[10..16].copy_from_slice(&BSSID.0); // addr2 - bssid

        let mut m = MockObjects::new();
        let mut me = m.make_mlme();
        me.make_client_station();
        let mut client = me.get_bound_client().expect("client should be present");
        client.move_to_associated_state();
        client.close_controlled_port();

        client.on_mac_frame(&eapol_frame[..], None);

        // Verify EAPoL frame was not sent to netstack.
        assert_eq!(m.fake_device.eth_queue.len(), 0);

        // Verify EAPoL frame was sent to SME.
        let eapol_ind = m
            .fake_device
            .next_mlme_msg::<fidl_mlme::EapolIndication>()
            .expect("error reading EAPOL.indication");
        assert_eq!(
            eapol_ind,
            fidl_mlme::EapolIndication { src_addr, dst_addr, data: EAPOL_PDU.to_vec() }
        );
    }

    #[test]
    fn eapol_frame_is_controlled_port_open() {
        let (src_addr, dst_addr, mut eapol_frame) = make_eapol_frame(IFACE_MAC);
        eapol_frame[1] = 0b00000010; // from_ds = 1, to_ds = 0 when AP sends to client (us)
        eapol_frame[4..10].copy_from_slice(&IFACE_MAC); // addr1 - receiver - client (us)
        eapol_frame[10..16].copy_from_slice(&BSSID.0); // addr2 - bssid

        let mut m = MockObjects::new();
        let mut me = m.make_mlme();
        me.make_client_station();
        let mut client = me.get_bound_client().expect("client should be present");
        client.move_to_associated_state();

        client.on_mac_frame(&eapol_frame[..], None);

        // Verify EAPoL frame was not sent to netstack.
        assert_eq!(m.fake_device.eth_queue.len(), 0);

        // Verify EAPoL frame was sent to SME.
        let eapol_ind = m
            .fake_device
            .next_mlme_msg::<fidl_mlme::EapolIndication>()
            .expect("error reading EAPOL.indication");
        assert_eq!(
            eapol_ind,
            fidl_mlme::EapolIndication { src_addr, dst_addr, data: EAPOL_PDU.to_vec() }
        );
    }

    #[test]
    fn send_eapol_ind_too_large() {
        let mut m = MockObjects::new();
        let mut me = m.make_mlme();
        me.make_client_station();
        let mut client = me.get_bound_client().expect("client should be present");
        client
            .send_eapol_indication([1; 6], [2; 6], &[5; 256])
            .expect_err("sending too large EAPOL frame should fail");
        m.fake_device
            .next_mlme_msg::<fidl_mlme::EapolIndication>()
            .expect_err("expected empty channel");
    }

    #[test]
    fn send_eapol_ind_success() {
        let mut m = MockObjects::new();
        let mut me = m.make_mlme();
        me.make_client_station();
        let mut client = me.get_bound_client().expect("client should be present");
        client
            .send_eapol_indication([1; 6], [2; 6], &[5; 200])
            .expect("expected EAPOL.indication to be sent");
        let eapol_ind = m
            .fake_device
            .next_mlme_msg::<fidl_mlme::EapolIndication>()
            .expect("error reading EAPOL.indication");
        assert_eq!(
            eapol_ind,
            fidl_mlme::EapolIndication { src_addr: [1; 6], dst_addr: [2; 6], data: vec![5; 200] }
        );
    }

    #[test]
    fn send_eapol_frame_success() {
        let mut m = MockObjects::new();
        let mut me = m.make_mlme();
        me.make_client_station();
        let mut client = me.get_bound_client().expect("client should be present");
        client.send_eapol_frame(IFACE_MAC, BSSID.0, false, &[5; 8]);

        // Verify EAPOL.confirm message was sent to SME.
        let eapol_confirm = m
            .fake_device
            .next_mlme_msg::<fidl_mlme::EapolConfirm>()
            .expect("error reading EAPOL.confirm");
        assert_eq!(
            eapol_confirm,
            fidl_mlme::EapolConfirm { result_code: fidl_mlme::EapolResultCodes::Success }
        );

        // Verify EAPoL frame was sent over the air.
        #[rustfmt::skip]
        assert_eq!(&m.fake_device.wlan_queue[0].0[..], &[
            // Data header:
            0b0000_10_00, 0b0000000_1, // FC
            0, 0, // Duration
            6, 6, 6, 6, 6, 6, // addr1
            7, 7, 7, 7, 7, 7, // addr2
            6, 6, 6, 6, 6, 6, // addr3
            0x10, 0, // Sequence Control
            // LLC header:
            0xaa, 0xaa, 0x03, // dsap ssap ctrl
            0x00, 0x00, 0x00, // oui
            0x88, 0x8E, // protocol id (EAPOL)
            // EAPoL PDU:
            5, 5, 5, 5, 5, 5, 5, 5,
        ][..]);
    }

    #[test]
    fn send_eapol_frame_failure() {
        let mut m = MockObjects::new();
        let device = m.fake_device.as_device_fail_wlan_tx();
        let mut me = m.make_mlme_with_device(device);
        me.make_client_station();
        let mut client = me.get_bound_client().expect("client should be present");
        client.send_eapol_frame([1; 6], [2; 6], false, &[5; 200]);

        // Verify EAPOL.confirm message was sent to SME.
        let eapol_confirm = m
            .fake_device
            .next_mlme_msg::<fidl_mlme::EapolConfirm>()
            .expect("error reading EAPOL.confirm");
        assert_eq!(
            eapol_confirm,
            fidl_mlme::EapolConfirm {
                result_code: fidl_mlme::EapolResultCodes::TransmissionFailure
            }
        );

        // Verify EAPoL frame was not sent over the air.
        assert!(m.fake_device.wlan_queue.is_empty());
    }

    #[test]
    fn send_ps_poll_frame() {
        let mut m = MockObjects::new();
        let mut me = m.make_mlme();
        me.make_client_station();
        let mut client = me.get_bound_client().expect("client should be present");
        client.send_ps_poll_frame(0xABCD).expect("failed sending PS POLL frame");

        // Verify ensure_on_channel. That is, scheduling scan request would not cause channel to be
        // switched right away, while frame is still being sent.
        me.on_sme_scan(scan_req());
        assert_eq!(me.ctx.device.channel(), MAIN_CHANNEL);
    }

    #[test]
    fn send_power_state_doze_frame_success() {
        let mut m = MockObjects::new();
        let mut me = m.make_mlme();
        let mut client = make_client_station();
        client
            .send_power_state_frame(&mut me.ctx, PowerState::DOZE)
            .expect("failed sending doze frame");
        client
            .send_power_state_frame(&mut me.ctx, PowerState::AWAKE)
            .expect("failed sending awake frame");

        // Verify no ensure_on_channel. That is, scheduling scan request would cause channel to be
        // switched right away.
        me.on_sme_scan(scan_req());
        assert_eq!(me.ctx.device.channel().primary, SCAN_CHANNEL_PRIMARY);
    }

    #[test]
    fn send_addba_req_frame() {
        let mut mock = MockObjects::new();
        let mut mlme = mock.make_mlme();
        mlme.make_client_station();
        let mut client = mlme.get_bound_client().expect("client should be present");

        let mut body = [0u8; 16];
        let mut writer = BufferWriter::new(&mut body[..]);
        block_ack::write_addba_req_body(&mut writer, 1)
            .and_then(|_| client.send_block_ack_frame(ADDBA_REQ_FRAME_LEN, writer.into_written()))
            .expect("failed sending addba frame");
        assert_eq!(
            &mock.fake_device.wlan_queue[0].0[..],
            &[
                // Mgmt header 1101 for action frame
                0b11010000, 0b00000000, // frame control
                0, 0, // duration
                6, 6, 6, 6, 6, 6, // addr1
                7, 7, 7, 7, 7, 7, // addr2
                6, 6, 6, 6, 6, 6, // addr3
                0x10, 0, // sequence control
                // Action frame header (Also part of ADDBA request frame)
                0x03, // Action Category: block ack (0x03)
                0x00, // block ack action: ADDBA request (0x00)
                1,    // block ack dialog token
                0b00000011, 0b00010000, // block ack parameters (u16)
                0, 0, // block ack timeout (u16) (0: disabled)
                0b00010000, 0, // block ack starting sequence number: fragment 0, sequence 1
            ][..]
        );
    }

    #[test]
    fn send_addba_resp_frame() {
        let mut mock = MockObjects::new();
        let mut mlme = mock.make_mlme();
        mlme.make_client_station();
        let mut client = mlme.get_bound_client().expect("client should be present");

        let mut body = [0u8; 16];
        let mut writer = BufferWriter::new(&mut body[..]);
        block_ack::write_addba_resp_body(&mut writer, 1)
            .and_then(|_| client.send_block_ack_frame(ADDBA_RESP_FRAME_LEN, writer.into_written()))
            .expect("failed sending addba frame");
        assert_eq!(
            &mock.fake_device.wlan_queue[0].0[..],
            &[
                // Mgmt header 1101 for action frame
                0b11010000, 0b00000000, // frame control
                0, 0, // duration
                6, 6, 6, 6, 6, 6, // addr1
                7, 7, 7, 7, 7, 7, // addr2
                6, 6, 6, 6, 6, 6, // addr3
                0x10, 0, // sequence control
                // Action frame header (Also part of ADDBA response frame)
                0x03, // Action Category: block ack (0x03)
                0x01, // block ack action: ADDBA response (0x01)
                1,    // block ack dialog token
                0, 0, // status
                0b00000011, 0b00010000, // block ack parameters (u16)
                0, 0, // block ack timeout (u16) (0: disabled)
            ][..]
        );
    }

    #[test]
    fn client_send_successful_associate_conf() {
        let mut m = MockObjects::new();
        let mut me = m.make_mlme();
        me.make_client_station();
        let mut client = me.get_bound_client().expect("client should be present");

        let mut ies = vec![];
        let rates = vec![1, 2, 3, 4, 5, 6, 7, 8, 9, 10];
        let rates_writer = ie::RatesWriter::try_new(&rates[..]).expect("Valid rates");
        // It should work even if ext_supp_rates shows up before supp_rates
        rates_writer.write_ext_supported_rates(&mut ies);
        rates_writer.write_supported_rates(&mut ies);
        ie::write_ht_capabilities(&mut ies, &ie::fake_ht_capabilities()).expect("Valid HT Cap");
        ie::write_vht_capabilities(&mut ies, &ie::fake_vht_capabilities()).expect("Valid VHT Cap");

        client.send_associate_conf_success(42, mac::CapabilityInfo(0x1234), &ies[..]);
        let associate_conf = m
            .fake_device
            .next_mlme_msg::<fidl_mlme::AssociateConfirm>()
            .expect("error reading Associate.confirm");
        assert_eq!(
            associate_conf,
            fidl_mlme::AssociateConfirm {
                association_id: 42,
                cap_info: 0x1234,
                result_code: fidl_mlme::AssociateResultCodes::Success,
                rates: vec![9, 10, 1, 2, 3, 4, 5, 6, 7, 8],
                wmm_param: None,
                ht_cap: Some(Box::new(fidl_mlme::HtCapabilities {
                    bytes: ie::fake_ht_capabilities().as_bytes().try_into().unwrap()
                })),
                vht_cap: Some(Box::new(fidl_mlme::VhtCapabilities {
                    bytes: ie::fake_vht_capabilities().as_bytes().try_into().unwrap()
                })),
            }
        );
    }

    #[test]
    fn client_send_failed_associate_conf() {
        let mut m = MockObjects::new();
        let mut me = m.make_mlme();
        me.make_client_station();
        let mut client = me.get_bound_client().expect("client should be present");
        client.send_associate_conf_failure(fidl_mlme::AssociateResultCodes::RefusedExternalReason);
        let associate_conf = m
            .fake_device
            .next_mlme_msg::<fidl_mlme::AssociateConfirm>()
            .expect("error reading Associate.confirm");
        assert_eq!(
            associate_conf,
            fidl_mlme::AssociateConfirm {
                association_id: 0,
                cap_info: 0,
                result_code: fidl_mlme::AssociateResultCodes::RefusedExternalReason,
                rates: vec![],
                wmm_param: None,
                ht_cap: None,
                vht_cap: None,
            }
        );
    }

    #[test]
    #[allow(deprecated)] // Needed for raw MLME message until main loop lives in Rust
    fn mlme_respond_to_stats_query_with_empty_response() {
        let mut m = MockObjects::new();
        let mut me = m.make_mlme();
        let stats_query_req = fidl_mlme::MlmeRequestMessage::StatsQueryReq {};
        let result = me.handle_mlme_msg(stats_query_req);
        assert_variant!(result, Ok(()));
        let stats_query_resp = m
            .fake_device
            .next_mlme_msg::<fidl_mlme::StatsQueryResponse>()
            .expect("Should receive a stats query response");
        assert_eq!(stats_query_resp, stats::empty_stats_query_response());
    }

    #[test]
    fn unicast_addresses() {
        assert!(is_unicast([0; 6]));
        assert!(is_unicast([0xfe; 6]));
    }

    #[test]
    fn non_unicast_addresses() {
        assert!(!is_unicast([0xff; 6])); // broadcast
        assert!(!is_unicast([0x33, 0x33, 0, 0, 0, 0])); // IPv6 multicast
        assert!(!is_unicast([0x01, 0x00, 0x53, 0, 0, 0])); // IPv4 multicast
    }

    #[test]
    fn drop_mgmt_frame_wrong_bssid() {
        let frame = [
            // Mgmt header 1101 for action frame
            0b11010000, 0b00000000, // frame control
            0, 0, // duration
            7, 7, 7, 7, 7, 7, // addr1
            6, 6, 6, 6, 6, 6, // addr2
            0, 0, 0, 0, 0, 0, // addr3 (bssid should have been [6; 6])
            0x10, 0, // sequence control
        ];
        let frame = mac::MacFrame::parse(&frame[..], false).unwrap();
        assert_eq!(false, make_client_station().should_handle_frame(&frame));
    }

    #[test]
    fn drop_mgmt_frame_wrong_dst_addr() {
        let frame = [
            // Mgmt header 1101 for action frame
            0b11010000, 0b00000000, // frame control
            0, 0, // duration
            0, 0, 0, 0, 0, 0, // addr1 (dst_addr should have been [7; 6])
            6, 6, 6, 6, 6, 6, // addr2
            6, 6, 6, 6, 6, 6, // addr3
            0x10, 0, // sequence control
        ];
        let frame = mac::MacFrame::parse(&frame[..], false).unwrap();
        assert_eq!(false, make_client_station().should_handle_frame(&frame));
    }

    #[test]
    fn mgmt_frame_ok_broadcast() {
        let frame = [
            // Mgmt header 1101 for action frame
            0b11010000, 0b00000000, // frame control
            0, 0, // duration
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, // addr1 (dst_addr is broadcast)
            6, 6, 6, 6, 6, 6, // addr2
            6, 6, 6, 6, 6, 6, // addr3
            0x10, 0, // sequence control
        ];
        let frame = mac::MacFrame::parse(&frame[..], false).unwrap();
        assert_eq!(true, make_client_station().should_handle_frame(&frame));
    }

    #[test]
    fn mgmt_frame_ok_client_addr() {
        let frame = [
            // Mgmt header 1101 for action frame
            0b11010000, 0b00000000, // frame control
            0, 0, // duration
            7, 7, 7, 7, 7, 7, // addr1 (dst_addr should have been [7; 6])
            6, 6, 6, 6, 6, 6, // addr2
            6, 6, 6, 6, 6, 6, // addr3
            0x10, 0, // sequence control
        ];
        let frame = mac::MacFrame::parse(&frame[..], false).unwrap();
        assert_eq!(true, make_client_station().should_handle_frame(&frame));
    }

    #[test]
    fn drop_data_frame_wrong_bssid() {
        let frame = [
            // Data header 0100
            0b01001000,
            0b00000010, // frame control. right 2 bits of octet 2: from_ds(1), to_ds(0)
            0, 0, // duration
            7, 7, 7, 7, 7, 7, // addr1 (dst_addr)
            0, 0, 0, 0, 0, 0, // addr2 (bssid should have been [6; 6])
            6, 6, 6, 6, 6, 6, // addr3
            0x10, 0, // sequence control
        ];
        let frame = mac::MacFrame::parse(&frame[..], false).unwrap();
        assert_eq!(false, make_client_station().should_handle_frame(&frame));
    }

    #[test]
    fn drop_data_frame_wrong_dst_addr() {
        let frame = [
            // Data header 0100
            0b01001000,
            0b00000010, // frame control. right 2 bits of octet 2: from_ds(1), to_ds(0)
            0, 0, // duration
            0, 0, 0, 0, 0, 0, // addr1 (dst_addr should have been [7; 6])
            6, 6, 6, 6, 6, 6, // addr2 (bssid)
            6, 6, 6, 6, 6, 6, // addr3
            0x10, 0, // sequence control
        ];
        let frame = mac::MacFrame::parse(&frame[..], false).unwrap();
        assert_eq!(false, make_client_station().should_handle_frame(&frame));
    }

    #[test]
    fn data_frame_ok_broadcast() {
        let frame = [
            // Data header 0100
            0b01001000,
            0b00000010, // frame control. right 2 bits of octet 2: from_ds(1), to_ds(0)
            0, 0, // duration
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, // addr1 (dst_addr is broadcast)
            6, 6, 6, 6, 6, 6, // addr2 (bssid)
            6, 6, 6, 6, 6, 6, // addr3
            0x10, 0, // sequence control
        ];
        let frame = mac::MacFrame::parse(&frame[..], false).unwrap();
        assert_eq!(true, make_client_station().should_handle_frame(&frame));
    }

    #[test]
    fn data_frame_ok_client_addr() {
        let frame = [
            // Data header 0100
            0b01001000,
            0b00000010, // frame control. right 2 bits of octet 2: from_ds(1), to_ds(0)
            0, 0, // duration
            7, 7, 7, 7, 7, 7, // addr1 (dst_addr)
            6, 6, 6, 6, 6, 6, // addr2 (bssid)
            6, 6, 6, 6, 6, 6, // addr3
            0x10, 0, // sequence control
        ];
        let frame = mac::MacFrame::parse(&frame[..], false).unwrap();
        assert_eq!(true, make_client_station().should_handle_frame(&frame));
    }
}
