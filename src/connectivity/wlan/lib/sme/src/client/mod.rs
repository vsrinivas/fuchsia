// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod bss;
mod capabilities;
mod event;
pub mod info;
mod inspect;
mod protection;
mod rsn;
mod scan;
mod state;

mod wpa;

#[cfg(test)]
pub mod test_utils;

use {
    self::{
        event::Event,
        info::InfoReporter,
        protection::Protection,
        rsn::{get_wpa2_rsna, get_wpa3_rsna},
        scan::{DiscoveryScan, JoinScan, ScanScheduler},
        state::{ClientState, ConnectCommand},
        wpa::get_legacy_wpa_association,
    },
    crate::{
        clone_utils::clone_scan_request,
        responder::Responder,
        sink::{InfoSink, MlmeSink},
        timer::{self, TimedEvent},
        InfoStream, MlmeRequest, MlmeStream, Ssid,
    },
    anyhow::{format_err, Context as _},
    fidl_fuchsia_wlan_common as fidl_common,
    fidl_fuchsia_wlan_mlme::{self as fidl_mlme, DeviceInfo, MlmeEvent, ScanRequest},
    fidl_fuchsia_wlan_sme as fidl_sme,
    fuchsia_inspect_contrib::{inspect_insert, inspect_log, log::InspectListClosure},
    futures::channel::{mpsc, oneshot},
    log::{error, info, warn},
    std::{
        iter::{Iterator, Peekable},
        sync::Arc,
    },
    wep_deprecated,
    wlan_common::{
        self,
        bss::{BssDescription, Protection as BssProtection},
        format::MacFmt,
        hasher::WlanHasher,
        mac::MacAddr,
        RadioConfig,
    },
    wlan_inspect::wrappers::InspectWlanChan,
    wlan_rsn::auth,
};

pub use self::{
    bss::{BssInfo, ClientConfig},
    info::{InfoEvent, ScanResult},
};

// This is necessary to trick the private-in-public checker.
// A private module is not allowed to include private types in its interface,
// even though the module itself is private and will never be exported.
// As a workaround, we add another private module with public types.
mod internal {
    use {
        crate::{
            client::{event::Event, info::InfoReporter, inspect, ConnectionAttemptId},
            sink::MlmeSink,
            timer::Timer,
        },
        fidl_fuchsia_wlan_mlme::DeviceInfo,
        std::sync::Arc,
    };

    pub struct Context {
        pub device_info: Arc<DeviceInfo>,
        pub mlme_sink: MlmeSink,
        pub(crate) timer: Timer<Event>,
        pub att_id: ConnectionAttemptId,
        pub(crate) inspect: Arc<inspect::SmeTree>,
        pub(crate) info: InfoReporter,
        pub(crate) is_softmac: bool,
    }
}

use self::internal::*;

pub type TimeStream = timer::TimeStream<Event>;

pub struct ConnectConfig {
    responder: Responder<ConnectResult>,
    credential: fidl_sme::Credential,
    radio_cfg: RadioConfig,
}

// An automatically increasing sequence number that uniquely identifies a logical
// connection attempt. For example, a new connection attempt can be triggered
// by a DisassociateInd message from the MLME.
pub type ConnectionAttemptId = u64;

pub type ScanTxnId = u64;

pub struct ClientSme {
    cfg: ClientConfig,
    state: Option<ClientState>,
    scan_sched: ScanScheduler<Responder<BssDiscoveryResult>, ConnectConfig>,
    context: Context,
}

#[derive(Clone, Debug, PartialEq)]
pub enum ConnectResult {
    Success,
    Canceled,
    Failed(ConnectFailure),
}

impl<T: Into<ConnectFailure>> From<T> for ConnectResult {
    fn from(failure: T) -> Self {
        ConnectResult::Failed(failure.into())
    }
}

#[derive(Clone, Debug, PartialEq)]
pub enum ConnectFailure {
    SelectNetworkFailure(SelectNetworkFailure),
    ScanFailure(fidl_mlme::ScanResultCodes),
    JoinFailure(fidl_mlme::JoinResultCodes),
    AuthenticationFailure(fidl_mlme::AuthenticateResultCodes),
    AssociationFailure(AssociationFailure),
    EstablishRsnaFailure(EstablishRsnaFailure),
}

impl ConnectFailure {
    pub fn is_timeout(&self) -> bool {
        // Note: we don't return true for JoinFailureTimeout because it's the only join failure
        //       type, so in practice it's returned whether there's a timeout or not.
        //       For association, we don't have a failure type for timeout, so cannot deduce
        //       whether an association failure is due to timeout.
        //
        // TODO(fxbug.dev/29897): Change JOIN_FAILURE_TIMEOUT -> JOIN_FAILURE
        match self {
            ConnectFailure::AuthenticationFailure(failure) => match failure {
                fidl_mlme::AuthenticateResultCodes::AuthFailureTimeout => true,
                _ => false,
            },
            ConnectFailure::EstablishRsnaFailure(failure) => match failure {
                EstablishRsnaFailure {
                    reason: EstablishRsnaFailureReason::KeyFrameExchangeTimeout,
                    ..
                }
                | EstablishRsnaFailure {
                    reason: EstablishRsnaFailureReason::OverallTimeout, ..
                } => true,
                _ => false,
            },
            _ => false,
        }
    }

    /// Returns true if failure was likely caused by rejected
    /// credentials. In some cases, we cannot be 100% certain that
    /// credentials were rejected, but it's worth noting when we
    /// observe a failure event that was more than likely caused by
    /// rejected credentials.
    pub fn likely_due_to_credential_rejected(&self) -> bool {
        match self {
            // Assuming the correct type of credentials are given, a
            // bad password will cause a variety of errors depending
            // on the security type. All of the following cases assume
            // no frames were dropped unintentionally. For example,
            // it's possible to conflate a WPA2 bad password error
            // with a dropped frame at just the right moment since the
            // error itself is *caused by* a dropped frame.

            // For WPA1 and WPA2, the error will be
            // EstablishRsnaFailure::KeyFrameExchangeTimeout.  When
            // the authenticator receives a bad MIC (derived from the
            // password), it will silently drop the EAPOL handshake
            // frame it received.
            //
            // NOTE: The alternative possibilities for seeing an
            // EstablishRsnaFailure::KeyFrameExchangeTimeout are an
            // error in our crypto parameter parsing and crypto
            // implementation, or a lost connection with the AP.
            ConnectFailure::EstablishRsnaFailure(EstablishRsnaFailure {
                auth_method: Some(auth::MethodName::Psk),
                reason: EstablishRsnaFailureReason::KeyFrameExchangeTimeout,
            }) => true,

            // For WEP, the entire association is always handled by
            // fullmac, so the best we can do is use
            // fidl_mlme::AssociateResultCodes. The code that arises
            // when WEP fails with rejected credentials is
            // RefusedReasonUnspecified. This is a catch-all error for
            // a WEP authentication failure, but it is being
            // considered good enough for catching rejected
            // credentials for a deprecated WEP association.
            ConnectFailure::AssociationFailure(AssociationFailure {
                bss_protection: BssProtection::Wep,
                code: fidl_mlme::AssociateResultCodes::RefusedNotAuthenticated,
            }) => true,
            _ => false,
        }
    }
}

#[derive(Clone, Debug, PartialEq)]
pub enum SelectNetworkFailure {
    NoScanResultWithSsid,
    IncompatibleConnectRequest,
    InternalProtectionError,
}

impl From<SelectNetworkFailure> for ConnectFailure {
    fn from(failure: SelectNetworkFailure) -> Self {
        ConnectFailure::SelectNetworkFailure(failure)
    }
}

#[derive(Clone, Debug, PartialEq)]
pub struct AssociationFailure {
    pub bss_protection: BssProtection,
    pub code: fidl_mlme::AssociateResultCodes,
}

impl From<AssociationFailure> for ConnectFailure {
    fn from(failure: AssociationFailure) -> Self {
        ConnectFailure::AssociationFailure(failure)
    }
}

#[derive(Clone, Debug, PartialEq)]
pub struct EstablishRsnaFailure {
    pub auth_method: Option<auth::MethodName>,
    pub reason: EstablishRsnaFailureReason,
}

#[derive(Clone, Debug, PartialEq)]
pub enum EstablishRsnaFailureReason {
    StartSupplicantFailed,
    KeyFrameExchangeTimeout,
    OverallTimeout,
    InternalError,
}

impl From<EstablishRsnaFailure> for ConnectFailure {
    fn from(failure: EstablishRsnaFailure) -> Self {
        ConnectFailure::EstablishRsnaFailure(failure)
    }
}

pub type BssDiscoveryResult = Result<Vec<BssInfo>, fidl_mlme::ScanResultCodes>;

#[derive(Clone, Debug, PartialEq)]
pub struct Status {
    pub connected_to: Option<BssInfo>,
    pub connecting_to: Option<Ssid>,
}

