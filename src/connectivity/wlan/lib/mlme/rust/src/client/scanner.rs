// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        buffer::{BufferProvider, OutBuf},
        client::{
            channel_scheduler::{self, ChannelListener, ChannelScheduler},
            convert_beacon::{construct_bss_description, RxInfo},
            frame_writer::write_probe_req_frame,
            TimedEvent,
        },
        device::{Device, TxFlags},
        error::Error,
        timer::{EventId, Timer},
    },
    banjo_ddk_hw_wlan_wlaninfo::{WlanInfoDriverFeature, WLAN_INFO_CHANNEL_LIST_MAX_CHANNELS},
    banjo_ddk_protocol_wlan_info::{
        WlanChannel, WlanChannelBandwidth, WlanSsid, WLAN_MAX_SSID_LEN,
    },
    banjo_ddk_protocol_wlan_mac::{WlanHwScan, WlanHwScanConfig, WlanHwScanType},
    failure::Fail,
    fidl_fuchsia_wlan_mlme as fidl_mlme, fuchsia_zircon as zx,
    log::{error, warn},
    std::{
        collections::{hash_map, HashMap},
        hash::{Hash, Hasher},
    },
    wlan_common::{
        buffer_writer::BufferWriter,
        frame_len,
        ie::{
            parse_ht_capabilities, parse_vht_capabilities, IE_PREFIX_LEN, SUPPORTED_RATES_MAX_LEN,
        },
        mac::{self, Bssid, CapabilityInfo, MacAddr},
        sequence::SequenceManager,
        time::TimeUnit,
    },
};

type BeaconHash = u64;

#[derive(Fail, Debug, PartialEq, Eq)]
pub enum ScanError {
    #[fail(display = "scanner is busy")]
    Busy,
    #[fail(display = "invalid arg: empty channel list")]
    EmptyChannelList,
    #[fail(display = "invalid arg: channel list too large")]
    ChannelListTooLarge,
    #[fail(display = "invalid arg: max_channel_time < min_channel_time")]
    MaxChannelTimeLtMin,
    #[fail(display = "invalid arg: SSID too long")]
    SsidTooLong,
    #[fail(display = "fail starting hw scan: {}", _0)]
    StartHwScanFails(#[cause] zx::Status),
    #[fail(display = "hw scan aborted")]
    HwScanAborted,
}

impl From<ScanError> for zx::Status {
    fn from(e: ScanError) -> Self {
        match e {
            ScanError::Busy => zx::Status::UNAVAILABLE,
            ScanError::EmptyChannelList
            | ScanError::ChannelListTooLarge
            | ScanError::MaxChannelTimeLtMin
            | ScanError::SsidTooLong => zx::Status::INVALID_ARGS,
            ScanError::StartHwScanFails(status) => status,
            ScanError::HwScanAborted => zx::Status::INTERNAL,
        }
    }
}

pub struct Scanner {
    ongoing_scan_req: Option<OngoingScanRequest>,
    /// MAC address of current client interface
    iface_mac: MacAddr,

    // TODO(29063): Having Scanner own the below dependencies for now, but these will need to be
    //              changed to some reference type later on.
    buf_provider: BufferProvider,
    device: Device,
    seq_mgr: SequenceManager,
    timer: Timer<TimedEvent>,
}

struct OngoingScanRequest {
    /// Scan request that's currently being serviced.
    req: fidl_mlme::ScanRequest,
    /// ID of channel request made to channel scheduler as result of scan request. This is not
    /// populated in hardware scan.
    chan_sched_req_id: Option<channel_scheduler::RequestId>,
    /// BSS seen from beacon or probe response. This is populated while scan request is in
    /// progress and is cleared out otherwise.
    seen_bss: HashMap<Bssid, (BeaconHash, fidl_mlme::BssDescription)>,
    /// ID of timeout event scheduled for active scan at beginning of each channel switch. At
    /// end of timeout, a probe request is sent.
    probe_delay_timeout_id: Option<EventId>,
}

impl Scanner {
    pub fn new(
        device: Device,
        buf_provider: BufferProvider,
        timer: Timer<TimedEvent>,
        iface_mac: MacAddr,
    ) -> Self {
        Self {
            ongoing_scan_req: None,
            iface_mac,

            buf_provider,
            device,
            seq_mgr: SequenceManager::new(),
            timer,
        }
    }

