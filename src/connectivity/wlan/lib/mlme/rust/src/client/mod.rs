// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod channel_listener;
mod channel_scheduler;
mod convert_beacon;
mod frame_writer;
mod lost_bss;
mod scanner;
mod state;
pub mod temporary_c_binding;

use {
    crate::{
        buffer::{BufferProvider, OutBuf},
        device::{Device, TxFlags},
        error::Error,
        logger,
        timer::*,
        write_eth_frame,
    },
    anyhow::format_err,
    banjo_ddk_protocol_wlan_info as banjo_wlan_info, banjo_ddk_protocol_wlan_mac as banjo_wlan_mac,
    channel_listener::{ChannelListenerSource, ChannelListenerState},
    channel_scheduler::ChannelScheduler,
    fidl_fuchsia_wlan_mlme as fidl_mlme,
    frame_writer::*,
    fuchsia_zircon as zx,
    log::{error, warn},
    lost_bss::LostBssCounter,
    scanner::Scanner,
    state::States,
    static_assertions::assert_eq_size,
    std::convert::TryInto,
    wlan_common::{
        buffer_writer::BufferWriter,
        frame_len,
        ie::{
            self, parse_ht_capabilities, parse_vht_capabilities, rsn::rsne, Id, Reader,
            IE_PREFIX_LEN, SUPPORTED_RATES_MAX_LEN,
        },
        mac::{self, Aid, Bssid, MacAddr, OptionalField, PowerState, Presence},
        sequence::SequenceManager,
        TimeUnit,
    },
    zerocopy::{AsBytes, ByteSlice},
};

pub use scanner::ScanError;

/// Maximum size of EAPOL frames forwarded to SME.
/// TODO(34845): Evaluate whether EAPOL size restriction is needed.
const MAX_EAPOL_FRAME_LEN: usize = 255;

/// Number of beacon intervals which beacon is not seen before we declare BSS as lost
const DEFAULT_AUTO_DEAUTH_TIMEOUT_BEACON_COUNT: u32 = 100;

#[derive(Debug, PartialEq)]
pub enum TimedEvent {
    /// Authentication timed out. AP did not complete authentication in time.
    Authenticating,
    /// Association timed out. AP did not complete association in time.
    Associating,
    ChannelScheduler,
    ScannerProbeDelay(banjo_wlan_info::WlanChannel),
    LostBssCountdown,
}

/// ClientConfig affects time duration used for different timeouts.
/// Originally added to more easily control behavior in tests.
#[repr(C)]
#[derive(Debug, Clone)]
pub struct ClientConfig {
    signal_report_beacon_timeout: usize,
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
        // TODO(41417): Remove this once devmgr installs a Rust logger.
        logger::install();

        let iface_mac = device.wlan_info().ifc_info.mac_addr;
        let timer = Timer::<TimedEvent>::new(scheduler);
        Self {
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

    pub fn on_mac_frame(
        &mut self,
        sta: Option<&mut Client>,
        frame: &[u8],
        rx_info: Option<banjo_wlan_mac::WlanRxInfo>,
    ) {
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
                        if let Some(sta) = sta {
                            if bssid == sta.bssid {
                                sta.reset_lost_bss_timeout(&mut self.ctx);
                            }
                        }
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
        // TODO(29063): Call Client::on_mac_frame here once station is fully ported to Rust
    }

    #[allow(deprecated)] // Allow until main message loop is in Rust.
    pub fn handle_mlme_msg(
        &mut self,
        sta: Option<&mut Client>,
        msg: fidl_mlme::MlmeRequestMessage,
    ) -> Result<(), Error> {
        use fidl_mlme::MlmeRequestMessage as MlmeMsg;

        match msg {
            // Handle non station specific MLME messages first (Join, Scan, etc.)
            MlmeMsg::StartScan { req } => {
                self.on_sme_scan(sta, req);
                Ok(())
            }
            _ => {
                // TODO(eyw): Do not use Rust MLME to handle MLME message until we are ready.
                const DISABLE_RUST_MLME_MSG_SUPPORT: bool = true;

                if DISABLE_RUST_MLME_MSG_SUPPORT {
                    return Err(Error::Status(
                        format!("defer MLME messages to C++ handler during Rust migration"),
                        // Note: important to use NOT_SUPPORTED status while migrating MLME to Rust.
                        // C++ dispatcher uses it to determine whether to handle MLME msg in C++
                        zx::Status::NOT_SUPPORTED,
                    ));
                }
                match sta {
                    None => {
                        Err(Error::Status(format!("No client sta."), zx::Status::NOT_SUPPORTED))
                    }
                    Some(sta) => Ok(sta
                        .bind(&mut self.scanner, &mut self.chan_sched, &mut self.channel_state)
                        .handle_mlme_msg(&mut self.ctx, msg)),
                }
            }
        }
    }

    fn on_sme_scan(&mut self, sta: Option<&mut Client>, req: fidl_mlme::ScanRequest) {
        let channel_state = &mut self.channel_state;
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

    /// Called when a previously scheduled `TimedEvent` fired.
    /// Return true if auto-deauth has triggered. Return false otherwise.
    pub fn handle_timed_event(&mut self, sta: Option<&mut Client>, event_id: EventId) -> bool {
        let event = match self.ctx.timer.triggered(&event_id) {
            Some(event) => event,
            None => {
                error!(
                    "event for given ID already consumed;\
                     this should NOT happen - ignoring event"
                );
                return false;
            }
        };

        match event {
            TimedEvent::ChannelScheduler => {
                let mut listener = self.channel_state.bind(&mut self.ctx, &mut self.scanner, sta);
                // We are not scheduling new event, so it doesn't matter what source we bind here
                let mut chan_sched =
                    self.chan_sched.bind(&mut listener, ChannelListenerSource::Others);
                chan_sched.handle_timeout();
            }
            TimedEvent::ScannerProbeDelay(channel) => {
                self.scanner.bind(&mut self.ctx).handle_probe_delay_timeout(channel);
            }
            _ => {
                if let Some(sta) = sta {
                    return sta
                        .bind(&mut self.scanner, &mut self.chan_sched, &mut self.channel_state)
                        .handle_timed_event(&mut self.ctx, event_id, event);
                }
            }
        }
        false
    }
}

/// A STA running in Client mode.
/// The Client STA is in its early development process and does not yet manage its internal state
/// machine or track negotiated capabilities.
pub struct Client {
    state: Option<States>,
    /// |lost_bss_counter|, which is started when client is associated. Used to keep track whether
    /// BSS is still alive nearby.
    /// This is only here because state handling still lives in C++ MLME.
    /// TODO(29063): Move this to Associated state when we use Rust MLME state
    pub(crate) lost_bss_counter: Option<LostBssCounter>,
    pub bssid: Bssid,
    pub iface_mac: MacAddr,
    pub is_rsn: bool,
}

impl Client {
    pub fn new(bssid: Bssid, iface_mac: MacAddr, is_rsn: bool) -> Self {
        Self {
            bssid,
            iface_mac,
            state: Some(States::new_initial()),
            lost_bss_counter: None,
            is_rsn,
        }
    }

    pub fn bind<'a>(
        &'a mut self,
        scanner: &'a mut Scanner,
        chan_sched: &'a mut ChannelScheduler,
        channel_state: &'a mut ChannelListenerState,
    ) -> BoundClient<'a> {
        BoundClient { sta: self, scanner, chan_sched, channel_state }
    }