impl ClientSme {
    pub fn new(
        cfg: ClientConfig,
        info: DeviceInfo,
        iface_tree_holder: Arc<wlan_inspect::iface_mgr::IfaceTreeHolder>,
        hasher: WlanHasher,
        is_softmac: bool,
    ) -> (Self, MlmeStream, InfoStream, TimeStream) {
        let device_info = Arc::new(info);
        let (mlme_sink, mlme_stream) = mpsc::unbounded();
        let (info_sink, info_stream) = mpsc::unbounded();
        let (mut timer, time_stream) = timer::create_timer();
        let inspect = Arc::new(inspect::SmeTree::new(&iface_tree_holder.node, hasher));
        iface_tree_holder.add_iface_subtree(inspect.clone());
        timer.schedule(event::InspectPulseCheck);

        (
            ClientSme {
                cfg,
                state: Some(ClientState::new(cfg)),
                scan_sched: ScanScheduler::new(Arc::clone(&device_info)),
                context: Context {
                    mlme_sink: MlmeSink::new(mlme_sink),
                    device_info,
                    timer,
                    att_id: 0,
                    inspect,
                    info: InfoReporter::new(InfoSink::new(info_sink)),
                    is_softmac,
                },
            },
            mlme_stream,
            info_stream,
            time_stream,
        )
    }

    pub fn on_connect_command(
        &mut self,
        req: fidl_sme::ConnectRequest,
    ) -> oneshot::Receiver<ConnectResult> {
        let (responder, receiver) = Responder::new();
        if req.ssid.len() > wlan_common::ie::SSID_MAX_LEN {
            // TODO(fxbug.dev/42081): Use a more accurate error (InvalidSsidArg) for this error.
            responder.respond(SelectNetworkFailure::NoScanResultWithSsid.into());
            return receiver;
        }
        // Cancel any ongoing connect attempt
        self.state = self.state.take().map(|state| state.cancel_ongoing_connect(&mut self.context));

        let ssid = req.ssid;

        if let Some(bss_desc) = req.bss_desc {
            let bss_desc = match BssDescription::from_fidl(*bss_desc) {
                Ok(bss_desc) => bss_desc,
                Err(e) => {
                    error!("Failed converting FIDL BssDescription: {:?}", e);
                    responder.respond(SelectNetworkFailure::IncompatibleConnectRequest.into());
                    return receiver;
                }
            };

            // We can connect directly now.
            let viable_bss = match get_protection(
                &self.context.device_info,
                &self.cfg,
                &req.credential,
                &bss_desc,
                &self.context.inspect.hasher,
            ) {
                Err(e) => {
                    warn!("{:?}", e);
                    responder.respond(SelectNetworkFailure::IncompatibleConnectRequest.into());
                    return receiver;
                }
                Ok(protection) => ViableBss { bss: &bss_desc, protection },
            };
            let protection = viable_bss.protection;
            let cmd = ConnectCommand {
                bss: Box::new(bss_desc.clone()),
                responder: Some(responder),
                protection,
                radio_cfg: RadioConfig::from_fidl(req.radio_cfg),
            };

            self.context.info.report_connect_started(ssid);
            self.context.info.report_candidate_network(bss_desc);
            self.state = self.state.take().map(|state| state.connect(cmd, &mut self.context));
            return receiver;
        }

        // We want to default to Active scan so that for routers that support WSC, we can retrieve
        // AP metadata from the probe response. However, for SoftMAC, we default to passive scan
        // because we do not have a proper active scan implementation for DFS channels.
        let scan_request = if self.context.is_softmac
            && req.deprecated_scan_type == fidl_common::ScanType::Passive
        {
            fidl_sme::ScanRequest::Passive(fidl_sme::PassiveScanRequest {})
        } else {
            fidl_sme::ScanRequest::Active(fidl_sme::ActiveScanRequest {
                ssids: vec![],
                channels: vec![],
            })
        };
        info!("SME received a connect command. Initiating a join scan with targeted SSID");
        let (canceled_token, req) = self.scan_sched.enqueue_scan_to_join(JoinScan {
            ssid: ssid.clone(),
            token: ConnectConfig {
                responder,
                credential: req.credential,
                radio_cfg: RadioConfig::from_fidl(req.radio_cfg),
            },
            scan_request,
        });
        // If the new scan replaced an existing pending JoinScan, notify the existing transaction
        if let Some(token) = canceled_token {
            report_connect_finished(
                Some(token.responder),
                &mut self.context,
                ConnectResult::Canceled,
            );
        }

        self.context.info.report_connect_started(ssid);
        self.send_scan_request(req);
        receiver
    }

    pub fn on_disconnect_command(&mut self) {
        self.state = self.state.take().map(|state| state.disconnect(&mut self.context));
        self.context.inspect.update_pulse(self.status());
    }

    pub fn on_scan_command(
        &mut self,
        scan_request: fidl_sme::ScanRequest,
    ) -> oneshot::Receiver<BssDiscoveryResult> {
        info!("SME received a scan command, initiating a discovery scan");
        let (responder, receiver) = Responder::new();
        let scan = DiscoveryScan::new(responder, scan_request);
        let req = self.scan_sched.enqueue_scan_to_discover(scan);
        self.send_scan_request(req);
        receiver
    }

    pub fn status(&self) -> Status {
        let status = self.state.as_ref().expect("expected state to be always present").status();
        if status.connecting_to.is_some() {
            status
        } else {
            // If the association machine is not connecting to a network, but the scanner
            // has a queued 'JoinScan', include the SSID we are trying to connect to
            Status {
                connecting_to: self.scan_sched.get_join_scan().map(|s| s.ssid.clone()),
                ..status
            }
        }
    }

    fn send_scan_request(&mut self, req: Option<ScanRequest>) {
        if let Some(req) = req {
            let is_join_scan = self.scan_sched.is_scanning_to_join();
            let is_connected = self.status().connected_to.is_some();
            self.context.info.report_scan_started(
                clone_scan_request(&req),
                is_join_scan,
                is_connected,
            );
            self.context.mlme_sink.send(MlmeRequest::Scan(req));
        }
    }
}

struct ViableBss<'a> {
    bss: &'a BssDescription,
    protection: Protection,
}

impl super::Station for ClientSme {
    type Event = Event;

    fn on_mlme_event(&mut self, event: MlmeEvent) {
        match event {
            MlmeEvent::OnScanResult { result } => {
                self.scan_sched.on_mlme_scan_result(result, &self.context.inspect);
            }
            MlmeEvent::OnScanEnd { end } => {
                let txn_id = end.txn_id;
                let (result, request) =
                    self.scan_sched.on_mlme_scan_end(end, &self.context.inspect);
                // Finalize stats for previous scan first before sending scan request for the next
                // one, which would also start stats collection for new scan scan.
                self.context.info.report_scan_ended(txn_id, &result);
                self.send_scan_request(request);
                match result {
                    scan::ScanResult::None => (),
                    scan::ScanResult::JoinScanFinished { token, result: Ok(bss_list) }
                        if bss_list.is_empty() =>
                    {
                        info!("Scan results:");
                        info!("  No Results");
                        let error_msg_str = "no matching BSS found";
                        error!("{}", error_msg_str);
                        report_connect_finished(
                            Some(token.responder),
                            &mut self.context,
                            SelectNetworkFailure::NoScanResultWithSsid.into(),
                        );
                        inspect_log_join_scan(
                            &mut self.context,
                            &bss_list,
                            None,
                            Some(error_msg_str.to_string()),
                        );
                    }
                    scan::ScanResult::JoinScanFinished { token, result: Ok(bss_list) } => {
                        info!("Scan results:");
                        for bss in &bss_list {
                            info!("  {}", bss.to_string(&self.context.inspect.hasher));
                        }

                        let mut compatible_bss_iter =
                            filter_to_compatible_bss(&bss_list, &self.cfg, &token.credential);

                        if compatible_bss_iter.peek() == None {
                            let error_msg_str = "incompatible connect request for scan results";
                            error!("{}", error_msg_str);
                            report_connect_finished(
                                Some(token.responder),
                                &mut self.context,
                                SelectNetworkFailure::IncompatibleConnectRequest.into(),
                            );
                            inspect_log_join_scan(
                                &mut self.context,
                                &bss_list,
                                None,
                                Some(error_msg_str.to_string()),
                            );
                        } else {
                            match filter_to_viable_bss(
                                compatible_bss_iter,
                                &self.context.device_info,
                                &self.cfg,
                                &token.credential,
                                &self.context.inspect.hasher,
                            )
                            .max_by_key(|compatible_bss| compatible_bss.bss.candidacy())
                            {
                                None => {
                                    // Report a SelectNetworkFailure if there are no compatible results.
                                    let error_msg_str = "internal protection error";
                                    error!("{}", error_msg_str);
                                    report_connect_finished(
                                        Some(token.responder),
                                        &mut self.context,
                                        SelectNetworkFailure::InternalProtectionError.into(),
                                    );
                                    inspect_log_join_scan(
                                        &mut self.context,
                                        &bss_list,
                                        None,
                                        Some(error_msg_str.to_string()),
                                    );
                                }
                                Some(best_compatible_bss) => {
                                    let best_bss = best_compatible_bss.bss;
                                    let best_bssid = best_bss.bssid;
                                    let best_bss_protection = best_compatible_bss.protection;

                                    info!("Attempting to connect to:");
                                    info!("  {}", best_bss.to_string(&self.context.inspect.hasher));

                                    self.context.info.report_candidate_network(best_bss.clone());
                                    let cmd = ConnectCommand {
                                        bss: Box::new(best_bss.clone()),
                                        responder: Some(token.responder),
                                        protection: best_bss_protection,
                                        radio_cfg: token.radio_cfg,
                                    };

                                    self.state = self
                                        .state
                                        .take()
                                        .map(|state| state.connect(cmd, &mut self.context));

                                    inspect_log_join_scan(
                                        &mut self.context,
                                        &bss_list,
                                        Some(best_bssid),
                                        Some("attempt to connect".to_string()),
                                    );
                                }
                            }
                        }
                    }
                    scan::ScanResult::JoinScanFinished { token, result: Err(e) } => {
                        inspect_log!(
                            self.context.inspect.join_scan_events.lock(),
                            result: format!("scan failure: {:?}", e),
                        );
                        error!("cannot join network because scan failed: {:?}", e);
                        let result = ConnectFailure::ScanFailure(e).into();
                        report_connect_finished(Some(token.responder), &mut self.context, result);
                    }
                    scan::ScanResult::DiscoveryFinished { tokens, result } => {
                        let result = result.map(|bss_list| {
                            bss_list
                                .iter()
                                .map(|bss| self.cfg.convert_bss_description(&bss, None))
                                .collect()
                        });
                        for responder in tokens {
                            responder.respond(result.clone());
                        }
                    }
                }
            }
            other => {
                self.state =
                    self.state.take().map(|state| state.on_mlme_event(other, &mut self.context));
            }
        };

        self.context.inspect.update_pulse(self.status());
    }

