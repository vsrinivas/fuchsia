// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        buffer::OutBuf,
        client::{
            channel_listener::{ChannelListener, ChannelListenerSource},
            channel_scheduler::ChannelScheduler,
            convert_beacon::construct_bss_description,
            frame_writer::write_probe_req_frame,
            Context, TimedEvent,
        },
        device::{Device, TxFlags},
        error::Error,
        timer::EventId,
    },
    anyhow::format_err,
    banjo_ddk_hw_wlan_wlaninfo as banjo_hw_wlaninfo,
    banjo_ddk_protocol_wlan_info as banjo_wlan_info, banjo_ddk_protocol_wlan_mac as banjo_wlan_mac,
    fidl_fuchsia_wlan_mlme as fidl_mlme, fuchsia_zircon as zx,
    log::{error, warn},
    std::{
        collections::{hash_map, HashMap},
        hash::{Hash, Hasher},
    },
    thiserror::Error,
    wlan_common::{
        buffer_writer::BufferWriter,
        frame_len,
        ie::{IE_PREFIX_LEN, SUPPORTED_RATES_MAX_LEN},
        mac::{self, Bssid, CapabilityInfo, MacAddr},
        time::TimeUnit,
    },
};

type BeaconHash = u64;

#[derive(Error, Debug, PartialEq, Eq)]
pub enum ScanError {
    #[error("scanner is busy")]
    Busy,
    #[error("invalid arg: empty channel list")]
    EmptyChannelList,
    #[error("invalid arg: channel list too large")]
    ChannelListTooLarge,
    #[error("invalid arg: max_channel_time < min_channel_time")]
    MaxChannelTimeLtMin,
    #[error("invalid arg: SSID too long")]
    SsidTooLong,
    #[error("fail starting hw scan: {}", _0)]
    StartHwScanFails(zx::Status),
    #[error("hw scan aborted")]
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

impl From<ScanError> for fidl_mlme::ScanResultCodes {
    fn from(e: ScanError) -> Self {
        match e {
            ScanError::Busy => fidl_mlme::ScanResultCodes::NotSupported,
            ScanError::EmptyChannelList
            | ScanError::ChannelListTooLarge
            | ScanError::MaxChannelTimeLtMin
            | ScanError::SsidTooLong => fidl_mlme::ScanResultCodes::InvalidArgs,
            ScanError::StartHwScanFails(..) | ScanError::HwScanAborted => {
                fidl_mlme::ScanResultCodes::InternalError
            }
        }
    }
}

pub struct Scanner {
    ongoing_scan: Option<OngoingScan>,
    /// MAC address of current client interface
    iface_mac: MacAddr,
}

impl Scanner {
    pub fn new(iface_mac: MacAddr) -> Self {
        Self { ongoing_scan: None, iface_mac }
    }

    pub fn bind<'a>(&'a mut self, ctx: &'a mut Context) -> BoundScanner<'a> {
        BoundScanner { scanner: self, ctx }
    }

    #[cfg(test)]
    pub fn probe_delay_timeout_id(&self) -> Option<EventId> {
        match self.ongoing_scan {
            Some(OngoingScan { probe_delay_timeout_id: id, .. }) => id,
            None => None,
        }
    }
}

pub struct BoundScanner<'a> {
    scanner: &'a mut Scanner,
    ctx: &'a mut Context,
}

struct OngoingScan {
    /// Scan request that's currently being serviced.
    req: fidl_mlme::ScanRequest,
    /// BSS seen from beacon or probe response. This is populated while scan request is in
    /// progress and is cleared out otherwise.
    seen_bss: HashMap<Bssid, (BeaconHash, fidl_mlme::BssDescription)>,
    /// ID of timeout event scheduled for active scan at beginning of each channel switch. At
    /// end of timeout, a probe request is sent.
    probe_delay_timeout_id: Option<EventId>,
}