    /// Handle scan request. Queue requested scan channels in channel scheduler.
    ///
    /// If a scan request is in progress, or the new request has invalid argument (empty channel
    /// list or larger min channel time than max), then the request is rejected.
    pub fn handle_mlme_scan_req<F, CL: ChannelListener>(
        &mut self,
        req: fidl_mlme::ScanRequest,
        build_channel_listener: F,
        channel_scheduler: &mut ChannelScheduler<CL>,
    ) -> Result<(), ScanError>
    where
        F: FnOnce(&mut Self) -> CL,
    {
        if self.ongoing_scan_req.is_some() {
            send_scan_end(req.txn_id, fidl_mlme::ScanResultCodes::NotSupported, &mut self.device);
            return Err(ScanError::Busy);
        }

        let channel_list = req.channel_list.as_ref().map(|list| list.as_slice()).unwrap_or(&[][..]);
        if channel_list.is_empty() {
            send_scan_end(req.txn_id, fidl_mlme::ScanResultCodes::InvalidArgs, &mut self.device);
            return Err(ScanError::EmptyChannelList);
        }
        if channel_list.len() > WLAN_INFO_CHANNEL_LIST_MAX_CHANNELS {
            send_scan_end(req.txn_id, fidl_mlme::ScanResultCodes::InvalidArgs, &mut self.device);
            return Err(ScanError::ChannelListTooLarge);
        }
        if req.max_channel_time < req.min_channel_time {
            send_scan_end(req.txn_id, fidl_mlme::ScanResultCodes::InvalidArgs, &mut self.device);
            return Err(ScanError::MaxChannelTimeLtMin);
        }
        if req.ssid.len() > WLAN_MAX_SSID_LEN as usize {
            send_scan_end(req.txn_id, fidl_mlme::ScanResultCodes::InvalidArgs, &mut self.device);
            return Err(ScanError::SsidTooLong);
        }

        let mut chan_sched_req_id = None;
        let wlan_info = self.device.wlan_info().ifc_info;
        let hw_scan = (wlan_info.driver_features & WlanInfoDriverFeature::SCAN_OFFLOAD).0 > 0;
        if hw_scan {
            let scan_type = if req.scan_type == fidl_mlme::ScanTypes::Active {
                WlanHwScanType::ACTIVE
            } else {
                WlanHwScanType::PASSIVE
            };
            let mut channels = [0; WLAN_INFO_CHANNEL_LIST_MAX_CHANNELS];
            channels[..channel_list.len()].copy_from_slice(channel_list);
            let mut ssid = [0; WLAN_MAX_SSID_LEN as usize];
            ssid[..req.ssid.len()].copy_from_slice(&req.ssid[..]);

            let config = WlanHwScanConfig {
                scan_type,
                num_channels: channel_list.len() as u8,
                channels,
                ssid: WlanSsid { len: req.ssid.len() as u8, ssid },
            };
            if let Err(status) = self.device.start_hw_scan(&config) {
                send_scan_end(
                    req.txn_id,
                    fidl_mlme::ScanResultCodes::InternalError,
                    &mut self.device,
                );
                return Err(ScanError::StartHwScanFails(status));
            }
        } else {
            let channels = channel_list
                .iter()
                .map(|c| WlanChannel {
                    primary: *c,
                    cbw: WlanChannelBandwidth::_20,
                    secondary80: 0,
                })
                .collect();
            let mut listener = build_channel_listener(self);
            let dwell_time = TimeUnit(req.max_channel_time as u16).into();
            let request_id = channel_scheduler.queue_channels(channels, dwell_time, &mut listener);
            chan_sched_req_id = Some(request_id);
        }
        self.ongoing_scan_req = Some(OngoingScanRequest {
            req,
            chan_sched_req_id,
            seen_bss: HashMap::new(),
            probe_delay_timeout_id: None,
        });
        Ok(())
    }

    /// Called when MLME receives a beacon or probe response so that scanner saves it in a BSS map.
    ///
    /// This method is a no-op if no scan request is in progress.
    pub fn handle_beacon_or_probe_response(
        &mut self,
        bssid: Bssid,
        timestamp: u64,
        beacon_interval: TimeUnit,
        capability_info: CapabilityInfo,
        ies: &[u8],
        rx_info: RxInfo,
    ) {
        let mut seen_bss = match &mut self.ongoing_scan_req {
            Some(req) => &mut req.seen_bss,
            None => return,
        };
        match seen_bss.entry(bssid) {
            hash_map::Entry::Occupied(mut entry) => {
                let (existing_hash, bss_description) = entry.get_mut();
                if timestamp < bss_description.timestamp {
                    return;
                }
                let hash = beacon_hash(beacon_interval, capability_info, ies);
                if hash != *existing_hash {
                    let new_bss_description = construct_bss_description(
                        bssid,
                        timestamp,
                        beacon_interval,
                        capability_info,
                        ies,
                        rx_info,
                    );
                    let mut new_bss_description = match new_bss_description {
                        Ok(bss) => bss,
                        Err(e) => {
                            warn!("Failed to parse beacon or probe response: {}", e);
                            return;
                        }
                    };
                    // In case where AP is hidden, its SSID is blank (all 0) in beacon but contains
                    // the actual SSID in probe response. So if we receive a blank SSID, always
                    // prefer the existing one since we may have set the actual SSID if we had
                    // received a probe response previously.
                    if new_bss_description.ssid.iter().all(|b| *b == 0)
                        && bss_description.ssid.iter().any(|b| *b != 0)
                    {
                        new_bss_description.ssid = bss_description.ssid.clone();
                    }
                    // TIM element is present in beacon frame but no probe response. In case we
                    // see no DTIM period in latest frame, use value from previous one.
                    if new_bss_description.dtim_period == 0 {
                        new_bss_description.dtim_period = bss_description.dtim_period;
                    }
                    *bss_description = new_bss_description;
                    *existing_hash = hash;
                }
            }
            hash_map::Entry::Vacant(entry) => {
                let hash = beacon_hash(beacon_interval, capability_info, ies);
                let bss_description = construct_bss_description(
                    bssid,
                    timestamp,
                    beacon_interval,
                    capability_info,
                    ies,
                    rx_info,
                );
                let bss_description = match bss_description {
                    Ok(bss) => bss,
                    Err(e) => {
                        warn!("Failed to parse beacon or probe response: {}", e);
                        return;
                    }
                };
                entry.insert((hash, bss_description));
            }
        }
    }

