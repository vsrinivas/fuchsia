// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[allow(unused)]
mod channel_scheduler;
#[allow(unused)]
mod convert_beacon;
mod cpp_proxy;
mod frame_writer;
#[allow(unused)]
mod scanner;
mod state;

use {
    crate::{
        buffer::{BufferProvider, OutBuf},
        device::{Device, TxFlags},
        error::Error,
        logger,
        timer::*,
        write_eth_frame,
    },
    banjo_ddk_protocol_wlan_info::WlanChannel,
    failure::format_err,
    fidl_fuchsia_wlan_mlme as fidl_mlme,
    frame_writer::*,
    fuchsia_zircon as zx,
    log::error,
    state::States,
    wlan_common::{
        buffer_writer::BufferWriter,
        frame_len,
        ie::{
            parse_ht_capabilities, parse_vht_capabilities, rsn::rsne, IE_PREFIX_LEN,
            SUPPORTED_RATES_MAX_LEN,
        },
        mac::{self, Aid, Bssid, MacAddr, OptionalField, PowerState, Presence},
        sequence::SequenceManager,
    },
    zerocopy::ByteSlice,
};

pub use {cpp_proxy::CppChannelScheduler, scanner::ScanError};

/// Maximum size of EAPOL frames forwarded to SME.
/// TODO(34845): Evaluate whether EAPOL size restriction is needed.
const MAX_EAPOL_FRAME_LEN: usize = 255;

#[derive(Debug, PartialEq)]
pub enum TimedEvent {
    /// Authentication timed out. AP did not complete authentication in time.
    Authenticating,
    /// Association timed out. AP did not complete association in time.
    Associating,
    ChannelScheduler,
    ScannerProbeDelay(WlanChannel),
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
    cpp_chan_sched: CppChannelScheduler,
}

pub struct ClientMlme {
    ctx: Context,
}

impl ClientMlme {
    pub fn new(
        config: ClientConfig,
        device: Device,
        buf_provider: BufferProvider,
        scheduler: Scheduler,
        cpp_chan_sched: CppChannelScheduler,
    ) -> Self {
        // TODO(41417): Remove this once devmgr installs a Rust logger.
        logger::install();

        let timer = Timer::<TimedEvent>::new(scheduler);
        Self {
            ctx: Context {
                config,
                device,
                buf_provider,
                timer,
                seq_mgr: SequenceManager::new(),
                cpp_chan_sched,
            },
        }
    }

    pub fn ctx(&mut self) -> &mut Context {
        &mut self.ctx
    }

    pub fn seq_mgr(&mut self) -> &mut SequenceManager {
        &mut self.ctx.seq_mgr
    }

    #[allow(deprecated)] // Allow until main message loop is in Rust.
    pub fn handle_mlme_msg(&mut self, _msg: fidl_mlme::MlmeRequestMessage) -> Result<(), Error> {
        // 0x90 for now.
        Err(Error::Status(format!("MLME messages not supported"), zx::Status::NOT_SUPPORTED))
    }

    /// Called when a previously scheduled `TimedEvent` fired.
    pub fn handle_timed_event(&mut self, sta: Option<&mut Client>, event_id: EventId) {
        // TODO(29063): handle scanner and channel scheduler timed events
        if let Some(sta) = sta {
            sta.handle_timed_event(self.ctx(), event_id);
        }
    }
}

/// A STA running in Client mode.
/// The Client STA is in its early development process and does not yet manage its internal state
/// machine or track negotiated capabilities.
pub struct Client {
    state: Option<States>,
    pub bssid: Bssid,
    pub iface_mac: MacAddr,
    pub is_rsn: bool,
}

impl Client {
    pub fn new(bssid: Bssid, iface_mac: MacAddr, is_rsn: bool) -> Self {
        Self { bssid, iface_mac, state: Some(States::new_initial()), is_rsn }
    }

    pub fn authenticate(&mut self, ctx: &mut Context, timeout_bcn_count: u8) {
        // Safe: |state| is never None and always replaced with Some(..).
        self.state = Some(self.state.take().unwrap().authenticate(self, ctx, timeout_bcn_count));
    }

    // TODO(hahnr): Take MLME-ASSOCIATE.request as parameter.
    pub fn associate(&mut self, ctx: &mut Context) {
        // Safe: |state| is never None and always replaced with Some(..).
        self.state = Some(self.state.take().unwrap().associate(self, ctx));
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
        write_open_auth_frame(&mut w, self.bssid, self.iface_mac, &mut ctx.seq_mgr)?;
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
            self.bssid,
            self.iface_mac,
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
        write_keep_alive_resp_frame(&mut w, self.bssid, self.iface_mac, &mut ctx.seq_mgr)?;
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
        write_deauth_frame(&mut w, self.bssid, self.iface_mac, reason_code, &mut ctx.seq_mgr)?;
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
            self.bssid,
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
        write_ps_poll_frame(&mut w, aid, self.bssid, self.iface_mac)?;
        let bytes_written = w.bytes_written();
        let out_buf = OutBuf::from(buf, bytes_written);
        self.send_mgmt_or_ctrl_frame(ctx, out_buf)
            .map_err(|s| Error::Status(format!("error sending PS-Poll frame"), s))
    }