    fn on_timeout(&mut self, timed_event: TimedEvent<Event>) {
        self.state = self.state.take().map(|state| match timed_event.event {
            event @ Event::EstablishingRsnaTimeout(..)
            | event @ Event::KeyFrameExchangeTimeout(..)
            | event @ Event::ConnectionPing(..)
            | event @ Event::SaeTimeout(..) => {
                state.handle_timeout(timed_event.id, event, &mut self.context)
            }
            Event::InspectPulseCheck(..) => {
                self.context.timer.schedule(event::InspectPulseCheck);
                state
            }
        });

        // Because `self.status()` relies on the value of `self.state` to be present, we cannot
        // retrieve it and update pulse node inside the closure above.
        self.context.inspect.update_pulse(self.status());
    }
}

fn inspect_log_join_scan(
    ctx: &mut Context,
    bss_list: &[BssDescription],
    candidate_bssid: Option<MacAddr>,
    result_msg: Option<String>,
) {
    let inspect_bss = InspectListClosure(&bss_list, |node_writer, key, bss| {
        inspect_insert!(node_writer, var key: {
            bssid: bss.bssid.to_mac_str(),
            bssid_hash: ctx.inspect.hasher.hash_mac_addr(&bss.bssid),
            ssid: String::from_utf8_lossy(bss.ssid()).as_ref(),
            ssid_hash: ctx.inspect.hasher.hash(bss.ssid()),
            channel: InspectWlanChan(&bss.chan),
            rssi_dbm: bss.rssi_dbm,
        });
    });
    let hasher = &ctx.inspect.hasher;
    inspect_log!(ctx.inspect.join_scan_events.lock(), {
        bss_list: inspect_bss,
        candidate_bss: {
            bssid?: candidate_bssid.as_ref().map(|bssid| bssid.to_mac_str()),
            bssid_hash?: candidate_bssid.map(|bssid| hasher.hash_mac_addr(&bssid)),
        },
        result?: result_msg,
    });
}

fn report_connect_finished(
    responder: Option<Responder<ConnectResult>>,
    context: &mut Context,
    result: ConnectResult,
) {
    if let Some(responder) = responder {
        responder.respond(result.clone());
        context.info.report_connect_finished(result);
    }
}

/// Filter out any BssDescription in `bss_list` that is not compatible with
/// 'device_info`, `client_config`, or `credential`. A BssDescription is
/// not compatible if `client_config.is_bss_compatible()` returns `false`
/// or if `protection()` returns a `Result::Err`.
fn filter_to_compatible_bss<'a>(
    bss_list: &'a Vec<BssDescription>,
    client_config: &'a ClientConfig,
    credential: &'a fidl_sme::Credential,
) -> Peekable<impl Iterator<Item = &'a BssDescription>> {
    bss_list
        .iter()
        .filter_map(move |bss| {
            if !client_config.is_bss_compatible(bss) {
                return None;
            }
            if let fidl_sme::Credential::None(_) = credential {
                if bss.is_protected() {
                    return None;
                }
            } else {
                if !bss.is_protected() {
                    return None;
                }
            }
            Some(bss)
        })
        .peekable()
}

fn filter_to_viable_bss<'a>(
    bss_iter: Peekable<impl Iterator<Item = &'a BssDescription>>,
    device_info: &'a DeviceInfo,
    client_config: &'a ClientConfig,
    credential: &'a fidl_sme::Credential,
    hasher: &'a WlanHasher,
) -> impl Iterator<Item = ViableBss<'a>> {
    bss_iter.filter_map(move |bss| {
        match get_protection(device_info, client_config, credential, bss, hasher) {
            Err(e) => {
                warn!("{:?}", e);
                None
            }
            Ok(protection) => Some(ViableBss { bss, protection }),
        }
    })
}

