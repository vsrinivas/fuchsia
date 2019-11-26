// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        ap::{frame_writer::*, TimedEvent},
        buffer::{BufferProvider, OutBuf},
        device::{Device, TxFlags},
        error::Error,
        timer::{EventId, Timer},
        write_eth_frame,
    },
    fidl_fuchsia_wlan_mlme as fidl_mlme, fuchsia_zircon as zx,
    wlan_common::{
        buffer_writer::BufferWriter,
        frame_len,
        ie::{self, IE_PREFIX_LEN, SUPPORTED_RATES_MAX_LEN},
        mac::{
            self, Aid, AuthAlgorithmNumber, Bssid, MacAddr, OptionalField, Presence, StatusCode,
        },
        sequence::SequenceManager,
    },
};

pub struct Context {
    pub device: Device,
    pub buf_provider: BufferProvider,
    pub timer: Timer<TimedEvent>,
    pub seq_mgr: SequenceManager,
    pub bssid: Bssid,
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
        rates: Vec<u8>,
        rsne: Option<Vec<u8>>,
    ) -> Result<(), Error> {
        self.device.access_sme_sender(|sender| {
            sender.send_associate_ind(&mut fidl_mlme::AssociateIndication {
                peer_sta_address,
                listen_interval,
                ssid,
                rates,
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
    pub fn send_assoc_resp_frame(
        &mut self,
        addr: MacAddr,
        capabilities: mac::CapabilityInfo,
        aid: Aid,
        rates: &[u8],
        max_idle_period: Option<u16>,
    ) -> Result<(), Error> {
        let frame_len = frame_len!(mac::MgmtHdr, mac::AssocRespHdr);
        let rates_len = IE_PREFIX_LEN
            + rates.len()
            // If there are too many rates, they will be split into two IEs.
            // In this case, the total length would be the sum of:
            // 1) 1st IE: IE_PREFIX_LEN + SUPPORTED_RATES_MAX_LEN
            // 2) 2nd IE: IE_PREFIX_LEN + rates().len - SUPPORTED_RATES_MAX_LEN
            // The total length is IE_PREFIX_LEN + rates.len() + IE_PREFIX_LEN.
            + if rates.len() > SUPPORTED_RATES_MAX_LEN { IE_PREFIX_LEN } else { 0 };
        let max_idle_period_len = if max_idle_period.is_some() {
            IE_PREFIX_LEN + std::mem::size_of::<ie::BssMaxIdlePeriod>()
        } else {
            0
        };
        let frame_len = frame_len + rates_len + max_idle_period_len;

        let mut buf = self.buf_provider.get_buffer(frame_len)?;
        let mut w = BufferWriter::new(&mut buf[..]);
        write_assoc_resp_frame(
            &mut w,
            addr,
            self.bssid.clone(),
            &mut self.seq_mgr,
            capabilities,
            aid,
            rates,
            max_idle_period,
        )?;
        let bytes_written = w.bytes_written();
        let out_buf = OutBuf::from(buf, bytes_written);
        self.device
            .send_wlan_frame(out_buf, TxFlags::NONE)
            .map_err(|s| Error::Status(format!("error sending assoc resp frame"), s))
    }

    /// Sends a WLAN association response frame (IEEE Std 802.11-2016, 9.3.3.7) to the PHY, but only
    /// with the status code.
    pub fn send_assoc_resp_frame_error(
        &mut self,
        addr: MacAddr,
        capabilities: mac::CapabilityInfo,
        status_code: StatusCode,
    ) -> Result<(), Error> {
        const FRAME_LEN: usize = frame_len!(mac::MgmtHdr, mac::AssocRespHdr);
        let mut buf = self.buf_provider.get_buffer(FRAME_LEN)?;
        let mut w = BufferWriter::new(&mut buf[..]);
        write_assoc_resp_frame_error(
            &mut w,
            addr,
            self.bssid.clone(),
            &mut self.seq_mgr,
            capabilities,
            status_code,
        )?;
        let bytes_written = w.bytes_written();
        let out_buf = OutBuf::from(buf, bytes_written);
        self.device
            .send_wlan_frame(out_buf, TxFlags::NONE)
            .map_err(|s| Error::Status(format!("error sending assoc resp error frame"), s))
    }

    /// Sends a WLAN deauthentication frame (IEEE Std 802.11-2016, 9.3.3.1) to the PHY.
    pub fn send_deauth_frame(
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
    pub fn send_disassoc_frame(
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
    pub fn send_data_frame(
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
    pub fn send_eapol_frame(
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
    pub fn deliver_eth_frame(
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

#[cfg(test)]
mod test {
    use {
        super::*,
        crate::{
            ap::ClientEvent,
            buffer::FakeBufferProvider,
            device::FakeDevice,
            timer::{FakeScheduler, Scheduler},
        },
        wlan_common::assert_variant,
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
        ctx.send_mlme_assoc_ind(CLIENT_ADDR, 1, Some(b"coolnet".to_vec()), vec![1, 2, 3], None)
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
                rates: vec![1, 2, 3],
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
    fn schedule_after() {
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
    fn cancel_event() {
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
    fn send_auth_frame() {
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
    fn send_assoc_resp_frame() {
        let mut fake_device = FakeDevice::new();
        let mut fake_scheduler = FakeScheduler::new();
        let mut ctx = make_context(fake_device.as_device(), fake_scheduler.as_scheduler());
        ctx.send_assoc_resp_frame(
            CLIENT_ADDR,
            mac::CapabilityInfo(0),
            1,
            &[1, 2, 3, 4, 5, 6, 7, 8, 9, 10][..],
            Some(99),
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
            0, 0, // status code
            1, 0, // AID
            // IEs
            1, 8, 1, 2, 3, 4, 5, 6, 7, 8, // Rates
            50, 2, 9, 10, // Extended rates
            90, 3, 99, 0, 0, // BSS max idle period
        ][..]);
    }

    #[test]
    fn send_assoc_resp_frame_error() {
        let mut fake_device = FakeDevice::new();
        let mut fake_scheduler = FakeScheduler::new();
        let mut ctx = make_context(fake_device.as_device(), fake_scheduler.as_scheduler());
        ctx.send_assoc_resp_frame_error(
            CLIENT_ADDR,
            mac::CapabilityInfo(0),
            StatusCode::REJECTED_EMERGENCY_SERVICES_NOT_SUPPORTED,
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
            0, 0, // AID
        ][..]);
    }

    #[test]
    fn send_assoc_resp_frame_no_bss_max_idle_period() {
        let mut fake_device = FakeDevice::new();
        let mut fake_scheduler = FakeScheduler::new();
        let mut ctx = make_context(fake_device.as_device(), fake_scheduler.as_scheduler());
        ctx.send_assoc_resp_frame(
            CLIENT_ADDR,
            mac::CapabilityInfo(0),
            1,
            &[1, 2, 3, 4, 5, 6, 7, 8, 9, 10][..],
            None,
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
            0, 0, // status code
            1, 0, // AID
            // IEs
            1, 8, 1, 2, 3, 4, 5, 6, 7, 8, // Rates
            50, 2, 9, 10, // Extended rates
        ][..]);
    }

    #[test]
    fn send_disassoc_frame() {
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
    fn send_data_frame() {
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
    fn send_eapol_frame() {
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
    fn deliver_eth_frame() {
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
}