    pub fn pre_switch_off_channel(&mut self, ctx: &mut Context) {
        // Right now MLME does not use Rust's Associated state yet. If lost_bss_counter is
        // present, it means that MLME is connected.
        if self.lost_bss_counter.is_some() {
            if let Err(e) = send_power_state_frame(self, ctx, PowerState::DOZE) {
                warn!("unable to send doze frame: {:?}", e);
            }
            self.lost_bss_counter.as_mut().unwrap().pause(&mut ctx.timer);
        }
    }

    pub fn handle_back_on_channel(&mut self, ctx: &mut Context) {
        // Right now MLME does not use Rust's Associated state yet. If lost_bss_counter is
        // present, it means that MLME is connected.
        if self.lost_bss_counter.is_some() {
            if let Err(e) = send_power_state_frame(self, ctx, PowerState::AWAKE) {
                warn!("unable to send awake frame: {:?}", e);
            }
            // Safe to unwrap because we already checked that lost_bss_counter exists
            self.lost_bss_counter.as_mut().unwrap().unpause(&mut ctx.timer);
        }
    }

    pub fn start_lost_bss_counter(&mut self, ctx: &mut Context, beacon_period: TimeUnit) {
        let lost_bss_counter = LostBssCounter::start(
            &mut ctx.timer,
            beacon_period.into(),
            DEFAULT_AUTO_DEAUTH_TIMEOUT_BEACON_COUNT,
        );
        self.lost_bss_counter = Some(lost_bss_counter);
    }

    pub fn stop_lost_bss_counter(&mut self) {
        self.lost_bss_counter.take();
    }

    fn reset_lost_bss_timeout(&mut self, ctx: &mut Context) {
        if let Some(lost_bss_counter) = self.lost_bss_counter.as_mut() {
            lost_bss_counter.reset_timeout(&mut ctx.timer);
        }
    }
}

pub struct BoundClient<'a> {
    sta: &'a mut Client,
    scanner: &'a mut Scanner,
    chan_sched: &'a mut ChannelScheduler,
    channel_state: &'a mut ChannelListenerState,
}

impl<'a> BoundClient<'a> {
    pub fn authenticate(&mut self, ctx: &mut Context, timeout_bcn_count: u8) {
        // Safe: |state| is never None and always replaced with Some(..).
        self.sta.state =
            Some(self.sta.state.take().unwrap().authenticate(self, ctx, timeout_bcn_count));
    }

    // TODO(hahnr): Take MLME-ASSOCIATE.request as parameter.
    pub fn associate(&mut self, ctx: &mut Context) {
        // Safe: |state| is never None and always replaced with Some(..).
        self.sta.state = Some(self.sta.state.take().unwrap().associate(self, ctx));
    }

    /// Extracts aggregated and non-aggregated MSDUs from the data frame.
    /// Handles all data subtypes.
    /// EAPoL MSDUs are forwarded to SME via an MLME-EAPOL.indication message independent of the
    /// STA's current controlled port status.
    /// All other MSDUs are converted into Ethernet II frames and forwarded via the device to
    /// Fuchsia's Netstack if the STA's controlled port is open.
    /// NULL-Data frames are interpreted as "Keep Alive" requests and responded with NULL data
    /// frames if the STA's controlled port is open.
    // TODO(42080): Move entire logic into Associated state once C++ version no longer depends on
    // this function.
    pub fn handle_data_frame<B: ByteSlice>(
        &mut self,
        ctx: &mut Context,
        fixed_data_fields: &mac::FixedDataHdrFields,
        addr4: Option<mac::Addr4>,
        qos_ctrl: Option<mac::QosControl>,
        body: B,
        is_controlled_port_open: bool,
    ) {
        let msdus =
            mac::MsduIterator::from_data_frame_parts(*fixed_data_fields, addr4, qos_ctrl, body);
        match msdus {
            // Handle NULL data frames independent of the controlled port's status.
            mac::MsduIterator::Null => {
                if let Err(e) = self.send_keep_alive_resp_frame(ctx) {
                    error!("error sending keep alive frame: {}", e);
                }
            }
            // Handle aggregated and non-aggregated MSDUs.
            _ => {
                for msdu in msdus {
                    let mac::Msdu { dst_addr, src_addr, llc_frame } = &msdu;
                    match llc_frame.hdr.protocol_id.to_native() {
                        // Forward EAPoL frames to SME independent of the controlled port's
                        // status.
                        mac::ETHER_TYPE_EAPOL => {
                            if let Err(e) = self.send_eapol_indication(
                                ctx,
                                *src_addr,
                                *dst_addr,
                                &llc_frame.body[..],
                            ) {
                                error!("error sending MLME-EAPOL.indication: {}", e);
                            }
                        }
                        // Deliver non-EAPoL MSDUs only if the controlled port is open.
                        _ if is_controlled_port_open => {
                            if let Err(e) = self.deliver_msdu(ctx, msdu) {
                                error!("error while handling data frame: {}", e);
                            }
                        }
                        // Drop all non-EAPoL MSDUs if the controlled port is closed.
                        _ => (),
                    }
                }
            }
        }
    }