fn get_protection(
    device_info: &DeviceInfo,
    client_config: &ClientConfig,
    credential: &fidl_sme::Credential,
    bss: &BssDescription,
    hasher: &WlanHasher,
) -> Result<Protection, anyhow::Error> {
    let ssid_hash = hasher.hash_ssid(bss.ssid());
    let bssid_hash = hasher.hash_mac_addr(&bss.bssid);

    match bss.protection() {
        wlan_common::bss::Protection::Open => match credential {
            fidl_sme::Credential::None(_) => Ok(Protection::Open),
            _ => Err(format_err!(
                "Open network {} ({}) not compatible with credentials.",
                ssid_hash,
                bssid_hash
            )),
        },
        wlan_common::bss::Protection::Wep => match credential {
            fidl_sme::Credential::Password(pwd) => {
                wep_deprecated::derive_key(&pwd[..]).map(Protection::Wep).with_context(|| {
                    format!(
                        "WEP network {} ({}) protection cannot be retrieved with credential {:?}",
                        ssid_hash, bssid_hash, credential,
                    )
                })
            }
            _ => Err(format_err!(
                "WEP network {} ({}) not compatible with credential {:?}",
                ssid_hash,
                bssid_hash,
                credential
            )),
        },
        wlan_common::bss::Protection::Wpa1 => {
            get_legacy_wpa_association(device_info, credential, bss).with_context(|| {
                format!(
                    "WPA1 network {} ({}) protection cannot be retrieved with credential {:?}",
                    ssid_hash, bssid_hash, credential
                )
            })
        }
        // If WPA3 is supported, we will only treat Wpa2/Wpa3 transition APs as WPA3.
        wlan_common::bss::Protection::Wpa3Personal
        | wlan_common::bss::Protection::Wpa2Wpa3Personal
            if client_config.wpa3_supported =>
        {
            get_wpa3_rsna(device_info, credential, bss).with_context(|| {
                format!(
                    "WPA3 network {} ({}) protection cannot be retrieved with credential {:?}:",
                    ssid_hash, bssid_hash, credential
                )
            })
        }
        wlan_common::bss::Protection::Wpa2Legacy
        | wlan_common::bss::Protection::Wpa1Wpa2Personal
        | wlan_common::bss::Protection::Wpa2Personal
        | wlan_common::bss::Protection::Wpa2Wpa3Personal => {
            get_wpa2_rsna(device_info, credential, bss).with_context(|| {
                format!(
                    "WPA2 network {} ({}) protection cannot be retrieved with credential {:?}",
                    ssid_hash, bssid_hash, credential
                )
            })
        }
        wlan_common::bss::Protection::Unknown => {
            Err(format_err!("Unknown protection type for {} ({})", ssid_hash, bssid_hash,))
        }
        _ => Err(format_err!("Unsupported protection type for {} ({})", ssid_hash, bssid_hash)),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::Config as SmeConfig;
    use fidl_fuchsia_wlan_internal as fidl_internal;
    use fidl_fuchsia_wlan_mlme as fidl_mlme;
    use fuchsia_inspect as finspect;
    use wlan_common::{assert_variant, fake_bss, fake_fidl_bss, ie::rsn::akm, RadioConfig};

    use super::test_utils::{
        create_assoc_conf, create_auth_conf, create_join_conf, expect_stream_empty,
    };

    use crate::test_utils;
    use crate::Station;

    const CLIENT_ADDR: [u8; 6] = [0x7A, 0xE7, 0x76, 0xD9, 0xF2, 0x67];
    const DUMMY_HASH_KEY: [u8; 8] = [88, 77, 66, 55, 44, 33, 22, 11];

    fn report_fake_scan_result(sme: &mut ClientSme, bss: fidl_internal::BssDescription) {
        sme.on_mlme_event(MlmeEvent::OnScanResult {
            result: fidl_mlme::ScanResult { txn_id: 1, bss },
        });
        sme.on_mlme_event(MlmeEvent::OnScanEnd {
            end: fidl_mlme::ScanEnd { txn_id: 1, code: fidl_mlme::ScanResultCodes::Success },
        });
    }

    #[test]
    fn test_detection_of_rejected_wpa1_or_wpa2_credentials() {
        let failure = ConnectFailure::EstablishRsnaFailure(EstablishRsnaFailure {
            auth_method: Some(auth::MethodName::Psk),
            reason: EstablishRsnaFailureReason::KeyFrameExchangeTimeout,
        });
        assert!(failure.likely_due_to_credential_rejected());
    }

    #[test]
    fn test_detection_of_rejected_wep_credentials() {
        let failure = ConnectFailure::AssociationFailure(AssociationFailure {
            bss_protection: BssProtection::Wep,
            code: fidl_mlme::AssociateResultCodes::RefusedNotAuthenticated,
        });
        assert!(failure.likely_due_to_credential_rejected());
    }

    #[test]
    fn test_no_detection_of_rejected_wpa1_or_wpa2_credentials() {
        let failure = ConnectFailure::ScanFailure(fidl_mlme::ScanResultCodes::InternalError);
        assert!(!failure.likely_due_to_credential_rejected());

        let failure = ConnectFailure::AssociationFailure(AssociationFailure {
            bss_protection: BssProtection::Wpa2Personal,
            code: fidl_mlme::AssociateResultCodes::RefusedNotAuthenticated,
        });
        assert!(!failure.likely_due_to_credential_rejected());
    }

    #[test]
    fn test_get_protection() {
        let dev_info = test_utils::fake_device_info(CLIENT_ADDR);
        let client_config = Default::default();
        let hasher = WlanHasher::new(DUMMY_HASH_KEY);

        // Open network without credentials:
        let credential = fidl_sme::Credential::None(fidl_sme::Empty);
        let bss = fake_bss!(Open);
        let protection = get_protection(&dev_info, &client_config, &credential, &bss, &hasher);
        assert_variant!(protection, Ok(Protection::Open));

        // Open network with credentials:
        let credential = fidl_sme::Credential::Password(b"somepass".to_vec());
        let bss = fake_bss!(Open);
        get_protection(&dev_info, &client_config, &credential, &bss, &hasher)
            .expect_err("unprotected network cannot use password");

        // RSN with user entered password:
        let credential = fidl_sme::Credential::Password(b"somepass".to_vec());
        let bss = fake_bss!(Wpa2);
        let protection = get_protection(&dev_info, &client_config, &credential, &bss, &hasher);
        assert_variant!(protection, Ok(Protection::Rsna(_)));

        // RSN with user entered PSK:
        let credential = fidl_sme::Credential::Psk(vec![0xAC; 32]);
        let bss = fake_bss!(Wpa2);
        let protection = get_protection(&dev_info, &client_config, &credential, &bss, &hasher);
        assert_variant!(protection, Ok(Protection::Rsna(_)));

        // RSN without credentials:
        let credential = fidl_sme::Credential::None(fidl_sme::Empty);
        let bss = fake_bss!(Wpa2);
        get_protection(&dev_info, &client_config, &credential, &bss, &hasher)
            .expect_err("protected network requires password");
    }

    #[test]
    fn test_get_protection_wep() {
        let dev_info = test_utils::fake_device_info(CLIENT_ADDR);
        let client_config = ClientConfig::from_config(SmeConfig::default().with_wep(), false);
        let hasher = WlanHasher::new(DUMMY_HASH_KEY);

        // WEP-40 with credentials:
        let credential = fidl_sme::Credential::Password(b"wep40".to_vec());
        let bss = fake_bss!(Wep);
        let protection = get_protection(&dev_info, &client_config, &credential, &bss, &hasher);
        assert_variant!(protection, Ok(Protection::Wep(_)));

        // WEP-104 with credentials:
        let credential = fidl_sme::Credential::Password(b"superinsecure".to_vec());
        let bss = fake_bss!(Wep);
        let protection = get_protection(&dev_info, &client_config, &credential, &bss, &hasher);
        assert_variant!(protection, Ok(Protection::Wep(_)));

        // WEP without credentials:
        let credential = fidl_sme::Credential::None(fidl_sme::Empty);
        let bss = fake_bss!(Wep);
        get_protection(&dev_info, &client_config, &credential, &bss, &hasher)
            .expect_err("WEP network not supported");

        // WEP with invalid credentials:
        let credential = fidl_sme::Credential::Password(b"wep".to_vec());
        let bss = fake_bss!(Wep);
        get_protection(&dev_info, &client_config, &credential, &bss, &hasher)
            .expect_err("expected error for invalid WEP credentials");
    }

    #[test]
    fn test_get_protection_sae() {
        let dev_info = test_utils::fake_device_info(CLIENT_ADDR);
        let mut client_config = ClientConfig::from_config(SmeConfig::default(), true);
        let hasher = WlanHasher::new(DUMMY_HASH_KEY);

        // WPA3, supported
        let credential = fidl_sme::Credential::Password(b"somepass".to_vec());
        let bss = fake_bss!(Wpa3);
        let protection = get_protection(&dev_info, &client_config, &credential, &bss, &hasher);
        assert_variant!(protection, Ok(Protection::Rsna(rsna)) => {
            assert_eq!(rsna.negotiated_protection.akm.suite_type, akm::SAE)
        });

        // WPA2/3, supported
        let credential = fidl_sme::Credential::Password(b"somepass".to_vec());
        let bss = fake_bss!(Wpa2Wpa3);
        let protection = get_protection(&dev_info, &client_config, &credential, &bss, &hasher);
        assert_variant!(protection, Ok(Protection::Rsna(rsna)) => {
            assert_eq!(rsna.negotiated_protection.akm.suite_type, akm::SAE)
        });

        client_config.wpa3_supported = false;

        // WPA3, unsupported
        let credential = fidl_sme::Credential::Password(b"somepass".to_vec());
        let bss = fake_bss!(Wpa3);
        let protection = get_protection(&dev_info, &client_config, &credential, &bss, &hasher);
        assert_variant!(protection, Err(_));

        // WPA2/3, WPA3 unsupported, downgrade to WPA2
        let credential = fidl_sme::Credential::Password(b"somepass".to_vec());
        let bss = fake_bss!(Wpa2Wpa3);
        let protection = get_protection(&dev_info, &client_config, &credential, &bss, &hasher);
        assert_variant!(protection, Ok(Protection::Rsna(rsna)) => {
            assert_eq!(rsna.negotiated_protection.akm.suite_type, akm::PSK)
        });
    }

    #[test]
    fn status_connecting_to() {
        let (mut sme, _mlme_stream, _info_stream, _time_stream) = create_sme();
        assert_eq!(Status { connected_to: None, connecting_to: None }, sme.status());

        // Issue a connect command and expect the status to change appropriately.
        // We also check that the association machine state is still disconnected
        // to make sure that the status comes from the scanner.
        let credential = fidl_sme::Credential::None(fidl_sme::Empty);
        let _recv = sme.on_connect_command(connect_req(b"foo".to_vec(), credential));
        assert_eq!(None, sme.state.as_ref().unwrap().status().connecting_to);
        assert_eq!(
            Status { connected_to: None, connecting_to: Some(b"foo".to_vec()) },
            sme.status()
        );

        // Push a fake scan result into SME. We should still be connecting to "foo",
        // but the status should now come from the state machine and not from the scanner.
        report_fake_scan_result(&mut sme, fake_fidl_bss!(Open, ssid: b"foo".to_vec()));
        assert_eq!(Some(b"foo".to_vec()), sme.state.as_ref().unwrap().status().connecting_to);
        assert_eq!(
            Status { connected_to: None, connecting_to: Some(b"foo".to_vec()) },
            sme.status()
        );

        // As soon as connect command is issued for "bar", the status changes immediately
        let credential = fidl_sme::Credential::None(fidl_sme::Empty);
        let _recv2 = sme.on_connect_command(connect_req(b"bar".to_vec(), credential));
        assert_eq!(
            Status { connected_to: None, connecting_to: Some(b"bar".to_vec()) },
            sme.status()
        );
    }

    #[test]
    fn status_connecting_to_open_network_with_duplicate_ssid_in_scan_result() {
        let (mut sme, mut mlme_stream, _info_stream, _time_stream) = create_sme();
        assert_eq!(Status { connected_to: None, connecting_to: None }, sme.status());

        let credential = fidl_sme::Credential::None(fidl_sme::Empty);
        let _recv = sme.on_connect_command(connect_req(b"foo".to_vec(), credential));
        assert_eq!(None, sme.state.as_ref().unwrap().status().connecting_to);
        assert_eq!(
            Status { connected_to: None, connecting_to: Some(b"foo".to_vec()) },
            sme.status()
        );

        assert_variant!(mlme_stream.try_next(), Ok(Some(MlmeRequest::Scan(..))));

        // Push a fake scan result into SME. We should still be connecting to "foo",
        // but the status should now come from the state machine and not from the scanner.
        sme.on_mlme_event(MlmeEvent::OnScanResult {
            result: fidl_mlme::ScanResult {
                txn_id: 1,
                bss: fidl_internal::BssDescription {
                    bssid: [1; 6],
                    rssi_dbm: -100,
                    ..fake_fidl_bss!(Open, ssid: b"foo".to_vec())
                },
            },
        });
        sme.on_mlme_event(MlmeEvent::OnScanResult {
            result: fidl_mlme::ScanResult {
                txn_id: 1,
                bss: fidl_internal::BssDescription {
                    bssid: [2; 6],
                    rssi_dbm: -1,
                    ..fake_fidl_bss!(Wpa2, ssid: b"foo".to_vec())
                },
            },
        });
        sme.on_mlme_event(MlmeEvent::OnScanEnd {
            end: fidl_mlme::ScanEnd { txn_id: 1, code: fidl_mlme::ScanResultCodes::Success },
        });
        // Despite the fact that the stronger signal strength network has the wrong security
        // type, we should try to connect to the open network anyway.
        assert_eq!(Some(b"foo".to_vec()), sme.state.as_ref().unwrap().status().connecting_to);
        assert_eq!(
            Status { connected_to: None, connecting_to: Some(b"foo".to_vec()) },
            sme.status()
        );

        assert_variant!(mlme_stream.try_next(), Ok(Some(MlmeRequest::Join(..))));
    }

    #[test]
    fn status_connecting_to_protected_network_with_duplicate_ssid_in_scan_result() {
        let (mut sme, mut mlme_stream, _info_stream, _time_stream) = create_sme();
        assert_eq!(Status { connected_to: None, connecting_to: None }, sme.status());

        let credential = fidl_sme::Credential::Password(b"somepass".to_vec());
        let _recv = sme.on_connect_command(connect_req(b"foo".to_vec(), credential));
        assert_eq!(None, sme.state.as_ref().unwrap().status().connecting_to);
        assert_eq!(
            Status { connected_to: None, connecting_to: Some(b"foo".to_vec()) },
            sme.status()
        );

        assert_variant!(mlme_stream.try_next(), Ok(Some(MlmeRequest::Scan(..))));

        // Push fake scan results into SME with the same SSIDs and different security types.
        sme.on_mlme_event(MlmeEvent::OnScanResult {
            result: fidl_mlme::ScanResult {
                txn_id: 1,
                bss: fidl_internal::BssDescription {
                    bssid: [1; 6],
                    rssi_dbm: -100,
                    ..fake_fidl_bss!(Wpa2, ssid: b"foo".to_vec())
                },
            },
        });
        sme.on_mlme_event(MlmeEvent::OnScanResult {
            result: fidl_mlme::ScanResult {
                txn_id: 1,
                bss: fidl_internal::BssDescription {
                    bssid: [2; 6],
                    rssi_dbm: -1,
                    ..fake_fidl_bss!(Open, ssid: b"foo".to_vec())
                },
            },
        });
        sme.on_mlme_event(MlmeEvent::OnScanEnd {
            end: fidl_mlme::ScanEnd { txn_id: 1, code: fidl_mlme::ScanResultCodes::Success },
        });
        // Despite the fact that the stronger signal strength network has the wrong security
        // type, we should try to connect to the Wpa2 network anyway.
        assert_eq!(Some(b"foo".to_vec()), sme.state.as_ref().unwrap().status().connecting_to);
        assert_eq!(
            Status { connected_to: None, connecting_to: Some(b"foo".to_vec()) },
            sme.status()
        );

        assert_variant!(mlme_stream.try_next(), Ok(Some(MlmeRequest::Join(..))));
    }

    #[test]
    fn connecting_to_wep_network_supported() {
        let inspector = finspect::Inspector::new();
        let sme_root_node = inspector.root().create_child("sme");
        let (mut sme, mut mlme_stream, _info_stream, _time_stream) = ClientSme::new(
            ClientConfig::from_config(SmeConfig::default().with_wep(), false),
            test_utils::fake_device_info(CLIENT_ADDR),
            Arc::new(wlan_inspect::iface_mgr::IfaceTreeHolder::new(sme_root_node)),
            WlanHasher::new(DUMMY_HASH_KEY),
            true, // is_softmac
        );
        assert_eq!(Status { connected_to: None, connecting_to: None }, sme.status());

        // Issue a connect command and verify that connecting_to status is changed for upper
        // layer (but not underlying state machine) and a scan request is sent to MLME.
        let credential = fidl_sme::Credential::Password(b"wep40".to_vec());
        let req = connect_req(b"foo".to_vec(), credential);
        let _recv = sme.on_connect_command(req);
        assert_eq!(None, sme.state.as_ref().unwrap().status().connecting_to);
        assert_eq!(
            Status { connected_to: None, connecting_to: Some(b"foo".to_vec()) },
            sme.status()
        );

        assert_variant!(mlme_stream.try_next(), Ok(Some(MlmeRequest::Scan(..))));

        // Simulate scan end and verify that underlying state machine's status is changed,
        // and a join request is sent to MLME.
        report_fake_scan_result(&mut sme, fake_fidl_bss!(Wep, ssid: b"foo".to_vec()));
        assert_eq!(Some(b"foo".to_vec()), sme.state.as_ref().unwrap().status().connecting_to);
        assert_eq!(
            Status { connected_to: None, connecting_to: Some(b"foo".to_vec()) },
            sme.status()
        );

        assert_variant!(mlme_stream.try_next(), Ok(Some(MlmeRequest::Join(..))));
    }

    #[test]
    fn connecting_to_wep_network_unsupported() {
        let (mut sme, mut mlme_stream, _info_stream, _time_stream) = create_sme();
        assert_eq!(Status { connected_to: None, connecting_to: None }, sme.status());

        // Issue a connect command and verify that connecting_to status is changed for upper
        // layer (but not underlying state machine) and a scan request is sent to MLME.
        let credential = fidl_sme::Credential::Password(b"wep40".to_vec());
        let req = connect_req(b"foo".to_vec(), credential);
        let mut connect_fut = sme.on_connect_command(req);
        assert_eq!(None, sme.state.as_ref().unwrap().status().connecting_to);
        assert_eq!(
            Status { connected_to: None, connecting_to: Some(b"foo".to_vec()) },
            sme.status()
        );

        assert_variant!(mlme_stream.try_next(), Ok(Some(MlmeRequest::Scan(..))));

        // Simulate scan end and verify that underlying state machine's status is not changed,
        report_fake_scan_result(&mut sme, fake_fidl_bss!(Wep, ssid: b"foo".to_vec()));
        assert_eq!(None, sme.state.as_ref().unwrap().status().connecting_to);
        assert_connect_result_failed(&mut connect_fut);
    }

    #[test]
    fn connecting_password_supplied_for_protected_network() {
        let (mut sme, mut mlme_stream, _info_stream, _time_stream) = create_sme();
        assert_eq!(Status { connected_to: None, connecting_to: None }, sme.status());

        // Issue a connect command and verify that connecting_to status is changed for upper
        // layer (but not underlying state machine) and a scan request is sent to MLME.
        let credential = fidl_sme::Credential::Password(b"somepass".to_vec());
        let req = connect_req(b"foo".to_vec(), credential);
        let _recv = sme.on_connect_command(req);
        assert_eq!(None, sme.state.as_ref().unwrap().status().connecting_to);
        assert_eq!(
            Status { connected_to: None, connecting_to: Some(b"foo".to_vec()) },
            sme.status()
        );

        assert_variant!(mlme_stream.try_next(), Ok(Some(MlmeRequest::Scan(..))));

        // Simulate scan end and verify that underlying state machine's status is changed,
        // and a join request is sent to MLME.
        report_fake_scan_result(&mut sme, fake_fidl_bss!(Wpa2, ssid: b"foo".to_vec()));
        assert_eq!(Some(b"foo".to_vec()), sme.state.as_ref().unwrap().status().connecting_to);
        assert_eq!(
            Status { connected_to: None, connecting_to: Some(b"foo".to_vec()) },
            sme.status()
        );

        assert_variant!(mlme_stream.try_next(), Ok(Some(MlmeRequest::Join(..))));
    }

    #[test]
    fn connecting_psk_supplied_for_protected_network() {
        let (mut sme, mut mlme_stream, _info_stream, _time_stream) = create_sme();
        assert_eq!(Status { connected_to: None, connecting_to: None }, sme.status());

        // Issue a connect command and verify that connecting_to status is changed for upper
        // layer (but not underlying state machine) and a scan request is sent to MLME.

        // IEEE Std 802.11-2016, J.4.2, Test case 1
        // PSK for SSID "IEEE" and password "password".
        #[rustfmt::skip]
        let psk = vec![
            0xf4, 0x2c, 0x6f, 0xc5, 0x2d, 0xf0, 0xeb, 0xef,
            0x9e, 0xbb, 0x4b, 0x90, 0xb3, 0x8a, 0x5f, 0x90,
            0x2e, 0x83, 0xfe, 0x1b, 0x13, 0x5a, 0x70, 0xe2,
            0x3a, 0xed, 0x76, 0x2e, 0x97, 0x10, 0xa1, 0x2e,
        ];
        let credential = fidl_sme::Credential::Psk(psk);
        let req = connect_req(b"IEEE".to_vec(), credential);
        let _recv = sme.on_connect_command(req);
        assert_eq!(None, sme.state.as_ref().unwrap().status().connecting_to);
        assert_eq!(
            Status { connected_to: None, connecting_to: Some(b"IEEE".to_vec()) },
            sme.status()
        );

        assert_variant!(mlme_stream.try_next(), Ok(Some(MlmeRequest::Scan(..))));

        // Simulate scan end and verify that underlying state machine's status is changed,
        // and a join request is sent to MLME.
        report_fake_scan_result(&mut sme, fake_fidl_bss!(Wpa2, ssid: b"IEEE".to_vec()));
        assert_eq!(Some(b"IEEE".to_vec()), sme.state.as_ref().unwrap().status().connecting_to);
        assert_eq!(
            Status { connected_to: None, connecting_to: Some(b"IEEE".to_vec()) },
            sme.status()
        );

        assert_variant!(mlme_stream.try_next(), Ok(Some(MlmeRequest::Join(..))));
    }

    #[test]
    fn connecting_password_supplied_for_unprotected_network() {
        let (mut sme, mut mlme_stream, _info_stream, _time_stream) = create_sme();
        assert_eq!(Status { connected_to: None, connecting_to: None }, sme.status());

        let credential = fidl_sme::Credential::Password(b"somepass".to_vec());
        let req = connect_req(b"foo".to_vec(), credential);
        let mut connect_fut = sme.on_connect_command(req);
        assert_eq!(None, sme.state.as_ref().unwrap().status().connecting_to);
        assert_eq!(
            Status { connected_to: None, connecting_to: Some(b"foo".to_vec()) },
            sme.status()
        );

        // Push a fake scan result into SME. We should not attempt to connect
        // because a password was supplied for unprotected network. So both the
        // SME client and underlying state machine should report not connecting
        // anymore.
        report_fake_scan_result(&mut sme, fake_fidl_bss!(Open, ssid: b"foo".to_vec()));
        assert_eq!(None, sme.state.as_ref().unwrap().status().connecting_to);
        assert_eq!(Status { connected_to: None, connecting_to: None }, sme.status());

        // No join request should be sent to MLME
        assert_no_join(&mut mlme_stream);

        // User should get a message that connection failed
        assert_variant!(
            connect_fut.try_recv(),
            Ok(Some(ConnectResult::Failed(ConnectFailure::SelectNetworkFailure(
                SelectNetworkFailure::IncompatibleConnectRequest,
            ),),),)
        );
    }

    #[test]
    fn connecting_psk_supplied_for_unprotected_network() {
        let (mut sme, mut mlme_stream, _info_stream, _time_stream) = create_sme();
        assert_eq!(Status { connected_to: None, connecting_to: None }, sme.status());

        let credential = fidl_sme::Credential::Psk(b"somepass".to_vec());
        let req = connect_req(b"foo".to_vec(), credential);
        let mut connect_fut = sme.on_connect_command(req);
        assert_eq!(None, sme.state.as_ref().unwrap().status().connecting_to);
        assert_eq!(
            Status { connected_to: None, connecting_to: Some(b"foo".to_vec()) },
            sme.status()
        );

        // Push a fake scan result into SME. We should not attempt to connect
        // because a password was supplied for unprotected network. So both the
        // SME client and underlying state machine should report not connecting
        // anymore.
        report_fake_scan_result(&mut sme, fake_fidl_bss!(Open, ssid: b"foo".to_vec()));
        assert_eq!(None, sme.state.as_ref().unwrap().status().connecting_to);
        assert_eq!(Status { connected_to: None, connecting_to: None }, sme.status());

        // No join request should be sent to MLME
        assert_no_join(&mut mlme_stream);

        // User should get a message that connection failed
        assert_variant!(
            connect_fut.try_recv(),
            Ok(Some(ConnectResult::Failed(ConnectFailure::SelectNetworkFailure(
                SelectNetworkFailure::IncompatibleConnectRequest,
            ),),),)
        );
    }

    #[test]
    fn connecting_no_password_supplied_for_protected_network() {
        let (mut sme, mut mlme_stream, _info_stream, _time_stream) = create_sme();
        assert_eq!(Status { connected_to: None, connecting_to: None }, sme.status());

        let credential = fidl_sme::Credential::None(fidl_sme::Empty);
        let req = connect_req(b"foo".to_vec(), credential);
        let mut connect_fut = sme.on_connect_command(req);
        assert_eq!(None, sme.state.as_ref().unwrap().status().connecting_to);
        assert_eq!(
            Status { connected_to: None, connecting_to: Some(b"foo".to_vec()) },
            sme.status()
        );

        // Push a fake scan result into SME. We should not attempt to connect
        // because no password was supplied for a protected network.
        report_fake_scan_result(&mut sme, fake_fidl_bss!(Wpa2, ssid: b"foo".to_vec()));
        assert_eq!(None, sme.state.as_ref().unwrap().status().connecting_to);
        assert_eq!(Status { connected_to: None, connecting_to: None }, sme.status());

        // No join request should be sent to MLME
        assert_no_join(&mut mlme_stream);

        // User should get a message that connection failed
        assert_variant!(
            connect_fut.try_recv(),
            Ok(Some(ConnectResult::Failed(ConnectFailure::SelectNetworkFailure(
                SelectNetworkFailure::IncompatibleConnectRequest,
            ),),),)
        );
    }

    #[test]
    fn connecting_bypass_join_scan_open() {
        let (mut sme, mut mlme_stream, _info_stream, _time_stream) = create_sme();
        assert_eq!(Status { connected_to: None, connecting_to: None }, sme.status());

        let credential = fidl_sme::Credential::None(fidl_sme::Empty);
        let bss_desc = fake_fidl_bss!(Open, ssid: b"bssname".to_vec());
        let req = connect_req_with_desc(b"bssname".to_vec(), bss_desc, credential);
        let mut connect_fut = sme.on_connect_command(req);

        assert_eq!(
            Status { connected_to: None, connecting_to: Some(b"bssname".to_vec()) },
            sme.status()
        );
        assert_variant!(mlme_stream.try_next(), Ok(Some(MlmeRequest::Join(..))));
        assert_variant!(connect_fut.try_recv(), Ok(None));
    }

    #[test]
    fn connecting_bypass_join_scan_protected() {
        let (mut sme, mut mlme_stream, _info_stream, _time_stream) = create_sme();
        assert_eq!(Status { connected_to: None, connecting_to: None }, sme.status());

        let credential = fidl_sme::Credential::Password(b"somepass".to_vec());
        let bss_desc = fake_fidl_bss!(Wpa2, ssid: b"bssname".to_vec());
        let req = connect_req_with_desc(b"bssname".to_vec(), bss_desc, credential);
        let mut connect_fut = sme.on_connect_command(req);

        assert_eq!(
            Status { connected_to: None, connecting_to: Some(b"bssname".to_vec()) },
            sme.status()
        );
        assert_variant!(mlme_stream.try_next(), Ok(Some(MlmeRequest::Join(..))));
        assert_variant!(connect_fut.try_recv(), Ok(None));
    }

    #[test]
    fn connecting_bypass_join_scan_mismatched_credential() {
        let (mut sme, mut mlme_stream, _info_stream, _time_stream) = create_sme();
        assert_eq!(Status { connected_to: None, connecting_to: None }, sme.status());

        let credential = fidl_sme::Credential::None(fidl_sme::Empty);
        let bss_desc = fake_fidl_bss!(Wpa2, ssid: b"bssname".to_vec());
        let req = connect_req_with_desc(b"bssname".to_vec(), bss_desc, credential);
        let mut connect_fut = sme.on_connect_command(req);

        assert_eq!(Status { connected_to: None, connecting_to: None }, sme.status());
        assert_no_join(&mut mlme_stream);

        // User should get a message that connection failed
        assert_variant!(
            connect_fut.try_recv(),
            Ok(Some(ConnectResult::Failed(ConnectFailure::SelectNetworkFailure(
                SelectNetworkFailure::IncompatibleConnectRequest,
            ),),),)
        );
    }

    #[test]
    fn connecting_bypass_join_scan_unsupported_bss() {
        let (mut sme, mut mlme_stream, _info_stream, _time_stream) = create_sme();
        assert_eq!(Status { connected_to: None, connecting_to: None }, sme.status());

        let credential = fidl_sme::Credential::Password(b"somepass".to_vec());
        let bss_desc = fake_fidl_bss!(Wpa3Enterprise, ssid: b"bssname".to_vec());
        let req = connect_req_with_desc(b"bssname".to_vec(), bss_desc, credential);
        let mut connect_fut = sme.on_connect_command(req);

        assert_eq!(Status { connected_to: None, connecting_to: None }, sme.status());
        assert_no_join(&mut mlme_stream);

        // User should get a message that connection failed
        assert_variant!(
            connect_fut.try_recv(),
            Ok(Some(ConnectResult::Failed(ConnectFailure::SelectNetworkFailure(
                SelectNetworkFailure::IncompatibleConnectRequest,
            ),),),)
        );
    }

    #[test]
    fn connecting_no_scan_result_with_ssid() {
        let (mut sme, _mlme_stream, _info_stream, _time_stream) = create_sme();

        let credential = fidl_sme::Credential::None(fidl_sme::Empty);
        let mut connect_fut = sme.on_connect_command(connect_req(b"foo".to_vec(), credential));
        let bss_desc = fake_fidl_bss!(Open, ssid: b"bar".to_vec());
        report_fake_scan_result(&mut sme, bss_desc);

        assert_variant!(connect_fut.try_recv(), Ok(Some(failure)) => {
            assert_eq!(failure, SelectNetworkFailure::NoScanResultWithSsid.into());
        });
    }

    #[test]
    fn connecting_right_credential_type_no_privacy() {
        let (mut sme, _mlme_stream, _info_stream, _time_stream) = create_sme();

        let credential = fidl_sme::Credential::Password(b"password".to_vec());
        let mut connect_fut = sme.on_connect_command(connect_req(b"foo".to_vec(), credential));
        let bss_desc = fidl_internal::BssDescription {
            cap: wlan_common::mac::CapabilityInfo(0).with_privacy(false).0,
            ..fake_fidl_bss!(Wpa2, ssid: b"foo".to_vec())
        };
        report_fake_scan_result(&mut sme, bss_desc);

        assert_variant!(connect_fut.try_recv(), Ok(Some(failure)) => {
            assert_eq!(failure, SelectNetworkFailure::IncompatibleConnectRequest.into());
        });
    }

    #[test]
    fn connecting_right_credential_type_but_short_password() {
        let (mut sme, _mlme_stream, _info_stream, _time_stream) = create_sme();

        let credential = fidl_sme::Credential::Password(b"pass".to_vec());
        let mut connect_fut = sme.on_connect_command(connect_req(b"foo".to_vec(), credential));
        let bss_desc = fake_fidl_bss!(Wpa2, ssid: b"foo".to_vec());
        report_fake_scan_result(&mut sme, bss_desc);

        assert_variant!(connect_fut.try_recv(), Ok(Some(failure)) => {
            assert_eq!(failure, SelectNetworkFailure::InternalProtectionError.into());
        });
    }

    #[test]
    fn connection_rejected_ssid_too_long() {
        let (mut sme, _mlme_stream, _info_stream, _time_stream) = create_sme();

        let credential = fidl_sme::Credential::None(fidl_sme::Empty);
        // SSID is one byte too long
        let mut connect_fut = sme.on_connect_command(connect_req([65; 33].to_vec(), credential));

        assert_variant!(connect_fut.try_recv(), Ok(Some(failure)) => {
            assert_eq!(failure, SelectNetworkFailure::NoScanResultWithSsid.into());
        });
    }

    #[test]
    fn new_connect_attempt_cancels_pending_connect() {
        let (mut sme, _mlme_stream, _info_stream, _time_stream) = create_sme();

        let req = connect_req(b"foo".to_vec(), fidl_sme::Credential::None(fidl_sme::Empty));
        let mut connect_fut1 = sme.on_connect_command(req);

        let req2 = connect_req(b"foo".to_vec(), fidl_sme::Credential::None(fidl_sme::Empty));
        let mut connect_fut2 = sme.on_connect_command(req2);

        // User should get a message that first connection attempt is canceled
        assert_connect_result(&mut connect_fut1, ConnectResult::Canceled);
        // Report scan result to transition second connection attempt past scan. This is to verify
        // that connection attempt will be canceled even in the middle of joining the network
        report_fake_scan_result(&mut sme, fake_fidl_bss!(Open, ssid: b"foo".to_vec()));

        let req3 = connect_req(b"foo".to_vec(), fidl_sme::Credential::None(fidl_sme::Empty));
        let mut _connect_fut3 = sme.on_connect_command(req3);

        // Verify that second connection attempt is canceled as new connect request comes in
        assert_connect_result(&mut connect_fut2, ConnectResult::Canceled);
    }

    #[test]
    fn test_info_event_complete_connect() {
        let (mut sme, _mlme_stream, mut info_stream, _time_stream) = create_sme();

        let credential = fidl_sme::Credential::None(fidl_sme::Empty);
        let _recv = sme.on_connect_command(connect_req(b"foo".to_vec(), credential));

        let bss_desc = fake_fidl_bss!(Open, ssid: b"foo".to_vec());
        let bssid = bss_desc.bssid;
        report_fake_scan_result(&mut sme, bss_desc);

        sme.on_mlme_event(create_join_conf(fidl_mlme::JoinResultCodes::Success));
        sme.on_mlme_event(create_auth_conf(bssid, fidl_mlme::AuthenticateResultCodes::Success));
        sme.on_mlme_event(create_assoc_conf(fidl_mlme::AssociateResultCodes::Success));

        assert_variant!(info_stream.try_next(), Ok(Some(InfoEvent::ConnectionPing(..))));
        assert_variant!(info_stream.try_next(), Ok(Some(InfoEvent::ConnectStats(stats))) => {
            let scan_stats = stats.join_scan_stats().expect("expect join scan stats");
            assert!(!scan_stats.scan_start_while_connected);
            assert!(scan_stats.scan_time().into_nanos() > 0);
            assert_eq!(scan_stats.result, ScanResult::Success);
            assert_eq!(scan_stats.bss_count, 1);
            assert!(stats.auth_time().is_some());
            assert!(stats.assoc_time().is_some());
            assert!(stats.rsna_time().is_none());
            assert!(stats.connect_time().into_nanos() > 0);
            assert_eq!(stats.result, ConnectResult::Success);
            assert!(stats.candidate_network.is_some());
            assert!(stats.previous_disconnect_info.is_none());
        });
    }

    #[test]
    fn test_info_event_failed_connect() {
        let (mut sme, _mlme_stream, mut info_stream, _time_stream) = create_sme();

        let credential = fidl_sme::Credential::None(fidl_sme::Empty);
        let _recv = sme.on_connect_command(connect_req(b"foo".to_vec(), credential));

        let bss_desc = fake_fidl_bss!(Open, ssid: b"foo".to_vec());
        let bssid = bss_desc.bssid;
        report_fake_scan_result(&mut sme, bss_desc);

        sme.on_mlme_event(create_join_conf(fidl_mlme::JoinResultCodes::Success));
        let auth_failure = fidl_mlme::AuthenticateResultCodes::Refused;
        sme.on_mlme_event(create_auth_conf(bssid, auth_failure));

        let result = ConnectResult::Failed(ConnectFailure::AuthenticationFailure(auth_failure));
        assert_variant!(info_stream.try_next(), Ok(Some(InfoEvent::ConnectStats(stats))) => {
            assert_eq!(stats.join_scan_stats().expect("no scan stats").result, ScanResult::Success);
            assert!(stats.auth_time().is_some());
            // no association time since requests already fails at auth step
            assert!(stats.assoc_time().is_none());
            assert!(stats.rsna_time().is_none());
            assert!(stats.connect_time().into_nanos() > 0);
            assert_eq!(stats.result, result);
            assert!(stats.candidate_network.is_some());
        });
        expect_stream_empty(&mut info_stream, "unexpected event in info stream");
    }

    #[test]
    fn test_info_event_connect_canceled_during_scan() {
        let (mut sme, _mlme_stream, mut info_stream, _time_stream) = create_sme();

        let credential = fidl_sme::Credential::None(fidl_sme::Empty);
        let _recv = sme.on_connect_command(connect_req(b"foo".to_vec(), credential));

        // Send another connect request, which should cancel first one
        let credential = fidl_sme::Credential::None(fidl_sme::Empty);
        let _recv = sme.on_connect_command(connect_req(b"foo".to_vec(), credential));
        assert_variant!(info_stream.try_next(), Ok(Some(InfoEvent::ConnectStats(stats))) => {
            assert!(stats.scan_start_stats.is_some());
            // Connect attempt cancels before scan is finished. Since we send stats right away,
            // end stats is blank and a complete scan stats cannot be constructed.
            assert!(stats.scan_end_stats.is_none());
            assert!(stats.join_scan_stats().is_none());

            assert!(stats.connect_time().into_nanos() > 0);
            assert_eq!(stats.result, ConnectResult::Canceled);
            assert!(stats.candidate_network.is_none());
        });

        // Old scan finishes. However, no join scan stats is sent
        report_fake_scan_result(&mut sme, fake_fidl_bss!(Open, ssid: b"foo".to_vec()));
        assert_variant!(info_stream.try_next(), Err(_));
    }

    #[test]
    fn test_info_event_connect_canceled_post_scan() {
        let (mut sme, _mlme_stream, mut info_stream, _time_stream) = create_sme();

        let credential = fidl_sme::Credential::None(fidl_sme::Empty);
        let _recv = sme.on_connect_command(connect_req(b"foo".to_vec(), credential));

        report_fake_scan_result(&mut sme, fake_fidl_bss!(Open, ssid: b"foo".to_vec()));

        // Send another connect request, which should cancel first one
        let credential = fidl_sme::Credential::None(fidl_sme::Empty);
        let _recv = sme.on_connect_command(connect_req(b"foo".to_vec(), credential));
        assert_variant!(info_stream.try_next(), Ok(Some(InfoEvent::ConnectStats(stats))) => {
            assert_eq!(stats.join_scan_stats().expect("no scan stats").result, ScanResult::Success);
            assert!(stats.connect_time().into_nanos() > 0);
            assert_eq!(stats.result, ConnectResult::Canceled);
            assert!(stats.candidate_network.is_some());
        });
    }

    #[test]
    fn test_info_event_candidate_network_multiple_bss() {
        let (mut sme, _mlme_stream, mut info_stream, _time_stream) = create_sme();

        let credential = fidl_sme::Credential::None(fidl_sme::Empty);
        let _recv = sme.on_connect_command(connect_req(b"foo".to_vec(), credential));

        let mut bss = fake_fidl_bss!(Open, ssid: b"foo".to_vec());
        bss.bssid = [1; 6];
        sme.on_mlme_event(MlmeEvent::OnScanResult {
            result: fidl_mlme::ScanResult { txn_id: 1, bss },
        });
        let mut bss = fake_fidl_bss!(Open, ssid: b"foo".to_vec());
        bss.bssid = [3; 6];
        sme.on_mlme_event(MlmeEvent::OnScanResult {
            result: fidl_mlme::ScanResult { txn_id: 1, bss },
        });
        // This scan result should not be counted since it's not the SSID we request
        let bss = fake_fidl_bss!(Open, ssid: b"bar".to_vec());
        sme.on_mlme_event(MlmeEvent::OnScanResult {
            result: fidl_mlme::ScanResult { txn_id: 1, bss },
        });
        sme.on_mlme_event(MlmeEvent::OnScanEnd {
            end: fidl_mlme::ScanEnd { txn_id: 1, code: fidl_mlme::ScanResultCodes::Success },
        });

        // Stop connecting attempt early since we just want to get ConnectStats
        sme.on_mlme_event(create_join_conf(fidl_mlme::JoinResultCodes::JoinFailureTimeout));

        assert_variant!(info_stream.try_next(), Ok(Some(InfoEvent::ConnectStats(stats))) => {
            assert_eq!(stats.join_scan_stats().expect("no scan stats").bss_count, 2);
            assert!(stats.candidate_network.is_some());
        });
    }

    #[test]
    fn test_info_event_dont_suppress_bss() {
        let (mut sme, _mlme_strem, _info_stream, _time_stream) = create_sme();
        let mut recv =
            sme.on_scan_command(fidl_sme::ScanRequest::Passive(fidl_sme::PassiveScanRequest {}));

        let mut bss = fake_fidl_bss!(Open, ssid: b"foo".to_vec());
        bss.bssid = [3; 6];
        sme.on_mlme_event(MlmeEvent::OnScanResult {
            result: fidl_mlme::ScanResult { txn_id: 1, bss },
        });
        let mut bss = fake_fidl_bss!(Open, ssid: b"foo".to_vec());
        bss.bssid = [4; 6];
        sme.on_mlme_event(MlmeEvent::OnScanResult {
            result: fidl_mlme::ScanResult { txn_id: 1, bss },
        });
        sme.on_mlme_event(MlmeEvent::OnScanEnd {
            end: fidl_mlme::ScanEnd { txn_id: 1, code: fidl_mlme::ScanResultCodes::Success },
        });

        // check that both BSS are received at the end of a scan
        assert_variant!(recv.try_recv(), Ok(Some(Ok(bss_info))) => {
            let mut reported_bss_ssid = bss_info.into_iter().map(|bss| (bss.ssid, bss.bssid)).collect::<Vec<_>>();
            reported_bss_ssid.sort();
            assert_eq!(reported_bss_ssid, vec![(b"foo".to_vec(), [3; 6]), (b"foo".to_vec(), [4; 6])]);
        })
    }

    #[test]
    fn test_info_event_discovery_scan() {
        let (mut sme, _mlme_stream, mut info_stream, _time_stream) = create_sme();

        let _recv =
            sme.on_scan_command(fidl_sme::ScanRequest::Active(fidl_sme::ActiveScanRequest {
                ssids: vec![],
                channels: vec![],
            }));

        report_fake_scan_result(&mut sme, fake_fidl_bss!(Open, ssid: b"foo".to_vec()));

        assert_variant!(info_stream.try_next(), Ok(Some(InfoEvent::DiscoveryScanStats(scan_stats))) => {
            assert!(!scan_stats.scan_start_while_connected);
            assert!(scan_stats.scan_time().into_nanos() > 0);
            assert_eq!(scan_stats.scan_type, fidl_mlme::ScanTypes::Active);
            assert_eq!(scan_stats.result, ScanResult::Success);
            assert_eq!(scan_stats.bss_count, 1);
        });
    }

    #[test]
    fn test_on_connect_command_default_to_active_scan() {
        let (mut sme, mut mlme_stream, _info_stream, _time_stream) = create_sme();
        sme.context.is_softmac = false;
        assert_eq!(Status { connected_to: None, connecting_to: None }, sme.status());

        let credential = fidl_sme::Credential::None(fidl_sme::Empty);
        let _recv = sme.on_connect_command(connect_req(b"foo".to_vec(), credential));
        assert_variant!(mlme_stream.try_next(), Ok(Some(MlmeRequest::Scan(req))) => {
            assert_eq!(req.scan_type, fidl_mlme::ScanTypes::Active);
        });
    }

    #[test]
    fn test_on_connect_command_softmac_adhere_to_scan_type_arg_passive() {
        let (mut sme, mut mlme_stream, _info_stream, _time_stream) = create_sme();
        sme.context.is_softmac = true;
        assert_eq!(Status { connected_to: None, connecting_to: None }, sme.status());

        let credential = fidl_sme::Credential::None(fidl_sme::Empty);
        let _recv = sme.on_connect_command(connect_req(b"foo".to_vec(), credential));
        assert_variant!(mlme_stream.try_next(), Ok(Some(MlmeRequest::Scan(req))) => {
            assert_eq!(req.scan_type, fidl_mlme::ScanTypes::Passive);
        });
    }

    #[test]
    fn test_on_connect_command_softmac_adhere_to_scan_type_arg_active() {
        let (mut sme, mut mlme_stream, _info_stream, _time_stream) = create_sme();
        sme.context.is_softmac = true;
        assert_eq!(Status { connected_to: None, connecting_to: None }, sme.status());

        let mut req = connect_req(b"foo".to_vec(), fidl_sme::Credential::None(fidl_sme::Empty));
        req.deprecated_scan_type = fidl_common::ScanType::Active;
        let _recv = sme.on_connect_command(req);
        assert_variant!(mlme_stream.try_next(), Ok(Some(MlmeRequest::Scan(req))) => {
            assert_eq!(req.scan_type, fidl_mlme::ScanTypes::Active);
        });
    }

    fn assert_connect_result(
        connect_result_receiver: &mut oneshot::Receiver<ConnectResult>,
        expected: ConnectResult,
    ) {
        match connect_result_receiver.try_recv() {
            Ok(Some(actual)) => assert_eq!(expected, actual),
            other => panic!("expect {:?}, got {:?}", expected, other),
        }
    }

    fn assert_no_join(mlme_stream: &mut mpsc::UnboundedReceiver<MlmeRequest>) {
        loop {
            match mlme_stream.try_next() {
                Ok(event) => match event {
                    Some(MlmeRequest::Join(..)) => panic!("unexpected join request sent to MLME"),
                    None => break,
                    _ => (),
                },
                Err(e) => {
                    assert_eq!(e.to_string(), "receiver channel is empty");
                    break;
                }
            }
        }
    }

    fn assert_connect_result_failed(connect_fut: &mut oneshot::Receiver<ConnectResult>) {
        assert_variant!(connect_fut.try_recv(), Ok(Some(ConnectResult::Failed(..))));
    }

    fn connect_req(ssid: Ssid, credential: fidl_sme::Credential) -> fidl_sme::ConnectRequest {
        fidl_sme::ConnectRequest {
            ssid,
            bss_desc: None,
            credential,
            radio_cfg: RadioConfig::default().to_fidl(),
            deprecated_scan_type: fidl_common::ScanType::Passive,
        }
    }

    fn connect_req_with_desc(
        ssid: Ssid,
        bss_desc: fidl_internal::BssDescription,
        credential: fidl_sme::Credential,
    ) -> fidl_sme::ConnectRequest {
        fidl_sme::ConnectRequest {
            ssid,
            bss_desc: Some(Box::new(bss_desc)),
            credential,
            radio_cfg: RadioConfig::default().to_fidl(),
            deprecated_scan_type: fidl_common::ScanType::Passive,
        }
    }

    fn create_sme() -> (ClientSme, MlmeStream, InfoStream, TimeStream) {
        let inspector = finspect::Inspector::new();
        let sme_root_node = inspector.root().create_child("sme");
        ClientSme::new(
            ClientConfig::default(),
            test_utils::fake_device_info(CLIENT_ADDR),
            Arc::new(wlan_inspect::iface_mgr::IfaceTreeHolder::new(sme_root_node)),
            WlanHasher::new(DUMMY_HASH_KEY),
            true, // is_softmac
        )
    }
}