impl<'a> BoundScanner<'a> {
    /// Handle scan request. Queue requested scan channels in channel scheduler.
    ///
    /// If a scan request is in progress, or the new request has invalid argument (empty channel
    /// list or larger min channel time than max), then the request is rejected.
    pub fn handle_mlme_scan_req<F, CL>(
        &'a mut self,
        req: fidl_mlme::ScanRequest,
        build_channel_listener: F,
        chan_sched: &mut ChannelScheduler,
    ) -> Result<(), ScanError>
    where
        F: FnOnce(&'a mut Context, &'a mut Scanner) -> CL,
        CL: ChannelListener,
    {
        macro_rules! send_scan_end_and_return {
            ($txn_id:expr, $scan_error:expr, $self:expr) => {{
                let error = $scan_error;
                send_scan_end($txn_id, error.into(), &mut $self.ctx.device);
                return Err($scan_error);
            }};
        }

        if self.scanner.ongoing_scan.is_some() {
            send_scan_end_and_return!(req.txn_id, ScanError::Busy, self);
        }

        let channel_list = req.channel_list.as_ref().map(|list| list.as_slice()).unwrap_or(&[][..]);
        if channel_list.is_empty() {
            send_scan_end_and_return!(req.txn_id, ScanError::EmptyChannelList, self);
        }
        if channel_list.len() > banjo_hw_wlaninfo::WLAN_INFO_CHANNEL_LIST_MAX_CHANNELS {
            send_scan_end_and_return!(req.txn_id, ScanError::ChannelListTooLarge, self);
        }
        if req.max_channel_time < req.min_channel_time {
            send_scan_end_and_return!(req.txn_id, ScanError::MaxChannelTimeLtMin, self);
        }
        if req.ssid.len() > banjo_wlan_info::WLAN_MAX_SSID_LEN as usize {
            send_scan_end_and_return!(req.txn_id, ScanError::SsidTooLong, self);
        }

        let wlan_info = self.ctx.device.wlan_info().ifc_info;
        let hw_scan =
            (wlan_info.driver_features & banjo_hw_wlaninfo::WlanInfoDriverFeature::SCAN_OFFLOAD).0
                > 0;
        if hw_scan {
            let scan_type = if req.scan_type == fidl_mlme::ScanTypes::Active {
                banjo_wlan_mac::WlanHwScanType::ACTIVE
            } else {
                banjo_wlan_mac::WlanHwScanType::PASSIVE
            };
            let mut channels = [0; banjo_hw_wlaninfo::WLAN_INFO_CHANNEL_LIST_MAX_CHANNELS];
            channels[..channel_list.len()].copy_from_slice(channel_list);
            let mut ssid = [0; banjo_wlan_info::WLAN_MAX_SSID_LEN as usize];
            ssid[..req.ssid.len()].copy_from_slice(&req.ssid[..]);

            let config = banjo_wlan_mac::WlanHwScanConfig {
                scan_type,
                num_channels: channel_list.len() as u8,
                channels,
                ssid: banjo_wlan_info::WlanSsid { len: req.ssid.len() as u8, ssid },
            };
            if let Err(status) = self.ctx.device.start_hw_scan(&config) {
                self.scanner.ongoing_scan.take();
                send_scan_end_and_return!(req.txn_id, ScanError::StartHwScanFails(status), self);
            }
            self.scanner.ongoing_scan =
                Some(OngoingScan { req, seen_bss: HashMap::new(), probe_delay_timeout_id: None });
        } else {
            let channels = channel_list
                .iter()
                .map(|c| banjo_wlan_info::WlanChannel {
                    primary: *c,
                    cbw: banjo_wlan_info::WlanChannelBandwidth::_20,
                    secondary80: 0,
                })
                .collect();
            let max_channel_time = req.max_channel_time;
            // Note: for software scanning case, it's important to populate this beforehand because
            //       channel scheduler may `begin_requested_channel_time` immediately, and scanner
            //       needs these information to determine whether to send probe request.
            self.scanner.ongoing_scan =
                Some(OngoingScan { req, seen_bss: HashMap::new(), probe_delay_timeout_id: None });
            let mut listener = build_channel_listener(self.ctx, self.scanner);
            let dwell_time = TimeUnit(max_channel_time as u16).into();
            chan_sched
                .bind(&mut listener, ChannelListenerSource::Scanner)
                .queue_channels(channels, dwell_time);
        }
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
        rx_info: Option<banjo_wlan_mac::WlanRxInfo>,
    ) {
        let seen_bss = match &mut self.scanner.ongoing_scan {
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
                    // TIM element is present in beacon frame but not probe response. In case we
                    // see no DTIM period in latest frame, use value from previous one.
                    if new_bss_description.dtim_period == 0 {
                        new_bss_description.dtim_period = bss_description.dtim_period;
                    }
                    // If there's no RX info, keep old values
                    if rx_info.is_none() {
                        new_bss_description.rssi_dbm = bss_description.rssi_dbm;
                        new_bss_description.rcpi_dbmh = bss_description.rcpi_dbmh;
                        new_bss_description.rsni_dbh = bss_description.rsni_dbh;
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
    pub fn handle_probe_delay_timeout(&mut self, channel: banjo_wlan_info::WlanChannel) {
        let ssid = match &self.scanner.ongoing_scan {
            Some(OngoingScan { req, .. }) => req.ssid.clone(),
            None => return,
        };
        if let Err(e) = self.send_probe_req(&ssid[..], channel) {
            error!("{}", e);
        }
    }

    pub fn handle_hw_scan_complete(&mut self, status: banjo_wlan_mac::WlanHwScan) {
        let req = match self.scanner.ongoing_scan.take() {
            Some(req) => req,
            None => {
                warn!("Received HwScanComplete with status {:?} while no req in progress", status);
                return;
            }
        };

        if status == banjo_wlan_mac::WlanHwScan::SUCCESS {
            send_results_and_end(req, &mut self.ctx.device);
        } else {
            send_scan_end(
                req.req.txn_id,
                fidl_mlme::ScanResultCodes::InternalError,
                &mut self.ctx.device,
            );
        }
    }

    /// Called after switching to a requested channel from a scan request. It's primarily to
    /// send out, or schedule to send out, a probe request in an active scan.
    pub fn begin_requested_channel_time(&mut self, channel: banjo_wlan_info::WlanChannel) {
        let (req, probe_delay_timeout_id) = match &mut self.scanner.ongoing_scan {
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
                let deadline = self.ctx.timer.now() + TimeUnit(req.probe_delay as u16).into();
                let timeout_id =
                    self.ctx.timer.schedule_event(deadline, TimedEvent::ScannerProbeDelay(channel));
                if let Some(old_id) = probe_delay_timeout_id.replace(timeout_id) {
                    self.ctx.timer.cancel_event(old_id);
                }
            }
        }
    }

    fn send_probe_req(
        &mut self,
        ssid: &[u8],
        channel: banjo_wlan_info::WlanChannel,
    ) -> Result<(), Error> {
        let iface_info = self.ctx.device.wlan_info().ifc_info;
        let band_info = get_band_info(&iface_info, channel)
            .ok_or(format_err!("no band found for chan {:?}", channel.primary))?;
        let rates: Vec<u8> = band_info.rates.iter().cloned().filter(|r| *r > 0).collect();

        let mgmt_hdr_len = frame_len!(mac::MgmtHdr);
        let ssid_len = IE_PREFIX_LEN + ssid.len();
        let rates_len = (IE_PREFIX_LEN + rates.len())
            + if rates.len() > SUPPORTED_RATES_MAX_LEN { IE_PREFIX_LEN } else { 0 };

        let frame_len = mgmt_hdr_len + ssid_len + rates_len;
        let mut buf = self.ctx.buf_provider.get_buffer(frame_len)?;
        let mut w = BufferWriter::new(&mut buf[..]);

        write_probe_req_frame(
            &mut w,
            self.scanner.iface_mac,
            &mut self.ctx.seq_mgr,
            &ssid,
            &rates[..],
        )?;
        let bytes_written = w.bytes_written();
        let out_buf = OutBuf::from(buf, bytes_written);
        self.ctx
            .device
            .send_wlan_frame(out_buf, TxFlags::NONE)
            .map_err(|s| Error::Status(format!("error sending probe req frame"), s))
    }

    /// Called when channel scheduler has gone through all the requested channels from a scan
    /// request. The scanner submits scan results to SME.
    pub fn handle_channel_req_complete(&mut self) {
        if let Some(req) = self.scanner.ongoing_scan.take() {
            send_results_and_end(req, &mut self.ctx.device);
        }
    }
}

fn get_band_info(
    iface_info: &banjo_hw_wlaninfo::WlanInfo,
    channel: banjo_wlan_info::WlanChannel,
) -> Option<&banjo_hw_wlaninfo::WlanInfoBandInfo> {
    const _2GHZ_BAND_HIGHEST_CHANNEL: u8 = 14;
    iface_info.bands[..iface_info.bands_count]
        .iter()
        .filter(|b| match channel.primary {
            x if x > _2GHZ_BAND_HIGHEST_CHANNEL => b.band == banjo_hw_wlaninfo::WlanInfoBand::_5GHZ,
            _ => b.band == banjo_hw_wlaninfo::WlanInfoBand::_2GHZ,
        })
        .next()
}

fn send_results_and_end(mut scan_req: OngoingScan, device: &mut Device) {
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
            client::{
                channel_listener::{LEvent, MockListenerState},
                channel_scheduler, ClientConfig,
            },
            device::FakeDevice,
            timer::{FakeScheduler, Timer},
        },
        fidl_fuchsia_wlan_common as fidl_common,
        std::{cell::RefCell, rc::Rc},
        wlan_common::{assert_variant, sequence::SequenceManager},
    };