    /// Delivers a single MSDU to the STA's underlying device. The MSDU is delivered as an
    /// Ethernet II frame.
    /// Returns Err(_) if writing or delivering the Ethernet II frame failed.
    fn deliver_msdu<B: ByteSlice>(
        &mut self,
        ctx: &mut Context,
        msdu: mac::Msdu<B>,
    ) -> Result<(), Error> {
        let mac::Msdu { dst_addr, src_addr, llc_frame } = msdu;

        let mut buf = [0u8; mac::MAX_ETH_FRAME_LEN];
        let mut writer = BufferWriter::new(&mut buf[..]);
        write_eth_frame(
            &mut writer,
            dst_addr,
            src_addr,
            llc_frame.hdr.protocol_id.to_native(),
            &llc_frame.body,
        )?;
        ctx.device
            .deliver_eth_frame(writer.into_written())
            .map_err(|s| Error::Status(format!("could not deliver Ethernet II frame"), s))
    }

    /// Sends an authentication frame using Open System authentication.
    pub fn send_open_auth_frame(&mut self, ctx: &mut Context) -> Result<(), Error> {
        const FRAME_LEN: usize = frame_len!(mac::MgmtHdr, mac::AuthHdr);
        let mut buf = ctx.buf_provider.get_buffer(FRAME_LEN)?;
        let mut w = BufferWriter::new(&mut buf[..]);
        write_open_auth_frame(&mut w, self.sta.bssid, self.sta.iface_mac, &mut ctx.seq_mgr)?;
        let bytes_written = w.bytes_written();
        let out_buf = OutBuf::from(buf, bytes_written);
        self.send_mgmt_or_ctrl_frame(ctx, out_buf)
            .map_err(|s| Error::Status(format!("error sending open auth frame"), s))
    }

    /// Sends an association request frame based on device capability.
    // TODO(fxb/39148): Use an IE set instead of individual IEs.
    pub fn send_assoc_req_frame(
        &mut self,
        ctx: &mut Context,
        cap_info: u16,
        ssid: &[u8],
        rates: &[u8],
        rsne: &[u8],
        ht_cap: &[u8],
        vht_cap: &[u8],
    ) -> Result<(), Error> {
        let frame_len = frame_len!(mac::MgmtHdr, mac::AssocReqHdr);
        let ssid_len = IE_PREFIX_LEN + ssid.len();
        let rates_len = (IE_PREFIX_LEN + rates.len())
                    // If there are too many rates, they will be split into two IEs.
                    // In this case, the total length would be the sum of:
                    // 1) 1st IE: IE_PREFIX_LEN + SUPPORTED_RATES_MAX_LEN
                    // 2) 2nd IE: IE_PREFIX_LEN + rates().len - SUPPORTED_RATES_MAX_LEN
                    // The total length is IE_PREFIX_LEN + rates.len() + IE_PREFIX_LEN.
                    + if rates.len() > SUPPORTED_RATES_MAX_LEN { IE_PREFIX_LEN } else { 0 };
        let rsne_len = rsne.len(); // RSNE already contains ID/len
        let ht_cap_len = if ht_cap.is_empty() { 0 } else { IE_PREFIX_LEN + ht_cap.len() };
        let vht_cap_len = if vht_cap.is_empty() { 0 } else { IE_PREFIX_LEN + vht_cap.len() };
        let frame_len = frame_len + ssid_len + rates_len + rsne_len + ht_cap_len + vht_cap_len;
        let mut buf = ctx.buf_provider.get_buffer(frame_len)?;
        let mut w = BufferWriter::new(&mut buf[..]);

        let rsne = if rsne.is_empty() {
            None
        } else {
            Some(
                rsne::from_bytes(rsne)
                    .map_err(|e| format_err!("error parsing rsne {:?} : {:?}", rsne, e))?
                    .1,
            )
        };

        let ht_cap = if ht_cap.is_empty() { None } else { Some(*parse_ht_capabilities(ht_cap)?) };
        let vht_cap =
            if vht_cap.is_empty() { None } else { Some(*parse_vht_capabilities(vht_cap)?) };

        write_assoc_req_frame(
            &mut w,
            self.sta.bssid,
            self.sta.iface_mac,
            &mut ctx.seq_mgr,
            mac::CapabilityInfo(cap_info),
            ssid,
            rates,
            rsne,
            ht_cap,
            vht_cap,
        )?;
        let bytes_written = w.bytes_written();
        let out_buf = OutBuf::from(buf, bytes_written);
        self.send_mgmt_or_ctrl_frame(ctx, out_buf)
            .map_err(|s| Error::Status(format!("error sending assoc req frame"), s))
    }

    /// Sends a "keep alive" response to the BSS. A keep alive response is a NULL data frame sent as
    /// a response to the AP transmitting NULL data frames to the client.
    // Note: This function was introduced to meet C++ MLME feature parity. However, there needs to
    // be some investigation, whether these "keep alive" frames are the right way of keeping a
    // client associated to legacy APs.
    fn send_keep_alive_resp_frame(&mut self, ctx: &mut Context) -> Result<(), Error> {
        const FRAME_LEN: usize = frame_len!(mac::FixedDataHdrFields);
        let mut buf = ctx.buf_provider.get_buffer(FRAME_LEN)?;
        let mut w = BufferWriter::new(&mut buf[..]);
        write_keep_alive_resp_frame(&mut w, self.sta.bssid, self.sta.iface_mac, &mut ctx.seq_mgr)?;
        let bytes_written = w.bytes_written();
        let out_buf = OutBuf::from(buf, bytes_written);
        ctx.device
            .send_wlan_frame(out_buf, TxFlags::NONE)
            .map_err(|s| Error::Status(format!("error sending keep alive frame"), s))
    }

    /// Sends a deauthentication notification to the joined BSS with the given `reason_code`.
    pub fn send_deauth_frame(
        &mut self,
        ctx: &mut Context,
        reason_code: mac::ReasonCode,
    ) -> Result<(), Error> {
        const FRAME_LEN: usize = frame_len!(mac::MgmtHdr, mac::DeauthHdr);
        let mut buf = ctx.buf_provider.get_buffer(FRAME_LEN)?;
        let mut w = BufferWriter::new(&mut buf[..]);
        write_deauth_frame(
            &mut w,
            self.sta.bssid,
            self.sta.iface_mac,
            reason_code,
            &mut ctx.seq_mgr,
        )?;
        let bytes_written = w.bytes_written();
        let out_buf = OutBuf::from(buf, bytes_written);
        self.send_mgmt_or_ctrl_frame(ctx, out_buf)
            .map_err(|s| Error::Status(format!("error sending deauthenticate frame"), s))
    }