    /// Notify scanner about end of probe-delay timeout so that it sends out probe request.
    pub fn handle_probe_delay_timeout(&mut self, channel: WlanChannel) {
        let ssid = match &self.ongoing_scan_req {
            Some(OngoingScanRequest { req, .. }) => req.ssid.clone(),
            None => return,
        };
        if let Err(e) = self.send_probe_req(&ssid[..], channel) {
            error!("{}", e);
        }
    }

    pub fn handle_hw_scan_complete(&mut self, status: WlanHwScan) {
        let req = match self.ongoing_scan_req.take() {
            Some(req) => req,
            None => {
                warn!("Received HwScanComplete with status {:?} while no req in progress", status);
                return;
            }
        };

        if status == WlanHwScan::SUCCESS {
            send_results_and_end(req, &mut self.device);
        } else {
            send_scan_end(
                req.req.txn_id,
                fidl_mlme::ScanResultCodes::InternalError,
                &mut self.device,
            );
        }
    }

    /// Called after switching to a requested channel from a scan request. It's primarily to
    /// send out, or schedule to send out, a probe request in an active scan.
    pub fn begin_requested_channel_time(&mut self, channel: WlanChannel) {
        let (req, probe_delay_timeout_id) = match &mut self.ongoing_scan_req {
            Some(req) => (&req.req, &mut req.probe_delay_timeout_id),
            None => return,
        };
        if req.scan_type == fidl_mlme::ScanTypes::Active {
            if req.probe_delay == 0 {
                let ssid = req.ssid.clone();
                if let Err(e) = self.send_probe_req(&ssid[..], channel) {
                    error!("{}", e);
                }
            } else {
                let deadline = self.timer.now() + TimeUnit(req.probe_delay as u16).into();
                let timeout_id =
                    self.timer.schedule_event(deadline, TimedEvent::ScannerProbeDelay(channel));
                if let Some(old_id) = probe_delay_timeout_id.replace(timeout_id) {
                    self.timer.cancel_event(old_id);
                }
            }
        }
    }

    fn send_probe_req(&mut self, ssid: &[u8], _channel: WlanChannel) -> Result<(), Error> {
        // TODO(29063): get actual value from `device`
        let rates = [0xb0, 0x48, 0x60, 0x6c];
        let ht_cap = &[][..];
        let vht_cap = &[][..];

        let mgmt_hdr_len = frame_len!(mac::MgmtHdr);
        let ssid_len = IE_PREFIX_LEN + ssid.len();
        let rates_len = (IE_PREFIX_LEN + rates.len())
            + if rates.len() > SUPPORTED_RATES_MAX_LEN { IE_PREFIX_LEN } else { 0 };
        let ht_cap_len = if ht_cap.is_empty() { 0 } else { IE_PREFIX_LEN + ht_cap.len() };
        let vht_cap_len = if vht_cap.is_empty() { 0 } else { IE_PREFIX_LEN + vht_cap.len() };

        let ht_cap = if ht_cap.is_empty() { None } else { Some(*parse_ht_capabilities(ht_cap)?) };
        let vht_cap =
            if vht_cap.is_empty() { None } else { Some(*parse_vht_capabilities(vht_cap)?) };

        let frame_len = mgmt_hdr_len + ssid_len + rates_len + ht_cap_len + vht_cap_len;
        let mut buf = self.buf_provider.get_buffer(frame_len)?;
        let mut w = BufferWriter::new(&mut buf[..]);

        write_probe_req_frame(
            &mut w,
            self.iface_mac,
            &mut self.seq_mgr,
            &ssid,
            &rates[..],
            ht_cap,
            vht_cap,
        )?;
        let bytes_written = w.bytes_written();
        let out_buf = OutBuf::from(buf, bytes_written);
        self.device
            .send_wlan_frame(out_buf, TxFlags::NONE)
            .map_err(|s| Error::Status(format!("error sending probe req frame"), s))
    }

    /// Called when channel scheduler has gone through all the requested channels from a scan
    /// request. The scanner submits scan results to SME.
    pub fn handle_channel_req_complete(&mut self, request_id: channel_scheduler::RequestId) {
        match self.ongoing_scan_req {
            Some(OngoingScanRequest { chan_sched_req_id: Some(id), .. }) if id == request_id => (),
            _ => return,
        }
        // Safe to unwrap because control point only reaches here if there's ongoing scan request
        send_results_and_end(self.ongoing_scan_req.take().unwrap(), &mut self.device);
    }

    #[cfg(test)]
    pub fn chan_sched_req_id(&self) -> Option<channel_scheduler::RequestId> {
        match self.ongoing_scan_req {
            Some(OngoingScanRequest { chan_sched_req_id: id, .. }) => id,
            None => None,
        }
    }