    const BSSID: Bssid = Bssid([6u8; 6]);
    const IFACE_MAC: MacAddr = [7u8; 6];
    // Original channel set by FakeDevice
    const ORIGINAL_CHAN: banjo_wlan_info::WlanChannel = chan(0);

    const TIMESTAMP: u64 = 364983910445;
    // Capability information: ESS
    const CAPABILITY_INFO: CapabilityInfo = CapabilityInfo(1);
    const BEACON_INTERVAL: u16 = 100;
    const RX_INFO: banjo_wlan_mac::WlanRxInfo = banjo_wlan_mac::WlanRxInfo {
        chan: banjo_wlan_info::WlanChannel {
            primary: 11,
            cbw: banjo_wlan_info::WlanChannelBandwidth::_20,
            secondary80: 0,
        },
        rssi_dbm: -40,
        rcpi_dbmh: 30,
        snr_dbh: 35,

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
        let mut ctx = m.make_ctx();
        let mut scanner = Scanner::new(IFACE_MAC);

        scanner
            .bind(&mut ctx)
            .handle_mlme_scan_req(
                scan_req(),
                m.listener_state.create_channel_listener_fn(),
                &mut m.chan_sched,
            )
            .expect("expect scan req accepted");
        let req_id = channel_scheduler::RequestId(1, ChannelListenerSource::Scanner);
        assert_eq!(
            m.listener_state.drain_events(),
            vec![
                LEvent::PreSwitch { from: ORIGINAL_CHAN, to: chan(6), req_id },
                LEvent::PostSwitch { from: ORIGINAL_CHAN, to: chan(6), req_id },
            ]
        );
    }