    /// Sends the given payload as a data frame over the air.
    pub fn send_data_frame(
        &mut self,
        ctx: &mut Context,
        src: MacAddr,
        dst: MacAddr,
        is_protected: bool,
        is_qos: bool,
        ether_type: u16,
        payload: &[u8],
    ) -> Result<(), Error> {
        let qos_presence = Presence::from_bool(is_qos);
        let data_hdr_len =
            mac::FixedDataHdrFields::len(mac::Addr4::ABSENT, qos_presence, mac::HtControl::ABSENT);
        let frame_len = data_hdr_len + std::mem::size_of::<mac::LlcHdr>() + payload.len();
        let mut buf = ctx.buf_provider.get_buffer(frame_len)?;
        let mut w = BufferWriter::new(&mut buf[..]);
        write_data_frame(
            &mut w,
            &mut ctx.seq_mgr,
            self.sta.bssid,
            src,
            dst,
            is_protected,
            is_qos,
            ether_type,
            payload,
        )?;
        let bytes_written = w.bytes_written();
        let out_buf = OutBuf::from(buf, bytes_written);
        let tx_flags = match ether_type {
            mac::ETHER_TYPE_EAPOL => TxFlags::FAVOR_RELIABILITY,
            _ => TxFlags::NONE,
        };
        ctx.device
            .send_wlan_frame(out_buf, tx_flags)
            .map_err(|s| Error::Status(format!("error sending data frame"), s))
    }