    #[cfg(test)]
    pub fn probe_delay_timeout_id(&self) -> Option<EventId> {
        match self.ongoing_scan_req {
            Some(OngoingScanRequest { probe_delay_timeout_id: id, .. }) => id,
            None => None,
        }
    }
}

fn send_results_and_end(mut scan_req: OngoingScanRequest, device: &mut Device) {
    for (_, (_, bss_description)) in scan_req.seen_bss.drain().into_iter() {
        send_scan_result(scan_req.req.txn_id, bss_description, device);
    }
    send_scan_end(scan_req.req.txn_id, fidl_mlme::ScanResultCodes::Success, device);
}

fn send_scan_result(txn_id: u64, bss: fidl_mlme::BssDescription, device: &mut Device) {
    let result = device.access_sme_sender(|sender| {
        sender.send_on_scan_result(&mut fidl_mlme::ScanResult { txn_id, bss })
    });
    if let Err(e) = result {
        error!("error sending MLME ScanResult: {}", e);
    }
}

fn send_scan_end(txn_id: u64, code: fidl_mlme::ScanResultCodes, device: &mut Device) {
    let result = device.access_sme_sender(|sender| {
        sender.send_on_scan_end(&mut fidl_mlme::ScanEnd { txn_id, code })
    });
    if let Err(e) = result {
        error!("error sending MLME ScanEnd: {}", e);
    }
}

fn beacon_hash(
    beacon_interval: TimeUnit,
    capability_info: CapabilityInfo,
    ies: &[u8],
) -> BeaconHash {
    let mut hasher = hash_map::DefaultHasher::new();
    beacon_interval.hash(&mut hasher);
    capability_info.hash(&mut hasher);
    ies.hash(&mut hasher);
    hasher.finish()
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{
            buffer::FakeBufferProvider,
            client::channel_scheduler::{LEvent, MockListener},
            device::FakeDevice,
            timer::FakeScheduler,
        },
        fidl_fuchsia_wlan_common as fidl_common,
        std::{cell::RefCell, rc::Rc},
        wlan_common::assert_variant,
    };

    const BSSID: Bssid = Bssid([6u8; 6]);
    const IFACE_MAC: MacAddr = [7u8; 6];
    // Original channel set by FakeDevice
    const ORIGINAL_CHAN: WlanChannel = chan(0);

    const TIMESTAMP: u64 = 364983910445;
    // Capability information: ESS
    const CAPABILITY_INFO: CapabilityInfo = CapabilityInfo(1);
    const BEACON_INTERVAL: u16 = 100;
    const RX_INFO: RxInfo = RxInfo {
        chan: WlanChannel { primary: 11, cbw: WlanChannelBandwidth::_20, secondary80: 0 },
        rssi_dbm: -40,
        rcpi_dbmh: 30,
        rsni_dbh: 35,

        // Unused fields
        rx_flags: 0,
        valid_fields: 0,
        phy: 0,
        data_rate: 0,
        mcs: 0,
    };

    fn scan_req() -> fidl_mlme::ScanRequest {
        fidl_mlme::ScanRequest {
            txn_id: 1337,
            bss_type: fidl_mlme::BssTypes::Infrastructure,
            bssid: BSSID.0,
            ssid: b"ssid".to_vec(),
            scan_type: fidl_mlme::ScanTypes::Passive,
            probe_delay: 0,
            channel_list: Some(vec![6]),
            min_channel_time: 100,
            max_channel_time: 300,
            ssid_list: None,
        }
    }

    #[test]
    fn test_handle_scan_req_queues_channels() {
        let mut m = MockObjects::new();
        let mut scanner = m.create_scanner();

        scanner
            .handle_mlme_scan_req(scan_req(), m.create_channel_listener_fn(), &mut m.chan_sched)
            .expect("expect scan req accepted");
        assert_eq!(
            m.drain_listener_events(),
            vec![
                LEvent::PreSwitch { from: ORIGINAL_CHAN, to: chan(6), req_id: 1 },
                LEvent::PostSwitch { from: ORIGINAL_CHAN, to: chan(6), req_id: 1 },
            ]
        );
    }