    /// Called when a previously scheduled `TimedEvent` fired.
    pub fn handle_timed_event(&mut self, ctx: &mut Context, event_id: EventId) {
        // Safe: |state| is never None and always replaced with Some(..).
        self.state = Some(self.state.take().unwrap().on_timed_event(self, ctx, event_id));
    }

    /// Called when an arbitrary frame was received over the air.
    pub fn on_mac_frame<B: ByteSlice>(&mut self, ctx: &mut Context, bytes: B, body_aligned: bool) {
        // Safe: |state| is never None and always replaced with Some(..).
        self.state = Some(self.state.take().unwrap().on_mac_frame(self, ctx, bytes, body_aligned));
    }

    // TODO(39899): The Rust crate does not yet support channel scheduling. When channel scheduling
    //              is available, use this code to doze and awake as needed.
    /// Sends a power management data frame to the associated AP indicating that the client has
    /// entered the given power state. See `PowerState`.
    ///
    /// # Errors
    ///
    /// Returns an error if the data frame cannot be sent to the AP.
    pub fn send_power_state_frame(
        &mut self,
        ctx: &mut Context,
        state: PowerState,
    ) -> Result<(), Error> {
        let mut buffer = ctx.buf_provider.get_buffer(mac::FixedDataHdrFields::len(
            mac::Addr4::ABSENT,
            mac::QosControl::ABSENT,
            mac::HtControl::ABSENT,
        ))?;
        let mut writer = BufferWriter::new(&mut buffer[..]);
        write_power_state_frame(&mut writer, self.bssid, self.iface_mac, &mut ctx.seq_mgr, state)?;
        let n = writer.bytes_written();
        let buffer = OutBuf::from(buffer, n);
        ctx.device
            .send_wlan_frame(buffer, TxFlags::NONE)
            .map_err(|error| Error::Status(format!("error sending power management frame"), error))
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
                peer_sta_address: self.bssid.0,
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
    fn send_associate_conf(
        &mut self,
        ctx: &mut Context,
        aid: mac::Aid,
        result_code: fidl_mlme::AssociateResultCodes,
    ) {
        let result = ctx.device.access_sme_sender(|sender| {
            sender.send_associate_conf(&mut fidl_mlme::AssociateConfirm {
                association_id: aid,
                result_code,
            })
        });
        if let Err(e) = result {
            error!("error sending MLME-AUTHENTICATE.confirm: {}", e);
        }
    }

    /// Sends an MLME-DEAUTHENTICATE.indication message to the joined BSS.
    fn send_deauthenticate_ind(&mut self, ctx: &mut Context, reason_code: fidl_mlme::ReasonCode) {
        let result = ctx.device.access_sme_sender(|sender| {
            sender.send_deauthenticate_ind(&mut fidl_mlme::DeauthenticateIndication {
                peer_sta_address: self.bssid.0,
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
                peer_sta_address: self.bssid.0,
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
        let end = ctx.timer.now().into_nanos() + ctx.config.ensure_on_channel_time;
        ctx.cpp_chan_sched.ensure_on_channel(end);
        ctx.device.send_wlan_frame(out_buf, TxFlags::NONE)
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{buffer::FakeBufferProvider, device::FakeDevice},
        cpp_proxy::FakeCppChannelScheduler,
        wlan_common::test_utils::fake_frames::*,
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

    struct MockObjects {
        fake_device: FakeDevice,
        fake_scheduler: FakeScheduler,
        fake_chan_sched: FakeCppChannelScheduler,
    }

    impl MockObjects {
        fn new() -> Self {
            Self {
                fake_device: FakeDevice::new(),
                fake_scheduler: FakeScheduler::new(),
                fake_chan_sched: FakeCppChannelScheduler::new(),
            }
        }

        fn make_ctx(&mut self) -> Context {
            let device = self.fake_device.as_device();
            self.make_ctx_with_device(device)
        }

        fn make_ctx_with_device(&mut self, device: Device) -> Context {
            let timer = Timer::<TimedEvent>::new(self.fake_scheduler.as_scheduler());
            Context {
                config: ClientConfig {
                    signal_report_beacon_timeout: 99999,
                    ensure_on_channel_time: 0,
                },
                device,
                buf_provider: FakeBufferProvider::new(),
                timer,
                seq_mgr: SequenceManager::new(),
                cpp_chan_sched: self.fake_chan_sched.as_chan_sched(),
            }
        }
    }

    fn make_client_station() -> Client {
        Client::new(BSSID, IFACE_MAC, false)
    }

    #[test]
    fn client_send_open_auth_frame() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut client = make_client_station();
        client.send_open_auth_frame(&mut ctx).expect("error delivering WLAN frame");
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
        assert!(m.fake_chan_sched.ensure_on_channel.is_some())
    }

    #[test]
    fn client_send_assoc_req_frame() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut client = make_client_station();
        client
            .send_assoc_req_frame(
                &mut ctx,
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
        assert!(m.fake_chan_sched.ensure_on_channel.is_some());
    }

    #[test]
    fn client_send_keep_alive_resp_frame() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut client = make_client_station();
        client.send_keep_alive_resp_frame(&mut ctx).expect("error delivering WLAN frame");
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
        assert!(m.fake_chan_sched.ensure_on_channel.is_none());
    }

    #[test]
    fn client_send_data_frame() {
        let payload = vec![5; 8];
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut client = make_client_station();
        client
            .send_data_frame(&mut ctx, [2; 6], [3; 6], false, false, 0x1234, &payload[..])
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
        assert!(m.fake_chan_sched.ensure_on_channel.is_none());
    }

    #[test]
    fn client_send_deauthentication_notification() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut client = make_client_station();
        client
            .send_deauth_frame(&mut ctx, mac::ReasonCode::AP_INITIATED)
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
        assert!(m.fake_chan_sched.ensure_on_channel.is_some());
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
        let mut ctx = m.make_ctx();
        let mut client = make_client_station();
        send_data_frame(&mut client, &mut ctx, data_frame, true);
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
        assert!(m.fake_chan_sched.ensure_on_channel.is_none());
    }

    #[test]
    fn data_frame_to_ethernet_single_llc() {
        let data_frame = make_data_frame_single_llc(None, None);
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut client = make_client_station();
        send_data_frame(&mut client, &mut ctx, data_frame, true);
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
        let mut ctx = m.make_ctx();
        let mut client = make_client_station();
        send_data_frame(&mut client, &mut ctx, data_frame, true);
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
        let mut ctx = m.make_ctx();
        let mut client = make_client_station();
        send_data_frame(&mut client, &mut ctx, data_frame, true);
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
        let mut ctx = m.make_ctx();
        let mut client = make_client_station();
        send_data_frame(&mut client, &mut ctx, data_frame, false);

        // Verify frame was not sent to netstack.
        assert_eq!(m.fake_device.eth_queue.len(), 0);
    }

    #[test]
    fn eapol_frame_controlled_port_closed() {
        let (src_addr, dst_addr, eapol_frame) = make_eapol_frame();
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut client = make_client_station();
        send_data_frame(&mut client, &mut ctx, eapol_frame, false);

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
        let mut ctx = m.make_ctx();
        let mut client = make_client_station();
        send_data_frame(&mut client, &mut ctx, eapol_frame, true);

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
        let mut ctx = m.make_ctx();
        let mut client = make_client_station();
        client
            .send_eapol_indication(&mut ctx, [1; 6], [2; 6], &[5; 256])
            .expect_err("sending too large EAPOL frame should fail");
        m.fake_device
            .next_mlme_msg::<fidl_mlme::EapolIndication>()
            .expect_err("expected empty channel");
    }

    #[test]
    fn send_eapol_ind_success() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut client = make_client_station();
        client
            .send_eapol_indication(&mut ctx, [1; 6], [2; 6], &[5; 200])
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
        let mut ctx = m.make_ctx();
        let mut client = make_client_station();
        client.send_eapol_frame(&mut ctx, IFACE_MAC, BSSID.0, false, &[5; 8]);

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
        let mut ctx = m.make_ctx_with_device(device);
        let mut client = make_client_station();
        client.send_eapol_frame(&mut ctx, [1; 6], [2; 6], false, &[5; 200]);

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
        let mut ctx = m.make_ctx();
        let mut client = make_client_station();
        client.send_ps_poll_frame(&mut ctx, 0xABCD).expect("failed sending PS POLL frame");
        assert!(m.fake_chan_sched.ensure_on_channel.is_some());
    }

    #[test]
    fn send_power_state_frame() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut client = make_client_station();
        client
            .send_power_state_frame(&mut ctx, PowerState::DOZE)
            .expect("failed sending doze frame");
        assert!(m.fake_chan_sched.ensure_on_channel.is_none());
        client
            .send_power_state_frame(&mut ctx, PowerState::AWAKE)
            .expect("failed sending awake frame");
        assert!(m.fake_chan_sched.ensure_on_channel.is_none());
    }

    fn send_data_frame(
        client: &mut Client,
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