    /// Sends an MLME-EAPOL.indication to MLME's SME peer.
    /// Note: MLME-EAPOL.indication is a custom Fuchsia primitive and not defined in IEEE 802.11.
    fn send_eapol_indication(
        &mut self,
        ctx: &mut Context,
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
        ctx.device.access_sme_sender(|sender| {
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
        ctx: &mut Context,
        src: MacAddr,
        dst: MacAddr,
        is_protected: bool,
        eapol_frame: &[u8],
    ) {
        // TODO(34910): EAPoL frames can be send in QoS data frames. However, Fuchsia's old C++
        // MLME never sent EAPoL frames in QoS data frames. For feature parity do the same.
        let result = self.send_data_frame(
            ctx,
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
        let result = ctx.device.access_sme_sender(|sender| {
            sender.send_eapol_conf(&mut fidl_mlme::EapolConfirm { result_code })
        });
        if let Err(e) = result {
            error!("error sending MLME-EAPOL.confirm message: {}", e);
        }
    }

    pub fn send_ps_poll_frame(&mut self, ctx: &mut Context, aid: Aid) -> Result<(), Error> {
        const FRAME_LEN: usize = frame_len!(mac::PsPoll);
        let mut buf = ctx.buf_provider.get_buffer(FRAME_LEN)?;
        let mut w = BufferWriter::new(&mut buf[..]);
        write_ps_poll_frame(&mut w, aid, self.sta.bssid, self.sta.iface_mac)?;
        let bytes_written = w.bytes_written();
        let out_buf = OutBuf::from(buf, bytes_written);
        self.send_mgmt_or_ctrl_frame(ctx, out_buf)
            .map_err(|s| Error::Status(format!("error sending PS-Poll frame"), s))
    }

    /// Sends an `ADDBA` request to the associated AP to begin a BlockAck
    /// session. This frame is sent to initiate a BlockAck session with the AP
    /// and is described by 802.11-2016, 9.6.5.2.
    ///
    /// # Errors
    ///
    /// Returns an error if the management frame cannot be sent to the AP.
    pub fn send_addba_req_frame(&mut self, ctx: &mut Context) -> Result<(), Error> {
        const FRAME_LEN: usize = frame_len!(mac::MgmtHdr, mac::ActionHdr, mac::AddbaReqHdr);
        let mut buf = ctx.buf_provider.get_buffer(FRAME_LEN)?;
        let mut w = BufferWriter::new(&mut buf[..]);
        // TODO(29887): It appears there is no particular rule to choose the
        //              value for `dialog_token`. Persist the dialog token for
        //              the BlockAck session and find a proven way to generate
        //              tokens.  See IEEE Std 802.11-2016, 9.6.5.2.
        let dialog_token = 1;
        write_addba_req_frame(
            &mut w,
            self.sta.bssid,
            self.sta.iface_mac,
            dialog_token,
            &mut ctx.seq_mgr,
        )?;
        let bytes_written = w.bytes_written();
        let out_buf = OutBuf::from(buf, bytes_written);
        self.send_mgmt_or_ctrl_frame(ctx, out_buf)
            .map_err(|s| Error::Status(format!("error sending addba request frame"), s))
    }

    /// Sends an `ADDBA` response to the associated AP to begin a BlockAck
    /// session. This frame is sent in response to an `ADDBA` request from the
    /// AP and is described by 802.11-2016, 9.6.5.3.
    ///
    /// # Errors
    ///
    /// Returns an error if the management frame cannot be sent to the AP.
    pub fn send_addba_resp_frame(
        &mut self,
        ctx: &mut Context,
        dialog_token: u8,
    ) -> Result<(), Error> {
        const FRAME_LEN: usize = frame_len!(mac::MgmtHdr, mac::ActionHdr, mac::AddbaRespHdr);
        let mut buf = ctx.buf_provider.get_buffer(FRAME_LEN)?;
        let mut w = BufferWriter::new(&mut buf[..]);
        write_addba_resp_frame(
            &mut w,
            self.sta.bssid,
            self.sta.iface_mac,
            dialog_token,
            &mut ctx.seq_mgr,
        )?;
        let bytes_written = w.bytes_written();
        let out_buf = OutBuf::from(buf, bytes_written);
        self.send_mgmt_or_ctrl_frame(ctx, out_buf)
            .map_err(|s| Error::Status(format!("error sending addba response frame"), s))
    }

    /// Called when a previously scheduled `TimedEvent` fired.
    pub fn handle_timed_event(
        &mut self,
        ctx: &mut Context,
        event_id: EventId,
        event: TimedEvent,
    ) -> bool {
        match event {
            TimedEvent::LostBssCountdown => {
                if let Some(lost_bss_counter) = self.sta.lost_bss_counter.as_mut() {
                    let auto_deauth = lost_bss_counter.handle_timeout(&mut ctx.timer, event_id);
                    if auto_deauth {
                        self.send_deauthenticate_ind(
                            ctx,
                            fidl_mlme::ReasonCode::LeavingNetworkDeauth,
                        );
                        if let Err(e) =
                            self.send_deauth_frame(ctx, mac::ReasonCode::LEAVING_NETWORK_DEAUTH)
                        {
                            warn!("Failed sending deauth frame {:?}", e);
                        }
                    }
                    return auto_deauth;
                }
            }
            _ => {
                // Safe: |state| is never None and always replaced with Some(..).
                self.sta.state =
                    Some(self.sta.state.take().unwrap().on_timed_event(self, ctx, event))
            }
        }
        false
    }

    /// Called when an arbitrary frame was received over the air.
    pub fn on_mac_frame<B: ByteSlice>(&mut self, ctx: &mut Context, bytes: B, body_aligned: bool) {
        // Safe: |state| is never None and always replaced with Some(..).
        self.sta.state =
            Some(self.sta.state.take().unwrap().on_mac_frame(self, ctx, bytes, body_aligned));
    }

    pub fn on_eth_frame<B: ByteSlice>(&mut self, ctx: &mut Context, frame: B) -> Result<(), Error> {
        let (state, result) = self.sta.state.take().unwrap().on_eth_frame(self, ctx, frame);
        self.sta.state = Some(state);
        result
    }

    #[allow(deprecated)] // Allow until main message loop is in Rust.
    pub fn handle_mlme_msg(&mut self, ctx: &mut Context, msg: fidl_mlme::MlmeRequestMessage) {
        // Safe: |state| is never None and always replaced with Some(..).
        let state = self.sta.state.take().unwrap().handle_mlme_msg(self, ctx, msg);
        self.sta.state.replace(state);
    }

    /// Sends an MLME-AUTHENTICATE.confirm message to the SME with authentication type
    /// `Open System` as only open authentication is supported.
    fn send_authenticate_conf(
        &mut self,
        ctx: &mut Context,
        result_code: fidl_mlme::AuthenticateResultCodes,
    ) {
        let result = ctx.device.access_sme_sender(|sender| {
            sender.send_authenticate_conf(&mut fidl_mlme::AuthenticateConfirm {
                peer_sta_address: self.sta.bssid.0,
                auth_type: fidl_mlme::AuthenticationTypes::OpenSystem,
                result_code,
                auth_content: None,
            })
        });
        if let Err(e) = result {
            error!("error sending MLME-AUTHENTICATE.confirm: {}", e);
        }
    }

    /// Sends an MLME-ASSOCIATE.confirm message to the SME.
    fn send_associate_conf_failure(
        &mut self,
        ctx: &mut Context,
        result_code: fidl_mlme::AssociateResultCodes,
    ) {
        // AID used for reporting failed associations to SME.
        const FAILED_ASSOCIATION_AID: mac::Aid = 0;

        let mut assoc_conf = fidl_mlme::AssociateConfirm {
            association_id: FAILED_ASSOCIATION_AID,
            cap_info: 0,
            result_code,
            rates: vec![],
            ht_cap: None,
            vht_cap: None,
        };

        let result =
            ctx.device.access_sme_sender(|sender| sender.send_associate_conf(&mut assoc_conf));
        if let Err(e) = result {
            error!("error sending MLME-AUTHENTICATE.confirm: {}", e);
        }
    }

    fn send_associate_conf_success<B: ByteSlice>(
        &mut self,
        ctx: &mut Context,
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
                _ => {}
            }
        }

        let result =
            ctx.device.access_sme_sender(|sender| sender.send_associate_conf(&mut assoc_conf));
        if let Err(e) = result {
            error!("error sending MLME-AUTHENTICATE.confirm: {}", e);
        }
    }

    /// Sends an MLME-DEAUTHENTICATE.indication message to the joined BSS.
    fn send_deauthenticate_ind(&mut self, ctx: &mut Context, reason_code: fidl_mlme::ReasonCode) {
        let result = ctx.device.access_sme_sender(|sender| {
            sender.send_deauthenticate_ind(&mut fidl_mlme::DeauthenticateIndication {
                peer_sta_address: self.sta.bssid.0,
                reason_code,
            })
        });
        if let Err(e) = result {
            error!("error sending MLME-DEAUTHENTICATE.indication: {}", e);
        }
    }

    /// Sends an MLME-DISASSOCIATE.indication message to the joined BSS.
    fn send_disassoc_ind(&mut self, ctx: &mut Context, reason_code: fidl_mlme::ReasonCode) {
        let result = ctx.device.access_sme_sender(|sender| {
            sender.send_disassociate_ind(&mut fidl_mlme::DisassociateIndication {
                peer_sta_address: self.sta.bssid.0,
                reason_code: reason_code.into_primitive(),
            })
        });
        if let Err(e) = result {
            error!("error sending MLME-DEAUTHENTICATE.indication: {}", e);
        }
    }

    fn send_mgmt_or_ctrl_frame(
        &mut self,
        ctx: &mut Context,
        out_buf: OutBuf,
    ) -> Result<(), zx::Status> {
        self.ensure_on_channel(ctx);
        ctx.device.send_wlan_frame(out_buf, TxFlags::NONE)
    }

    fn ensure_on_channel(&mut self, ctx: &mut Context) {
        match self.channel_state.main_channel {
            Some(main_channel) => {
                let duration = zx::Duration::from_nanos(ctx.config.ensure_on_channel_time);
                let mut listener = self.channel_state.bind(ctx, self.scanner, Some(self.sta));
                self.chan_sched
                    .bind(&mut listener, ChannelListenerSource::Others)
                    .schedule_immediate(main_channel, duration);
            }
            None => warn!("main channel not set, cannot ensure on channel"),
        }
    }
}

/// Sends a power management data frame to the associated AP indicating that the client has
/// entered the given power state. See `PowerState`.
///
/// # Errors
///
/// Returns an error if the data frame cannot be sent to the AP.
pub fn send_power_state_frame(
    sta: &Client,
    ctx: &mut Context,
    state: PowerState,
) -> Result<(), Error> {
    let mut buffer = ctx.buf_provider.get_buffer(mac::FixedDataHdrFields::len(
        mac::Addr4::ABSENT,
        mac::QosControl::ABSENT,
        mac::HtControl::ABSENT,
    ))?;
    let mut writer = BufferWriter::new(&mut buffer[..]);
    write_power_state_frame(&mut writer, sta.bssid, sta.iface_mac, &mut ctx.seq_mgr, state)?;
    let n = writer.bytes_written();
    let buffer = OutBuf::from(buffer, n);
    ctx.device
        .send_wlan_frame(buffer, TxFlags::NONE)
        .map_err(|error| Error::Status(format!("error sending power management frame"), error))
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{buffer::FakeBufferProvider, device::FakeDevice},
        fuchsia_zircon::prelude::DurationNum,
        wlan_common::{assert_variant, ie, test_utils::fake_frames::*},
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
            let config =
                ClientConfig { signal_report_beacon_timeout: 99999, ensure_on_channel_time: 0 };
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

    fn make_client_station() -> Client {
        Client::new(BSSID, IFACE_MAC, false)
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

    #[test]
    fn test_ensure_on_channel_followed_by_scheduled_scan() {
        let mut m = MockObjects::new();
        let mut me = m.make_mlme();
        let mut client = make_client_station();
        client
            .bind(&mut me.scanner, &mut me.chan_sched, &mut me.channel_state)
            .send_open_auth_frame(&mut me.ctx)
            .expect("error delivering WLAN frame");
        assert_eq!(m.fake_device.wlan_queue.len(), 1);

        // Verify ensure_on_channel. That is, scheduling scan request would not cause channel to be
        // switched right away, while frame is still being sent.
        me.on_sme_scan(Some(&mut client), scan_req());
        assert_eq!(me.ctx.device.channel(), MAIN_CHANNEL);

        // Verify that triggering scheduled timeout by channel scheduler would switch channel
        assert_eq!(m.fake_scheduler.deadlines.len(), 1);
        me.handle_timed_event(Some(&mut client), m.fake_scheduler.next_id.into());
        assert_eq!(me.ctx.device.channel().primary, SCAN_CHANNEL_PRIMARY);
    }

    #[test]
    fn test_active_scan_scheduling() {
        let mut m = MockObjects::new();
        let mut me = m.make_mlme();
        let mut client = make_client_station();

        let scan_req = fidl_mlme::ScanRequest {
            scan_type: fidl_mlme::ScanTypes::Active,
            probe_delay: 5,
            ..scan_req()
        };
        let scan_txn_id = scan_req.txn_id;
        me.on_sme_scan(Some(&mut client), scan_req);
        assert_eq!(me.ctx.device.channel().primary, SCAN_CHANNEL_PRIMARY);

        // There should be two scheduled events, one by channel scheduler for scanned channel,
        // another by scanner for delayed sending of probe request
        assert_eq!(m.fake_scheduler.deadlines.len(), 2);
        let (id, _deadline) = m.fake_scheduler.next_event().expect("expect scheduled event [1]");
        me.handle_timed_event(Some(&mut client), id);

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
        me.handle_timed_event(Some(&mut client), id);
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
        let mut client = make_client_station();

        me.on_sme_scan(Some(&mut client), scan_req());
        assert_eq!(me.ctx.device.channel().primary, SCAN_CHANNEL_PRIMARY);
        // Verify no power state frame is sent
        assert_eq!(m.fake_device.wlan_queue.len(), 0);

        // There should be one scheduled event for end of channel period
        assert_eq!(m.fake_scheduler.deadlines.len(), 1);
        let (id, _deadline) = m.fake_scheduler.next_event().expect("expect scheduled event");
        me.handle_timed_event(Some(&mut client), id);
        assert_eq!(me.ctx.device.channel(), MAIN_CHANNEL);

        // Verify no power state frame is sent
        assert_eq!(m.fake_device.wlan_queue.len(), 0);
    }

    #[test]
    fn test_send_power_state_frame_when_switching_channel_while_connected() {
        let mut m = MockObjects::new();
        let mut me = m.make_mlme();
        let mut client = make_client_station();

        // Pretend that client is associated by starting LostBssCounter
        client.start_lost_bss_counter(&mut me.ctx, TimeUnit::DEFAULT_BEACON_INTERVAL);

        // Send scan request to trigger channel switch
        me.on_sme_scan(Some(&mut client), scan_req());
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
        me.handle_timed_event(Some(&mut client), id);
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

    #[test]
    fn test_auto_deauth_uninterrupted_interval() {
        let mut m = MockObjects::new();
        let mut me = m.make_mlme();
        let mut client = make_client_station();

        client.start_lost_bss_counter(&mut me.ctx, TimeUnit::DEFAULT_BEACON_INTERVAL);

        // Verify timer is scheduled
        let (id, deadline) = assert_variant!(m.fake_scheduler.next_event(), Some(ev) => ev);

        // Verify that triggering event at deadline causes deauth
        m.fake_scheduler.set_time(deadline);
        assert!(me.handle_timed_event(Some(&mut client), id));
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
        let eapol_ind = m
            .fake_device
            .next_mlme_msg::<fidl_mlme::DeauthenticateIndication>()
            .expect("error reading DEAUTHENTICATE.indication");
        assert_eq!(
            eapol_ind,
            fidl_mlme::DeauthenticateIndication {
                peer_sta_address: BSSID.0,
                reason_code: fidl_mlme::ReasonCode::LeavingNetworkDeauth,
            }
        );
    }

    #[test]
    fn test_auto_deauth_received_beacon() {
        let mut m = MockObjects::new();
        let mut me = m.make_mlme();
        let mut client = make_client_station();

        client.start_lost_bss_counter(&mut me.ctx, TimeUnit::DEFAULT_BEACON_INTERVAL);

        // Verify timer is scheduled
        let (id, deadline1) = assert_variant!(m.fake_scheduler.next_event(), Some(ev) => ev);

        // Receive beacon midway, so triggering event at original deadline would not cause deauth
        m.fake_scheduler.increment_time(1.second());
        me.on_mac_frame(Some(&mut client), BEACON_FRAME, None);
        m.fake_scheduler.set_time(deadline1);
        assert!(!me.handle_timed_event(Some(&mut client), id));
        assert_eq!(m.fake_device.wlan_queue.len(), 0);

        // Verify more timer is scheduled
        let (id, deadline2) = assert_variant!(m.fake_scheduler.next_event(), Some(ev) => ev);
        assert!(deadline2 - deadline1 >= 1.second());

        // Verify that triggering event at new deadline causes deauth
        m.fake_scheduler.set_time(deadline2);
        assert!(me.handle_timed_event(Some(&mut client), id));
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
        let eapol_ind = m
            .fake_device
            .next_mlme_msg::<fidl_mlme::DeauthenticateIndication>()
            .expect("error reading DEAUTHENTICATE.indication");
        assert_eq!(
            eapol_ind,
            fidl_mlme::DeauthenticateIndication {
                peer_sta_address: BSSID.0,
                reason_code: fidl_mlme::ReasonCode::LeavingNetworkDeauth,
            }
        );
    }

    #[test]
    fn client_send_open_auth_frame() {
        let mut m = MockObjects::new();
        let mut me = m.make_mlme();
        let mut client = make_client_station();
        client
            .bind(&mut me.scanner, &mut me.chan_sched, &mut me.channel_state)
            .send_open_auth_frame(&mut me.ctx)
            .expect("error delivering WLAN frame");
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
        me.on_sme_scan(Some(&mut client), scan_req());
        assert_eq!(me.ctx.device.channel(), MAIN_CHANNEL);
    }

    #[test]
    fn client_send_assoc_req_frame() {
        let mut m = MockObjects::new();
        let mut me = m.make_mlme();
        let mut client = make_client_station();
        client
            .bind(&mut me.scanner, &mut me.chan_sched, &mut me.channel_state)
            .send_assoc_req_frame(
                &mut me.ctx,
                0x1234,                               // capability info
                &[11, 22, 33, 44],                    // SSID
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
        me.on_sme_scan(Some(&mut client), scan_req());
        assert_eq!(me.ctx.device.channel(), MAIN_CHANNEL);
    }

    #[test]
    fn client_send_keep_alive_resp_frame() {
        let mut m = MockObjects::new();
        let mut me = m.make_mlme();
        let mut client = make_client_station();
        client
            .bind(&mut me.scanner, &mut me.chan_sched, &mut me.channel_state)
            .send_keep_alive_resp_frame(&mut me.ctx)
            .expect("error delivering WLAN frame");
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
        me.on_sme_scan(Some(&mut client), scan_req());
        assert_eq!(me.ctx.device.channel().primary, SCAN_CHANNEL_PRIMARY);
    }

    #[test]
    fn client_send_data_frame() {
        let payload = vec![5; 8];
        let mut m = MockObjects::new();
        let mut me = m.make_mlme();
        let mut client = make_client_station();
        client
            .bind(&mut me.scanner, &mut me.chan_sched, &mut me.channel_state)
            .send_data_frame(&mut me.ctx, [2; 6], [3; 6], false, false, 0x1234, &payload[..])
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
        me.on_sme_scan(Some(&mut client), scan_req());
        assert_eq!(me.ctx.device.channel().primary, SCAN_CHANNEL_PRIMARY);
    }

    #[test]
    fn client_send_deauthentication_notification() {
        let mut m = MockObjects::new();
        let mut me = m.make_mlme();
        let mut client = make_client_station();
        client
            .bind(&mut me.scanner, &mut me.chan_sched, &mut me.channel_state)
            .send_deauth_frame(&mut me.ctx, mac::ReasonCode::AP_INITIATED)
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
        me.on_sme_scan(Some(&mut client), scan_req());
        assert_eq!(me.ctx.device.channel(), MAIN_CHANNEL);
    }

    #[test]
    fn respond_to_keep_alive_request() {
        #[rustfmt::skip]
        let data_frame = vec![
            // Data header:
            0b0100_10_00, 0b000000_1_0, // FC
            0, 0, // Duration
            6, 6, 6, 6, 6, 6, // addr1
            7, 7, 7, 7, 7, 7, // addr2
            7, 7, 7, 7, 7, 7, // addr3
            0x10, 0, // Sequence Control
        ];
        let mut m = MockObjects::new();
        let mut me = m.make_mlme();
        let mut client = make_client_station();
        send_data_frame(
            &mut client.bind(&mut me.scanner, &mut me.chan_sched, &mut me.channel_state),
            &mut me.ctx,
            data_frame,
            true,
        );
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
        me.on_sme_scan(Some(&mut client), scan_req());
        assert_eq!(me.ctx.device.channel().primary, SCAN_CHANNEL_PRIMARY);
    }

    #[test]
    fn data_frame_to_ethernet_single_llc() {
        let data_frame = make_data_frame_single_llc(None, None);
        let mut m = MockObjects::new();
        let mut me = m.make_mlme();
        let mut client = make_client_station();
        let mut client = client.bind(&mut me.scanner, &mut me.chan_sched, &mut me.channel_state);
        send_data_frame(&mut client, &mut me.ctx, data_frame, true);
        #[rustfmt::skip]
        assert_eq!(m.fake_device.eth_queue[0], [
            3, 3, 3, 3, 3, 3, // dst_addr
            4, 4, 4, 4, 4, 4, // src_addr
            9, 10, // ether_type
            11, 11, 11, // payload
        ]);
    }

    #[test]
    fn data_frame_to_ethernet_amsdu() {
        let data_frame = make_data_frame_amsdu();
        let mut m = MockObjects::new();
        let mut me = m.make_mlme();
        let mut client = make_client_station();
        let mut client = client.bind(&mut me.scanner, &mut me.chan_sched, &mut me.channel_state);
        send_data_frame(&mut client, &mut me.ctx, data_frame, true);
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
        let data_frame = make_data_frame_amsdu_padding_too_short();
        let mut m = MockObjects::new();
        let mut me = m.make_mlme();
        let mut client = make_client_station();
        let mut client = client.bind(&mut me.scanner, &mut me.chan_sched, &mut me.channel_state);
        send_data_frame(&mut client, &mut me.ctx, data_frame, true);
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
        let data_frame = make_data_frame_single_llc(None, None);
        let mut m = MockObjects::new();
        let mut me = m.make_mlme();
        let mut client = make_client_station();
        let mut client = client.bind(&mut me.scanner, &mut me.chan_sched, &mut me.channel_state);
        send_data_frame(&mut client, &mut me.ctx, data_frame, false);

        // Verify frame was not sent to netstack.
        assert_eq!(m.fake_device.eth_queue.len(), 0);
    }

    #[test]
    fn eapol_frame_controlled_port_closed() {
        let (src_addr, dst_addr, eapol_frame) = make_eapol_frame();
        let mut m = MockObjects::new();
        let mut me = m.make_mlme();
        let mut client = make_client_station();
        let mut client = client.bind(&mut me.scanner, &mut me.chan_sched, &mut me.channel_state);
        send_data_frame(&mut client, &mut me.ctx, eapol_frame, false);

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
        let (src_addr, dst_addr, eapol_frame) = make_eapol_frame();
        let mut m = MockObjects::new();
        let mut me = m.make_mlme();
        let mut client = make_client_station();
        let mut client = client.bind(&mut me.scanner, &mut me.chan_sched, &mut me.channel_state);
        send_data_frame(&mut client, &mut me.ctx, eapol_frame, true);

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
        let mut client = make_client_station();
        let mut client = client.bind(&mut me.scanner, &mut me.chan_sched, &mut me.channel_state);
        client
            .send_eapol_indication(&mut me.ctx, [1; 6], [2; 6], &[5; 256])
            .expect_err("sending too large EAPOL frame should fail");
        m.fake_device
            .next_mlme_msg::<fidl_mlme::EapolIndication>()
            .expect_err("expected empty channel");
    }

    #[test]
    fn send_eapol_ind_success() {
        let mut m = MockObjects::new();
        let mut me = m.make_mlme();
        let mut client = make_client_station();
        let mut client = client.bind(&mut me.scanner, &mut me.chan_sched, &mut me.channel_state);
        client
            .send_eapol_indication(&mut me.ctx, [1; 6], [2; 6], &[5; 200])
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
        let mut client = make_client_station();
        let mut client = client.bind(&mut me.scanner, &mut me.chan_sched, &mut me.channel_state);
        client.send_eapol_frame(&mut me.ctx, IFACE_MAC, BSSID.0, false, &[5; 8]);

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
        let mut client = make_client_station();
        let mut client = client.bind(&mut me.scanner, &mut me.chan_sched, &mut me.channel_state);
        client.send_eapol_frame(&mut me.ctx, [1; 6], [2; 6], false, &[5; 200]);

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
        let mut client = make_client_station();
        client
            .bind(&mut me.scanner, &mut me.chan_sched, &mut me.channel_state)
            .send_ps_poll_frame(&mut me.ctx, 0xABCD)
            .expect("failed sending PS POLL frame");

        // Verify ensure_on_channel. That is, scheduling scan request would not cause channel to be
        // switched right away, while frame is still being sent.
        me.on_sme_scan(Some(&mut client), scan_req());
        assert_eq!(me.ctx.device.channel(), MAIN_CHANNEL);
    }

    #[test]
    fn send_power_state_doze_frame_success() {
        let mut m = MockObjects::new();
        let mut me = m.make_mlme();
        let mut client = make_client_station();
        send_power_state_frame(&mut client, &mut me.ctx, PowerState::DOZE)
            .expect("failed sending doze frame");
        send_power_state_frame(&mut client, &mut me.ctx, PowerState::AWAKE)
            .expect("failed sending awake frame");

        // Verify no ensure_on_channel. That is, scheduling scan request would cause channel to be
        // switched right away.
        me.on_sme_scan(Some(&mut client), scan_req());
        assert_eq!(me.ctx.device.channel().primary, SCAN_CHANNEL_PRIMARY);
    }

    #[test]
    fn send_addba_req_frame() {
        let mut m = MockObjects::new();
        let mut me = m.make_mlme();
        let mut client = make_client_station();
        client
            .bind(&mut me.scanner, &mut me.chan_sched, &mut me.channel_state)
            .send_addba_req_frame(&mut me.ctx)
            .expect("failed sending addba frame");
        assert_eq!(
            &m.fake_device.wlan_queue[0].0[..],
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
        let mut m = MockObjects::new();
        let mut me = m.make_mlme();
        let mut client = make_client_station();
        client
            .bind(&mut me.scanner, &mut me.chan_sched, &mut me.channel_state)
            .send_addba_resp_frame(&mut me.ctx, 1)
            .expect("failed sending addba frame");
        assert_eq!(
            &m.fake_device.wlan_queue[0].0[..],
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
        let mut client = make_client_station();

        let mut ies = vec![];
        let rates = vec![1, 2, 3, 4, 5, 6, 7, 8, 9, 10];
        let rates_writer = ie::RatesWriter::try_new(&rates[..]).expect("Valid rates");
        // It should work even if ext_supp_rates shows up before supp_rates
        rates_writer.write_ext_supported_rates(&mut ies);
        rates_writer.write_supported_rates(&mut ies);
        ie::write_ht_capabilities(&mut ies, &ie::fake_ht_capabilities()).expect("Valid HT Cap");
        ie::write_vht_capabilities(&mut ies, &ie::fake_vht_capabilities()).expect("Valid VHT Cap");

        client
            .bind(&mut me.scanner, &mut me.chan_sched, &mut me.channel_state)
            .send_associate_conf_success(&mut me.ctx, 42, mac::CapabilityInfo(0x1234), &ies[..]);
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
        let mut client = make_client_station();
        client
            .bind(&mut me.scanner, &mut me.chan_sched, &mut me.channel_state)
            .send_associate_conf_failure(
                &mut me.ctx,
                fidl_mlme::AssociateResultCodes::RefusedExternalReason,
            );
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
                ht_cap: None,
                vht_cap: None,
            }
        );
    }

    fn send_data_frame(
        client: &mut BoundClient<'_>,
        ctx: &mut Context,
        data_frame: Vec<u8>,
        open_controlled_port: bool,
    ) {
        let parsed_frame = mac::MacFrame::parse(&data_frame[..], false).expect("invalid frame");
        let (fixed, addr4, qos, body) = match parsed_frame {
            mac::MacFrame::Data { fixed_fields, addr4, qos_ctrl, body, .. } => {
                (fixed_fields, addr4.map(|x| *x), qos_ctrl.map(|x| x.get()), body)
            }
            _ => panic!("error parsing data frame"),
        };
        client.handle_data_frame(ctx, &fixed, addr4, qos, body, open_controlled_port);
    }
}