    #[test]
    fn test_active_scan_probe_req_sent_with_no_delay() {
        let mut m = MockObjects::new();
        let mut scanner = m.create_scanner();

        let scan_req = fidl_mlme::ScanRequest {
            scan_type: fidl_mlme::ScanTypes::Active,
            probe_delay: 0,
            ..scan_req()
        };
        scanner
            .handle_mlme_scan_req(scan_req, m.create_channel_listener_fn(), &mut m.chan_sched)
            .expect("expect scan req accepted");
        assert_eq!(
            m.drain_listener_events(),
            vec![
                LEvent::PreSwitch { from: ORIGINAL_CHAN, to: chan(6), req_id: 1 },
                LEvent::PostSwitch { from: ORIGINAL_CHAN, to: chan(6), req_id: 1 },
            ]
        );

        // On post-switch announcement, the listener would call `begin_requested_channel_time`
        scanner.begin_requested_channel_time(chan(6));
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
            1, 4, // supp_rates id and length
            0xb0, 0x48, 0x60, 0x6c, // supp_rates
        ][..]);
    }

    #[test]
    fn test_active_scan_probe_req_sent_with_delay() {
        let mut m = MockObjects::new();
        let mut scanner = m.create_scanner();

        let scan_req = fidl_mlme::ScanRequest {
            scan_type: fidl_mlme::ScanTypes::Active,
            probe_delay: 5,
            ..scan_req()
        };
        scanner
            .handle_mlme_scan_req(scan_req, m.create_channel_listener_fn(), &mut m.chan_sched)
            .expect("expect scan req accepted");
        scanner.begin_requested_channel_time(chan(6));

        // Verify nothing is sent yet, but timeout is scheduled
        assert!(m.fake_device.wlan_queue.is_empty());
        assert!(scanner.probe_delay_timeout_id().is_some());
        let timeout_id = scanner.probe_delay_timeout_id().unwrap();
        assert_variant!(scanner.timer.triggered(&timeout_id), Some(event) => {
            assert_eq!(event, TimedEvent::ScannerProbeDelay(chan(6)));
        });

        // Check that telling scanner to handle timeout would send probe request frame
        scanner.handle_probe_delay_timeout(chan(6));
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
            1, 4, // supp_rates id and length
            0xb0, 0x48, 0x60, 0x6c, // supp_rates
        ][..]);
    }

    #[test]
    fn test_handle_scan_req_reject_if_busy() {
        let mut m = MockObjects::new();
        let mut scanner = m.create_scanner();

        scanner
            .handle_mlme_scan_req(scan_req(), m.create_channel_listener_fn(), &mut m.chan_sched)
            .expect("expect scan req accepted");
        let scan_req = fidl_mlme::ScanRequest { txn_id: 1338, ..scan_req() };
        let result = scanner.handle_mlme_scan_req(
            scan_req,
            m.create_channel_listener_fn(),
            &mut m.chan_sched,
        );
        assert_variant!(result, Err(ScanError::Busy));
        let scan_end = m
            .fake_device
            .next_mlme_msg::<fidl_mlme::ScanEnd>()
            .expect("error reading MLME ScanEnd");
        assert_eq!(
            scan_end,
            fidl_mlme::ScanEnd { txn_id: 1338, code: fidl_mlme::ScanResultCodes::NotSupported }
        );
    }

    #[test]
    fn test_handle_scan_req_empty_channel_list() {
        let mut m = MockObjects::new();
        let mut scanner = m.create_scanner();

        let scan_req = fidl_mlme::ScanRequest { channel_list: Some(vec![]), ..scan_req() };
        let result = scanner.handle_mlme_scan_req(
            scan_req,
            m.create_channel_listener_fn(),
            &mut m.chan_sched,
        );
        assert_variant!(result, Err(ScanError::EmptyChannelList));
        let scan_end = m
            .fake_device
            .next_mlme_msg::<fidl_mlme::ScanEnd>()
            .expect("error reading MLME ScanEnd");
        assert_eq!(
            scan_end,
            fidl_mlme::ScanEnd { txn_id: 1337, code: fidl_mlme::ScanResultCodes::InvalidArgs }
        );
    }

    #[test]
    fn test_handle_scan_req_long_channel_list() {
        let mut m = MockObjects::new();
        let mut scanner = m.create_scanner();

        let mut channel_list = vec![];
        for i in 1..=65 {
            channel_list.push(i);
        }
        let scan_req = fidl_mlme::ScanRequest { channel_list: Some(channel_list), ..scan_req() };
        let result = scanner.handle_mlme_scan_req(
            scan_req,
            m.create_channel_listener_fn(),
            &mut m.chan_sched,
        );
        assert_variant!(result, Err(ScanError::ChannelListTooLarge));
        let scan_end = m
            .fake_device
            .next_mlme_msg::<fidl_mlme::ScanEnd>()
            .expect("error reading MLME ScanEnd");
        assert_eq!(
            scan_end,
            fidl_mlme::ScanEnd { txn_id: 1337, code: fidl_mlme::ScanResultCodes::InvalidArgs }
        );
    }

    #[test]
    fn test_handle_scan_req_invalid_channel_time() {
        let mut m = MockObjects::new();
        let mut scanner = m.create_scanner();

        let scan_req =
            fidl_mlme::ScanRequest { min_channel_time: 101, max_channel_time: 100, ..scan_req() };
        let result = scanner.handle_mlme_scan_req(
            scan_req,
            m.create_channel_listener_fn(),
            &mut m.chan_sched,
        );
        assert_variant!(result, Err(ScanError::MaxChannelTimeLtMin));
        let scan_end = m
            .fake_device
            .next_mlme_msg::<fidl_mlme::ScanEnd>()
            .expect("error reading MLME ScanEnd");
        assert_eq!(
            scan_end,
            fidl_mlme::ScanEnd { txn_id: 1337, code: fidl_mlme::ScanResultCodes::InvalidArgs }
        );
    }

    #[test]
    fn test_handle_scan_req_ssid_too_long() {
        let mut m = MockObjects::new();
        let mut scanner = m.create_scanner();

        let scan_req = fidl_mlme::ScanRequest { ssid: vec![65; 33], ..scan_req() };
        let result = scanner.handle_mlme_scan_req(
            scan_req,
            m.create_channel_listener_fn(),
            &mut m.chan_sched,
        );
        assert_variant!(result, Err(ScanError::SsidTooLong));
        let scan_end = m
            .fake_device
            .next_mlme_msg::<fidl_mlme::ScanEnd>()
            .expect("error reading MLME ScanEnd");
        assert_eq!(
            scan_end,
            fidl_mlme::ScanEnd { txn_id: 1337, code: fidl_mlme::ScanResultCodes::InvalidArgs }
        );
    }

    #[test]
    fn test_start_hw_scan_success() {
        let mut m = MockObjects::new();
        m.fake_device.info.ifc_info.driver_features |= WlanInfoDriverFeature::SCAN_OFFLOAD;
        let mut scanner = m.create_scanner();

        scanner
            .handle_mlme_scan_req(scan_req(), m.create_channel_listener_fn(), &mut m.chan_sched)
            .expect("expect scan req accepted");

        // Verify that hw-scan is requested
        assert_variant!(m.fake_device.hw_scan_req, Some(config) => {
            assert_eq!(config.scan_type, WlanHwScanType::PASSIVE);
            assert_eq!(config.num_channels, 1);

            let mut channels = [0u8; WLAN_INFO_CHANNEL_LIST_MAX_CHANNELS];
            channels[..1].copy_from_slice(&[6]);
            assert_eq!(&config.channels[..], &channels[..]);

            let mut ssid = [0; WLAN_MAX_SSID_LEN as usize];
            ssid[..4].copy_from_slice(b"ssid");
            assert_eq!(config.ssid, WlanSsid { len: 4, ssid });
        }, "HW scan not initiated");

        // Verify that software scan is not scheduled
        assert!(scanner.chan_sched_req_id().is_none());

        // Mock receiving a beacon
        handle_beacon(&mut scanner, TIMESTAMP, &beacon_ies()[..]);

        // Verify scan results are sent on hw scan complete
        scanner.handle_hw_scan_complete(WlanHwScan::SUCCESS);
        m.fake_device.next_mlme_msg::<fidl_mlme::ScanResult>().expect("error reading ScanResult");
        let scan_end = m
            .fake_device
            .next_mlme_msg::<fidl_mlme::ScanEnd>()
            .expect("error reading MLME ScanEnd");
        assert_eq!(
            scan_end,
            fidl_mlme::ScanEnd { txn_id: 1337, code: fidl_mlme::ScanResultCodes::Success }
        );
    }

    #[test]
    fn test_start_hw_scan_fails() {
        let mut m = MockObjects::new();
        m.fake_device.info.ifc_info.driver_features |= WlanInfoDriverFeature::SCAN_OFFLOAD;
        let mut scanner = Scanner::new(
            m.fake_device.as_device_fail_start_hw_scan(),
            FakeBufferProvider::new(),
            Timer::<TimedEvent>::new(m.fake_scheduler.as_scheduler()),
            IFACE_MAC,
        );

        let result = scanner.handle_mlme_scan_req(
            scan_req(),
            m.create_channel_listener_fn(),
            &mut m.chan_sched,
        );
        assert_variant!(result, Err(ScanError::StartHwScanFails(zx::Status::NOT_SUPPORTED)));
        let scan_end = m
            .fake_device
            .next_mlme_msg::<fidl_mlme::ScanEnd>()
            .expect("error reading MLME ScanEnd");
        assert_eq!(
            scan_end,
            fidl_mlme::ScanEnd { txn_id: 1337, code: fidl_mlme::ScanResultCodes::InternalError }
        );
    }

    #[test]
    fn test_start_hw_scan_aborted() {
        let mut m = MockObjects::new();
        m.fake_device.info.ifc_info.driver_features |= WlanInfoDriverFeature::SCAN_OFFLOAD;
        let mut scanner = m.create_scanner();

        scanner
            .handle_mlme_scan_req(scan_req(), m.create_channel_listener_fn(), &mut m.chan_sched)
            .expect("expect scan req accepted");

        // Verify that hw-scan is requested
        assert_variant!(m.fake_device.hw_scan_req, Some(_), "HW scan not initiated");
        // Verify that software scan is not scheduled
        assert!(scanner.chan_sched_req_id().is_none());

        // Mock receiving a beacon
        handle_beacon(&mut scanner, TIMESTAMP, &beacon_ies()[..]);

        // Verify scan results are sent on hw scan complete
        scanner.handle_hw_scan_complete(WlanHwScan::ABORTED);
        let scan_end = m
            .fake_device
            .next_mlme_msg::<fidl_mlme::ScanEnd>()
            .expect("error reading MLME ScanEnd");
        assert_eq!(
            scan_end,
            fidl_mlme::ScanEnd { txn_id: 1337, code: fidl_mlme::ScanResultCodes::InternalError }
        );
    }

    #[test]
    fn test_handle_beacon_or_probe_response() {
        let mut m = MockObjects::new();
        let mut scanner = m.create_scanner();

        scanner
            .handle_mlme_scan_req(scan_req(), m.create_channel_listener_fn(), &mut m.chan_sched)
            .expect("expect scan req accepted");
        handle_beacon(&mut scanner, TIMESTAMP, &beacon_ies()[..]);
        let request_id = scanner.chan_sched_req_id().clone().unwrap();
        scanner.handle_channel_req_complete(request_id);

        let scan_result = m
            .fake_device
            .next_mlme_msg::<fidl_mlme::ScanResult>()
            .expect("error reading MLME ScanResult");
        assert_eq!(
            scan_result,
            fidl_mlme::ScanResult {
                txn_id: 1337,
                bss: fidl_mlme::BssDescription {
                    bssid: BSSID.0,
                    ssid: b"ssid".to_vec(),
                    bss_type: fidl_mlme::BssTypes::Infrastructure,
                    beacon_period: BEACON_INTERVAL,
                    dtim_period: 1,
                    timestamp: TIMESTAMP,
                    local_time: 0,
                    cap: CAPABILITY_INFO.0,
                    rates: vec![0xb0, 0x48, 0x60, 0x6c],
                    country: None,
                    rsne: None,
                    vendor_ies: None,
                    ht_cap: None,
                    ht_op: None,
                    vht_cap: None,
                    vht_op: None,
                    rssi_dbm: RX_INFO.rssi_dbm,
                    rcpi_dbmh: RX_INFO.rcpi_dbmh,
                    rsni_dbh: RX_INFO.rsni_dbh,
                    chan: fidl_common::WlanChan {
                        primary: RX_INFO.chan.primary,
                        cbw: fidl_common::Cbw::Cbw20,
                        secondary80: 0,
                    },
                }
            }
        );

        let scan_end = m
            .fake_device
            .next_mlme_msg::<fidl_mlme::ScanEnd>()
            .expect("error reading MLME ScanEnd");
        assert_eq!(
            scan_end,
            fidl_mlme::ScanEnd { txn_id: 1337, code: fidl_mlme::ScanResultCodes::Success }
        );
    }

    #[test]
    fn test_handle_beacon_or_probe_response_update() {
        let mut m = MockObjects::new();
        let mut scanner = m.create_scanner();

        scanner
            .handle_mlme_scan_req(scan_req(), m.create_channel_listener_fn(), &mut m.chan_sched)
            .expect("expect scan req accepted");
        handle_beacon(&mut scanner, TIMESTAMP, &beacon_ies()[..]);
        // Replace with beacon that has different SSID
        handle_beacon(&mut scanner, TIMESTAMP, &beacon_ies_2()[..]);
        let request_id = scanner.chan_sched_req_id().clone().unwrap();
        scanner.handle_channel_req_complete(request_id);

        // Verify that scan result has updated SSID
        let scan_result = m
            .fake_device
            .next_mlme_msg::<fidl_mlme::ScanResult>()
            .expect("error reading MLME ScanResult");
        assert_eq!(scan_result.bss.ssid, b"ss".to_vec());
        assert_eq!(scan_result.bss.rates, vec![0xb0, 0x48, 0x60, 0x6c]);
        assert_eq!(scan_result.bss.dtim_period, 1);

        let scan_end = m
            .fake_device
            .next_mlme_msg::<fidl_mlme::ScanEnd>()
            .expect("error reading MLME ScanEnd");
        assert_eq!(
            scan_end,
            fidl_mlme::ScanEnd { txn_id: 1337, code: fidl_mlme::ScanResultCodes::Success }
        );
    }

    #[test]
    fn test_handle_beacon_or_probe_response_older_timestamp() {
        let mut m = MockObjects::new();
        let mut scanner = m.create_scanner();

        scanner
            .handle_mlme_scan_req(scan_req(), m.create_channel_listener_fn(), &mut m.chan_sched)
            .expect("expect scan req accepted");
        handle_beacon(&mut scanner, TIMESTAMP, &beacon_ies()[..]);
        // Beacon comes with different SSID but older timestamp
        handle_beacon(&mut scanner, TIMESTAMP - 1, &beacon_ies_2()[..]);
        let request_id = scanner.chan_sched_req_id().clone().unwrap();
        scanner.handle_channel_req_complete(request_id);

        // Verify that scan result has first SSID, indicating that second call is ignored
        let scan_result = m
            .fake_device
            .next_mlme_msg::<fidl_mlme::ScanResult>()
            .expect("error reading MLME ScanResult");
        assert_eq!(scan_result.bss.ssid, b"ssid".to_vec());
        assert_eq!(scan_result.bss.rates, vec![0xb0, 0x48, 0x60, 0x6c]);
        assert_eq!(scan_result.bss.dtim_period, 1);

        let scan_end = m
            .fake_device
            .next_mlme_msg::<fidl_mlme::ScanEnd>()
            .expect("error reading MLME ScanEnd");
        assert_eq!(
            scan_end,
            fidl_mlme::ScanEnd { txn_id: 1337, code: fidl_mlme::ScanResultCodes::Success }
        );
    }

    #[test]
    fn test_handle_beacon_or_probe_response_blank_ssid() {
        let mut m = MockObjects::new();
        let mut scanner = m.create_scanner();

        scanner
            .handle_mlme_scan_req(scan_req(), m.create_channel_listener_fn(), &mut m.chan_sched)
            .expect("expect scan req accepted");
        handle_beacon(&mut scanner, TIMESTAMP, &beacon_ies()[..]);
        // Beacon comes with blank SSID
        handle_beacon(&mut scanner, TIMESTAMP, &beacon_ies_blank_ssid()[..]);
        let request_id = scanner.chan_sched_req_id().clone().unwrap();
        scanner.handle_channel_req_complete(request_id);

        // Verify that we keep existing SSID
        let scan_result = m
            .fake_device
            .next_mlme_msg::<fidl_mlme::ScanResult>()
            .expect("error reading MLME ScanResult");
        assert_eq!(scan_result.bss.ssid, b"ssid".to_vec());
        // Second beacon also has different rate. This verifies that second beacon is processed.
        assert_eq!(scan_result.bss.rates, vec![0x48, 0x60, 0x6c]);
        assert_eq!(scan_result.bss.dtim_period, 1);

        let scan_end = m
            .fake_device
            .next_mlme_msg::<fidl_mlme::ScanEnd>()
            .expect("error reading MLME ScanEnd");
        assert_eq!(
            scan_end,
            fidl_mlme::ScanEnd { txn_id: 1337, code: fidl_mlme::ScanResultCodes::Success }
        );
    }

    #[test]
    fn test_handle_beacon_or_probe_response_no_tim() {
        let mut m = MockObjects::new();
        let mut scanner = m.create_scanner();

        scanner
            .handle_mlme_scan_req(scan_req(), m.create_channel_listener_fn(), &mut m.chan_sched)
            .expect("expect scan req accepted");
        handle_beacon(&mut scanner, TIMESTAMP, &beacon_ies()[..]);
        // Beacon comes with blank SSID
        handle_beacon(&mut scanner, TIMESTAMP, &beacon_ies_no_tim()[..]);
        let request_id = scanner.chan_sched_req_id().clone().unwrap();
        scanner.handle_channel_req_complete(request_id);

        // Verify that we keep existing DTIM period
        let scan_result = m
            .fake_device
            .next_mlme_msg::<fidl_mlme::ScanResult>()
            .expect("error reading MLME ScanResult");
        assert_eq!(scan_result.bss.dtim_period, 1);
        // Second frame also has different rate. This verifies that it is processed.
        assert_eq!(scan_result.bss.rates, vec![0x48, 0x60, 0x6c]);
        assert_eq!(scan_result.bss.ssid, b"ssid".to_vec());

        let scan_end = m
            .fake_device
            .next_mlme_msg::<fidl_mlme::ScanEnd>()
            .expect("error reading MLME ScanEnd");
        assert_eq!(
            scan_end,
            fidl_mlme::ScanEnd { txn_id: 1337, code: fidl_mlme::ScanResultCodes::Success }
        );
    }

    fn handle_beacon(scanner: &mut Scanner, timestamp: u64, ies: &[u8]) {
        scanner.handle_beacon_or_probe_response(
            BSSID,
            timestamp,
            TimeUnit(BEACON_INTERVAL),
            CAPABILITY_INFO,
            ies,
            RX_INFO,
        );
    }

    const fn chan(primary: u8) -> WlanChannel {
        WlanChannel { primary, cbw: WlanChannelBandwidth::_20, secondary80: 0 }
    }

    fn beacon_ies() -> Vec<u8> {
        #[rustfmt::skip]
        let ies = vec![
            // SSID: "ssid"
            0x00, 0x04, 115, 115, 105, 100,
            // Supported rates: 24(B), 36, 48, 54
            0x01, 0x04, 0xb0, 0x48, 0x60, 0x6c,
            // TIM - DTIM count: 0, DTIM period: 1, PVB: 2
            0x05, 0x04, 0x00, 0x01, 0x00, 0x02,
        ];
        ies
    }

    fn beacon_ies_2() -> Vec<u8> {
        #[rustfmt::skip]
        let ies = vec![
            // SSID: "ss"
            0x00, 0x02, 115, 115,
            // Supported rates: 24(B), 36, 48, 54
            0x01, 0x04, 0xb0, 0x48, 0x60, 0x6c,
            // TIM - DTIM count: 0, DTIM period: 1, PVB: 2
            0x05, 0x04, 0x00, 0x01, 0x00, 0x02,
        ];
        ies
    }

    fn beacon_ies_no_tim() -> Vec<u8> {
        #[rustfmt::skip]
        let ies = vec![
            // SSID: "ssid"
            0x00, 0x04, 115, 115, 105, 100,
            // Supported rates: 36, 48, 54
            0x01, 0x03, 0x48, 0x60, 0x6c,
        ];
        ies
    }

    fn beacon_ies_blank_ssid() -> Vec<u8> {
        #[rustfmt::skip]
        let ies = vec![
            // SSID: ""
            0x00, 0x04, 0x00, 0x00, 0x00, 0x00,
            // Supported rates: 36, 48, 54
            0x01, 0x03, 0x48, 0x60, 0x6c,
            // TIM - DTIM count: 0, DTIM period: 1, PVB: 2
            0x05, 0x04, 0x00, 0x01, 0x00, 0x02,
        ];
        ies
    }

    struct MockObjects {
        fake_device: Box<FakeDevice>,
        fake_scheduler: Box<FakeScheduler>,
        listener_events: Rc<RefCell<Vec<LEvent>>>,
        chan_sched: ChannelScheduler<MockListener>,
    }

    impl MockObjects {
        fn new() -> Self {
            // Boxing these so that their memory locations don't change when moved. Important
            // since `as_scheduler` and `as_device` are called before they are moved into
            // MockObjects struct.
            let mut fake_device = Box::new(FakeDevice::new());
            let mut fake_scheduler = Box::new(FakeScheduler::new());

            let timer = Timer::<TimedEvent>::new(fake_scheduler.as_scheduler());
            let chan_sched = ChannelScheduler::new(fake_device.as_device(), timer);

            Self {
                fake_device,
                fake_scheduler,
                listener_events: Rc::new(RefCell::new(vec![])),
                chan_sched,
            }
        }

        fn create_scanner(&mut self) -> Scanner {
            Scanner::new(
                self.fake_device.as_device(),
                FakeBufferProvider::new(),
                Timer::<TimedEvent>::new(self.fake_scheduler.as_scheduler()),
                IFACE_MAC,
            )
        }

        fn create_channel_listener_fn(&mut self) -> impl FnOnce(&mut Scanner) -> MockListener {
            let events = Rc::clone(&self.listener_events);
            |_| MockListener { events }
        }

        fn drain_listener_events(&mut self) -> Vec<LEvent> {
            self.listener_events.borrow_mut().drain(..).collect()
        }
    }
}