    #[test]
    fn test_active_scan_probe_req_sent_with_no_delay() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut scanner = Scanner::new(IFACE_MAC);

        let scan_req = fidl_mlme::ScanRequest {
            scan_type: fidl_mlme::ScanTypes::Active,
            probe_delay: 0,
            ..scan_req()
        };
        scanner
            .bind(&mut ctx)
            .handle_mlme_scan_req(
                scan_req,
                m.listener_state.create_channel_listener_fn(),
                &mut m.chan_sched,
            )
            .expect("expect scan req accepted");
        let req_id = channel_scheduler::RequestId(1, ChannelListenerSource::Scanner);
        assert_eq!(
            m.listener_state.drain_events(),
            vec![
                LEvent::PreSwitch { from: ORIGINAL_CHAN, to: chan(6), req_id },
                LEvent::PostSwitch { from: ORIGINAL_CHAN, to: chan(6), req_id },
            ]
        );

        // On post-switch announcement, the listener would call `begin_requested_channel_time`
        scanner.bind(&mut ctx).begin_requested_channel_time(chan(6));
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
    }

    #[test]
    fn test_active_scan_probe_req_sent_with_delay() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut scanner = Scanner::new(IFACE_MAC);

        let scan_req = fidl_mlme::ScanRequest {
            scan_type: fidl_mlme::ScanTypes::Active,
            probe_delay: 5,
            ..scan_req()
        };
        scanner
            .bind(&mut ctx)
            .handle_mlme_scan_req(
                scan_req,
                m.listener_state.create_channel_listener_fn(),
                &mut m.chan_sched,
            )
            .expect("expect scan req accepted");
        scanner.bind(&mut ctx).begin_requested_channel_time(chan(6));

        // Verify nothing is sent yet, but timeout is scheduled
        assert!(m.fake_device.wlan_queue.is_empty());
        assert!(scanner.probe_delay_timeout_id().is_some());
        let timeout_id = scanner.probe_delay_timeout_id().unwrap();
        assert_variant!(ctx.timer.triggered(&timeout_id), Some(event) => {
            assert_eq!(event, TimedEvent::ScannerProbeDelay(chan(6)));
        });

        // Check that telling scanner to handle timeout would send probe request frame
        scanner.bind(&mut ctx).handle_probe_delay_timeout(chan(6));
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
    }

    #[test]
    fn test_handle_scan_req_reject_if_busy() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut scanner = Scanner::new(IFACE_MAC);

        scanner
            .bind(&mut ctx)
            .handle_mlme_scan_req(
                scan_req(),
                m.listener_state.create_channel_listener_fn(),
                &mut m.chan_sched,
            )
            .expect("expect scan req accepted");
        let scan_req = fidl_mlme::ScanRequest { txn_id: 1338, ..scan_req() };
        let result = scanner.bind(&mut ctx).handle_mlme_scan_req(
            scan_req,
            m.listener_state.create_channel_listener_fn(),
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
        let mut ctx = m.make_ctx();
        let mut scanner = Scanner::new(IFACE_MAC);

        let scan_req = fidl_mlme::ScanRequest { channel_list: Some(vec![]), ..scan_req() };
        let result = scanner.bind(&mut ctx).handle_mlme_scan_req(
            scan_req,
            m.listener_state.create_channel_listener_fn(),
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
        let mut ctx = m.make_ctx();
        let mut scanner = Scanner::new(IFACE_MAC);

        let mut channel_list = vec![];
        for i in 1..=65 {
            channel_list.push(i);
        }
        let scan_req = fidl_mlme::ScanRequest { channel_list: Some(channel_list), ..scan_req() };
        let result = scanner.bind(&mut ctx).handle_mlme_scan_req(
            scan_req,
            m.listener_state.create_channel_listener_fn(),
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
        let mut ctx = m.make_ctx();
        let mut scanner = Scanner::new(IFACE_MAC);

        let scan_req =
            fidl_mlme::ScanRequest { min_channel_time: 101, max_channel_time: 100, ..scan_req() };
        let result = scanner.bind(&mut ctx).handle_mlme_scan_req(
            scan_req,
            m.listener_state.create_channel_listener_fn(),
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
        let mut ctx = m.make_ctx();
        let mut scanner = Scanner::new(IFACE_MAC);

        let scan_req = fidl_mlme::ScanRequest { ssid: vec![65; 33], ..scan_req() };
        let result = scanner.bind(&mut ctx).handle_mlme_scan_req(
            scan_req,
            m.listener_state.create_channel_listener_fn(),
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
        let mut ctx = m.make_ctx();
        m.fake_device.info.ifc_info.driver_features |=
            banjo_hw_wlaninfo::WlanInfoDriverFeature::SCAN_OFFLOAD;
        let mut scanner = Scanner::new(IFACE_MAC);

        scanner
            .bind(&mut ctx)
            .handle_mlme_scan_req(
                scan_req(),
                m.listener_state.create_channel_listener_fn(),
                &mut m.chan_sched,
            )
            .expect("expect scan req accepted");

        // Verify that hw-scan is requested
        assert_variant!(m.fake_device.hw_scan_req, Some(config) => {
            assert_eq!(config.scan_type, banjo_wlan_mac::WlanHwScanType::PASSIVE);
            assert_eq!(config.num_channels, 1);

            let mut channels = [0u8; banjo_hw_wlaninfo::WLAN_INFO_CHANNEL_LIST_MAX_CHANNELS];
            channels[..1].copy_from_slice(&[6]);
            assert_eq!(&config.channels[..], &channels[..]);

            let mut ssid = [0; banjo_wlan_info::WLAN_MAX_SSID_LEN as usize];
            ssid[..4].copy_from_slice(b"ssid");
            assert_eq!(config.ssid, banjo_wlan_info::WlanSsid { len: 4, ssid });
        }, "HW scan not initiated");

        // Mock receiving a beacon
        handle_beacon(&mut scanner, &mut ctx, TIMESTAMP, &beacon_ies()[..]);

        // Verify scan results are sent on hw scan complete
        scanner.bind(&mut ctx).handle_hw_scan_complete(banjo_wlan_mac::WlanHwScan::SUCCESS);
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
        let device = m.fake_device.as_device_fail_start_hw_scan();
        let mut ctx = m.make_ctx_with_device(device);
        m.fake_device.info.ifc_info.driver_features |=
            banjo_hw_wlaninfo::WlanInfoDriverFeature::SCAN_OFFLOAD;
        let mut scanner = Scanner::new(IFACE_MAC);

        let result = scanner.bind(&mut ctx).handle_mlme_scan_req(
            scan_req(),
            m.listener_state.create_channel_listener_fn(),
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
        let mut ctx = m.make_ctx();
        m.fake_device.info.ifc_info.driver_features |=
            banjo_hw_wlaninfo::WlanInfoDriverFeature::SCAN_OFFLOAD;
        let mut scanner = Scanner::new(IFACE_MAC);

        scanner
            .bind(&mut ctx)
            .handle_mlme_scan_req(
                scan_req(),
                m.listener_state.create_channel_listener_fn(),
                &mut m.chan_sched,
            )
            .expect("expect scan req accepted");

        // Verify that hw-scan is requested
        assert_variant!(m.fake_device.hw_scan_req, Some(_), "HW scan not initiated");

        // Mock receiving a beacon
        handle_beacon(&mut scanner, &mut ctx, TIMESTAMP, &beacon_ies()[..]);

        // Verify scan results are sent on hw scan complete
        scanner.bind(&mut ctx).handle_hw_scan_complete(banjo_wlan_mac::WlanHwScan::ABORTED);
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
        let mut ctx = m.make_ctx();
        let mut scanner = Scanner::new(IFACE_MAC);

        scanner
            .bind(&mut ctx)
            .handle_mlme_scan_req(
                scan_req(),
                m.listener_state.create_channel_listener_fn(),
                &mut m.chan_sched,
            )
            .expect("expect scan req accepted");
        handle_beacon(&mut scanner, &mut ctx, TIMESTAMP, &beacon_ies()[..]);
        scanner.bind(&mut ctx).handle_channel_req_complete();

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
                    rsni_dbh: RX_INFO.snr_dbh,
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
        let mut ctx = m.make_ctx();
        let mut scanner = Scanner::new(IFACE_MAC);

        scanner
            .bind(&mut ctx)
            .handle_mlme_scan_req(
                scan_req(),
                m.listener_state.create_channel_listener_fn(),
                &mut m.chan_sched,
            )
            .expect("expect scan req accepted");
        handle_beacon(&mut scanner, &mut ctx, TIMESTAMP, &beacon_ies()[..]);
        // Replace with beacon that has different SSID
        handle_beacon(&mut scanner, &mut ctx, TIMESTAMP, &beacon_ies_2()[..]);
        scanner.bind(&mut ctx).handle_channel_req_complete();

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
        let mut ctx = m.make_ctx();
        let mut scanner = Scanner::new(IFACE_MAC);

        scanner
            .bind(&mut ctx)
            .handle_mlme_scan_req(
                scan_req(),
                m.listener_state.create_channel_listener_fn(),
                &mut m.chan_sched,
            )
            .expect("expect scan req accepted");
        handle_beacon(&mut scanner, &mut ctx, TIMESTAMP, &beacon_ies()[..]);
        // Beacon comes with different SSID but older timestamp
        handle_beacon(&mut scanner, &mut ctx, TIMESTAMP - 1, &beacon_ies_2()[..]);
        scanner.bind(&mut ctx).handle_channel_req_complete();

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
        let mut ctx = m.make_ctx();
        let mut scanner = Scanner::new(IFACE_MAC);

        scanner
            .bind(&mut ctx)
            .handle_mlme_scan_req(
                scan_req(),
                m.listener_state.create_channel_listener_fn(),
                &mut m.chan_sched,
            )
            .expect("expect scan req accepted");
        handle_beacon(&mut scanner, &mut ctx, TIMESTAMP, &beacon_ies()[..]);
        // Beacon comes with blank SSID
        handle_beacon(&mut scanner, &mut ctx, TIMESTAMP, &beacon_ies_blank_ssid()[..]);
        scanner.bind(&mut ctx).handle_channel_req_complete();

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
        let mut ctx = m.make_ctx();
        let mut scanner = Scanner::new(IFACE_MAC);

        scanner
            .bind(&mut ctx)
            .handle_mlme_scan_req(
                scan_req(),
                m.listener_state.create_channel_listener_fn(),
                &mut m.chan_sched,
            )
            .expect("expect scan req accepted");
        handle_beacon(&mut scanner, &mut ctx, TIMESTAMP, &beacon_ies()[..]);
        // Beacon comes with blank SSID
        handle_beacon(&mut scanner, &mut ctx, TIMESTAMP, &beacon_ies_no_tim()[..]);
        scanner.bind(&mut ctx).handle_channel_req_complete();

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

    #[test]
    fn test_handle_beacon_or_probe_response_no_rx_info() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut scanner = Scanner::new(IFACE_MAC);

        scanner
            .bind(&mut ctx)
            .handle_mlme_scan_req(
                scan_req(),
                m.listener_state.create_channel_listener_fn(),
                &mut m.chan_sched,
            )
            .expect("expect scan req accepted");
        handle_beacon(&mut scanner, &mut ctx, TIMESTAMP, &beacon_ies()[..]);
        // No RX info
        scanner.bind(&mut ctx).handle_beacon_or_probe_response(
            BSSID,
            TIMESTAMP,
            TimeUnit(BEACON_INTERVAL),
            CAPABILITY_INFO,
            &beacon_ies()[..],
            None,
        );
        scanner.bind(&mut ctx).handle_channel_req_complete();

        // Verify that we keep existing channel and signal strength measurements
        let scan_result = m
            .fake_device
            .next_mlme_msg::<fidl_mlme::ScanResult>()
            .expect("error reading MLME ScanResult");
        assert_eq!(scan_result.bss.rssi_dbm, -40);
        assert_eq!(scan_result.bss.rcpi_dbmh, 30);
        assert_eq!(scan_result.bss.rsni_dbh, 35);
        assert_eq!(
            scan_result.bss.chan,
            fidl_common::WlanChan {
                primary: RX_INFO.chan.primary,
                cbw: fidl_common::Cbw::Cbw20,
                secondary80: 0,
            }
        );
    }

    fn handle_beacon(scanner: &mut Scanner, ctx: &mut Context, timestamp: u64, ies: &[u8]) {
        scanner.bind(ctx).handle_beacon_or_probe_response(
            BSSID,
            timestamp,
            TimeUnit(BEACON_INTERVAL),
            CAPABILITY_INFO,
            ies,
            Some(RX_INFO),
        );
    }

    const fn chan(primary: u8) -> banjo_wlan_info::WlanChannel {
        banjo_wlan_info::WlanChannel {
            primary,
            cbw: banjo_wlan_info::WlanChannelBandwidth::_20,
            secondary80: 0,
        }
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
        fake_device: FakeDevice,
        fake_scheduler: FakeScheduler,
        listener_state: MockListenerState,
        chan_sched: ChannelScheduler,
    }

    impl MockObjects {
        fn new() -> Self {
            Self {
                fake_device: FakeDevice::new(),
                fake_scheduler: FakeScheduler::new(),
                listener_state: MockListenerState { events: Rc::new(RefCell::new(vec![])) },
                chan_sched: ChannelScheduler::new(),
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
            }
        }
    }
}
