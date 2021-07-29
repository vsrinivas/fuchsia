// Copyright 2021 The Fuchsia Authors. All rights reserved.
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
        scan::{DiscoveryScan, ScanScheduler},
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
    anyhow::{bail, format_err, Context as _},
    fidl_fuchsia_wlan_ieee80211 as fidl_ieee80211, fidl_fuchsia_wlan_internal as fidl_internal,
    fidl_fuchsia_wlan_mlme::{self as fidl_mlme, DeviceInfo, MlmeEvent, ScanRequest},
    fidl_fuchsia_wlan_sme as fidl_sme, fuchsia_zircon as zx,
    futures::channel::{mpsc, oneshot},
    log::{error, info, warn},
    std::sync::Arc,
    wep_deprecated,
    wlan_common::{
        self,
        bss::{BssDescription, Protection as BssProtection},
        hasher::WlanHasher,
        ie::{self, wsc},
        RadioConfig,
    },
    wlan_rsn::auth,
};

pub use self::{
    bss::{BssInfo, ClientConfig},
    info::InfoEvent,
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

// An automatically increasing sequence number that uniquely identifies a logical
// connection attempt. For example, a new connection attempt can be triggered
// by a DisassociateInd message from the MLME.
pub type ConnectionAttemptId = u64;

pub type ScanTxnId = u64;

pub struct ClientSme {
    cfg: ClientConfig,
    state: Option<ClientState>,
    scan_sched: ScanScheduler<Responder<BssDiscoveryResult>>,
    wmm_status_responders: Vec<Responder<fidl_sme::ClientSmeWmmStatusResult>>,
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
    // TODO(fxbug.dev/68531): SME no longer performs scans when connecting. Remove the
    //                        `ScanFailure` variant.
    ScanFailure(fidl_mlme::ScanResultCode),
    JoinFailure(fidl_mlme::JoinResultCode),
    AuthenticationFailure(fidl_mlme::AuthenticateResultCode),
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
                fidl_mlme::AuthenticateResultCode::AuthFailureTimeout => true,
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
            // fidl_mlme::AssociateResultCode. The code that arises
            // when WEP fails with rejected credentials is
            // RefusedReasonUnspecified. This is a catch-all error for
            // a WEP authentication failure, but it is being
            // considered good enough for catching rejected
            // credentials for a deprecated WEP association.
            ConnectFailure::AssociationFailure(AssociationFailure {
                bss_protection: BssProtection::Wep,
                code: fidl_mlme::AssociateResultCode::RefusedNotAuthenticated,
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
    pub code: fidl_mlme::AssociateResultCode,
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

pub type BssDiscoveryResult = Result<Vec<BssInfo>, fidl_mlme::ScanResultCode>;

// Almost mirrors fidl_sme::ServingApInfo except that ServingApInfo
// contains more info here than it does in fidl_sme.
#[derive(Clone, Debug, PartialEq)]
pub struct ServingApInfo {
    pub bssid: [u8; 6],
    pub ssid: Ssid,
    pub rssi_dbm: i8,
    pub snr_db: i8,
    pub signal_report_time: zx::Time,
    pub channel: wlan_common::channel::Channel,
    pub protection: BssProtection,
    pub ht_cap: Option<fidl_internal::HtCapabilities>,
    pub vht_cap: Option<fidl_internal::VhtCapabilities>,
    pub probe_resp_wsc: Option<wsc::ProbeRespWsc>,
    pub wmm_param: Option<ie::WmmParam>,
}

impl From<ServingApInfo> for fidl_sme::ServingApInfo {
    fn from(ap: ServingApInfo) -> fidl_sme::ServingApInfo {
        fidl_sme::ServingApInfo {
            bssid: ap.bssid,
            ssid: ap.ssid.clone(),
            rssi_dbm: ap.rssi_dbm,
            snr_db: ap.snr_db,
            channel: ap.channel.into(),
            protection: ap.protection.into(),
        }
    }
}

#[derive(Clone, Debug, PartialEq)]
pub enum ClientSmeStatus {
    Connected(ServingApInfo),
    Connecting(Ssid),
    Idle,
}

impl ClientSmeStatus {
    pub fn is_connected(&self) -> bool {
        matches!(self, ClientSmeStatus::Connected(_))
    }
}

impl From<ClientSmeStatus> for fidl_sme::ClientStatusResponse {
    fn from(client_sme_status: ClientSmeStatus) -> fidl_sme::ClientStatusResponse {
        match client_sme_status {
            ClientSmeStatus::Connected(serving_ap_info) => {
                fidl_sme::ClientStatusResponse::Connected(serving_ap_info.into())
            }
            ClientSmeStatus::Connecting(ssid) => {
                fidl_sme::ClientStatusResponse::Connecting(ssid.clone())
            }
            ClientSmeStatus::Idle => fidl_sme::ClientStatusResponse::Idle(fidl_sme::Empty {}),
        }
    }
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
                wmm_status_responders: vec![],
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
        if req.ssid.len() > (fidl_ieee80211::MAX_SSID_BYTE_LEN as usize) {
            // TODO(fxbug.dev/42081): Use a more accurate error (InvalidSsidArg) for this error.
            responder.respond(SelectNetworkFailure::NoScanResultWithSsid.into());
            return receiver;
        }
        // Cancel any ongoing connect attempt
        self.state = self.state.take().map(|state| state.cancel_ongoing_connect(&mut self.context));

        let ssid = req.ssid;
        let bss_desc = match BssDescription::from_fidl(req.bss_desc) {
            Ok(bss_desc) => bss_desc,
            Err(e) => {
                error!("Failed converting FIDL BssDescription in ConnectRequest: {:?}", e);
                responder.respond(SelectNetworkFailure::IncompatibleConnectRequest.into());
                return receiver;
            }
        };

        info!("Received ConnectRequest for {}", bss_desc.to_string(&self.context.inspect.hasher));

        if !self.cfg.is_bss_compatible(&bss_desc, &self.context.device_info) {
            warn!("BSS is incompatible");
            responder.respond(SelectNetworkFailure::IncompatibleConnectRequest.into());
            return receiver;
        }
        // We can connect directly now.
        let protection = match get_protection(
            &self.context.device_info,
            &self.cfg,
            &req.credential,
            &bss_desc,
            &self.context.inspect.hasher,
        ) {
            Ok(protection) => protection,
            Err(error) => {
                warn!("{:?}", error);
                responder.respond(SelectNetworkFailure::IncompatibleConnectRequest.into());
                return receiver;
            }
        };
        let cmd = ConnectCommand {
            bss: Box::new(bss_desc.clone()),
            responder: Some(responder),
            protection,
            radio_cfg: RadioConfig::from_fidl(req.radio_cfg),
        };

        self.context.info.report_connect_started(ssid);
        self.context.info.report_candidate_network(info::CandidateNetwork {
            bss: bss_desc,
            multiple_bss_candidates: req.multiple_bss_candidates,
        });
        self.state = self.state.take().map(|state| state.connect(cmd, &mut self.context));
        receiver
    }

    pub fn on_disconnect_command(
        &mut self,
        policy_disconnect_reason: fidl_sme::UserDisconnectReason,
    ) {
        self.state = self
            .state
            .take()
            .map(|state| state.disconnect(&mut self.context, policy_disconnect_reason));
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

    pub fn status(&self) -> ClientSmeStatus {
        self.state.as_ref().expect("expected state to be always present").status()
    }

    pub fn wmm_status(&mut self) -> oneshot::Receiver<fidl_sme::ClientSmeWmmStatusResult> {
        let (responder, receiver) = Responder::new();
        self.wmm_status_responders.push(responder);
        self.context.mlme_sink.send(MlmeRequest::WmmStatusReq);
        receiver
    }

    fn send_scan_request(&mut self, req: Option<ScanRequest>) {
        if let Some(req) = req {
            self.context
                .info
                .report_scan_started(clone_scan_request(&req), self.status().is_connected());
            self.context.mlme_sink.send(MlmeRequest::Scan(req));
        }
    }
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
                    scan::ScanResult::DiscoveryFinished { tokens, bss_description_list } => {
                        let bss_description_list =
                            bss_description_list.map(|bss_description_list| {
                                bss_description_list
                                    .iter()
                                    .map(|bss_description| {
                                        self.cfg.convert_bss_description(
                                            &bss_description,
                                            None,
                                            &self.context.device_info,
                                        )
                                    })
                                    .collect()
                            });
                        for responder in tokens {
                            responder.respond(bss_description_list.clone());
                        }
                    }
                }
            }
            MlmeEvent::OnWmmStatusResp { status, resp } => {
                for responder in self.wmm_status_responders.drain(..) {
                    let result =
                        if status == zx::sys::ZX_OK { Ok(resp.clone()) } else { Err(status) };
                    responder.respond(result);
                }
                let event = MlmeEvent::OnWmmStatusResp { status, resp };
                self.state =
                    self.state.take().map(|state| state.on_mlme_event(event, &mut self.context));
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
                self.context.mlme_sink.send(MlmeRequest::WmmStatusReq);
                self.context.timer.schedule(event::InspectPulseCheck);
                state
            }
        });

        // Because `self.status()` relies on the value of `self.state` to be present, we cannot
        // retrieve it and update pulse node inside the closure above.
        self.context.inspect.update_pulse(self.status());
    }
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

enum SelectedAssocType {
    Open,
    Wep,
    Wpa1,
    Wpa2,
    Wpa3,
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

    let selected_assoc_type = match bss.protection() {
        wlan_common::bss::Protection::Open => SelectedAssocType::Open,
        wlan_common::bss::Protection::Wep => SelectedAssocType::Wep,
        wlan_common::bss::Protection::Wpa1 => SelectedAssocType::Wpa1,
        wlan_common::bss::Protection::Wpa3Personal if client_config.wpa3_supported => {
            SelectedAssocType::Wpa3
        }
        // Only a password credential is valid for Wpa3, so downgrade to Wpa2 in other
        // cases if possible.
        wlan_common::bss::Protection::Wpa2Wpa3Personal => match credential {
            fidl_sme::Credential::Password(_) if client_config.wpa3_supported => {
                SelectedAssocType::Wpa3
            }
            _ => SelectedAssocType::Wpa2,
        },
        wlan_common::bss::Protection::Wpa1Wpa2PersonalTkipOnly
        | wlan_common::bss::Protection::Wpa2PersonalTkipOnly
        | wlan_common::bss::Protection::Wpa1Wpa2Personal
        | wlan_common::bss::Protection::Wpa2Personal => SelectedAssocType::Wpa2,
        wlan_common::bss::Protection::Unknown => {
            bail!("Unknown protection type for {} ({})", ssid_hash, bssid_hash)
        }
        _ => bail!("Unsupported protection type for {} ({})", ssid_hash, bssid_hash),
    };

    match selected_assoc_type {
        SelectedAssocType::Open => match credential {
            fidl_sme::Credential::None(_) => Ok(Protection::Open),
            _ => Err(format_err!(
                "Open network {} ({}) not compatible with credentials.",
                ssid_hash,
                bssid_hash
            )),
        },
        SelectedAssocType::Wep => match credential {
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
        SelectedAssocType::Wpa1 => get_legacy_wpa_association(device_info, credential, bss)
            .with_context(|| {
                format!(
                    "WPA1 network {} ({}) protection cannot be retrieved with credential {:?}",
                    ssid_hash, bssid_hash, credential
                )
            }),
        SelectedAssocType::Wpa2 => get_wpa2_rsna(device_info, credential, bss).with_context(|| {
            format!(
                "WPA2 network {} ({}) protection cannot be retrieved with credential {:?}",
                ssid_hash, bssid_hash, credential
            )
        }),
        SelectedAssocType::Wpa3 => get_wpa3_rsna(device_info, credential, bss).with_context(|| {
            format!(
                "WPA3 network {} ({}) protection cannot be retrieved with credential {:?}:",
                ssid_hash, bssid_hash, credential
            )
        }),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::Config as SmeConfig;
    use fidl_fuchsia_wlan_common as fidl_common;
    use fidl_fuchsia_wlan_internal as fidl_internal;
    use fidl_fuchsia_wlan_mlme as fidl_mlme;
    use fuchsia_inspect as finspect;
    use wlan_common::{assert_variant, fake_bss, fake_fidl_bss, ie::rsn::akm, RadioConfig};

    use super::test_utils::{
        create_assoc_conf, create_auth_conf, create_join_conf, create_on_wmm_status_resp,
        expect_stream_empty, fake_wmm_status_resp,
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
            end: fidl_mlme::ScanEnd { txn_id: 1, code: fidl_mlme::ScanResultCode::Success },
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
            code: fidl_mlme::AssociateResultCode::RefusedNotAuthenticated,
        });
        assert!(failure.likely_due_to_credential_rejected());
    }

    #[test]
    fn test_no_detection_of_rejected_wpa1_or_wpa2_credentials() {
        let failure = ConnectFailure::ScanFailure(fidl_mlme::ScanResultCode::InternalError);
        assert!(!failure.likely_due_to_credential_rejected());

        let failure = ConnectFailure::AssociationFailure(AssociationFailure {
            bss_protection: BssProtection::Wpa2Personal,
            code: fidl_mlme::AssociateResultCode::RefusedNotAuthenticated,
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

        // WPA2/3, WPA3 supported but PSK credential, downgrade to WPA2
        let credential = fidl_sme::Credential::Psk(vec![0xAC; 32]);
        let bss = fake_bss!(Wpa2Wpa3);
        let protection = get_protection(&dev_info, &client_config, &credential, &bss, &hasher);
        assert_variant!(protection, Ok(Protection::Rsna(rsna)) => {
            assert_eq!(rsna.negotiated_protection.akm.suite_type, akm::PSK)
        });

        // WPA2/3, WPA3 unsupported, downgrade to WPA2
        let credential = fidl_sme::Credential::Password(b"somepass".to_vec());
        let bss = fake_bss!(Wpa2Wpa3);
        let protection = get_protection(&dev_info, &client_config, &credential, &bss, &hasher);
        assert_variant!(protection, Ok(Protection::Rsna(rsna)) => {
            assert_eq!(rsna.negotiated_protection.akm.suite_type, akm::PSK)
        });
    }

    #[test]
    fn status_connecting() {
        let (mut sme, _mlme_stream, _info_stream, _time_stream) = create_sme();
        assert_eq!(ClientSmeStatus::Idle, sme.status());

        // Issue a connect command and expect the status to change appropriately.
        let credential = fidl_sme::Credential::None(fidl_sme::Empty);
        let bss_desc = fake_fidl_bss!(Open, ssid: b"foo".to_vec());
        let _recv = sme.on_connect_command(connect_req(b"foo".to_vec(), bss_desc, credential));
        assert_eq!(ClientSmeStatus::Connecting(b"foo".to_vec()), sme.status());

        // We should still be connecting to "foo", but the status should now come from the state
        // machine and not from the scanner.
        let ssid = assert_variant!(sme.state.as_ref().unwrap().status(), ClientSmeStatus::Connecting(ssid) => ssid);
        assert_eq!(b"foo".to_vec(), ssid);
        assert_eq!(ClientSmeStatus::Connecting(b"foo".to_vec()), sme.status());

        // As soon as connect command is issued for "bar", the status changes immediately
        let credential = fidl_sme::Credential::None(fidl_sme::Empty);
        let bss_desc = fake_fidl_bss!(Open, ssid: b"bar".to_vec());
        let _recv2 = sme.on_connect_command(connect_req(b"bar".to_vec(), bss_desc, credential));
        assert_eq!(ClientSmeStatus::Connecting(b"bar".to_vec()), sme.status());
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
        assert_eq!(ClientSmeStatus::Idle, sme.status());

        // Issue a connect command and expect the status to change appropriately.
        let credential = fidl_sme::Credential::Password(b"wep40".to_vec());
        let bss_desc = fake_fidl_bss!(Wep, ssid: b"foo".to_vec());
        let req = connect_req(b"foo".to_vec(), bss_desc, credential);
        let _recv = sme.on_connect_command(req);
        assert_eq!(ClientSmeStatus::Connecting(b"foo".to_vec()), sme.status());
        assert_variant!(mlme_stream.try_next(), Ok(Some(MlmeRequest::Join(..))));
    }

    #[test]
    fn connecting_to_wep_network_unsupported() {
        let (mut sme, mut _mlme_stream, _info_stream, _time_stream) = create_sme();
        assert_eq!(ClientSmeStatus::Idle, sme.status());

        // Issue a connect command and expect the status to change appropriately.
        let credential = fidl_sme::Credential::Password(b"wep40".to_vec());
        let bss_desc = fake_fidl_bss!(Wep, ssid: b"foo".to_vec());
        let req = connect_req(b"foo".to_vec(), bss_desc, credential);
        let mut _connect_fut = sme.on_connect_command(req);
        assert_eq!(ClientSmeStatus::Idle, sme.state.as_ref().unwrap().status());
    }

    #[test]
    fn connecting_password_supplied_for_protected_network() {
        let (mut sme, mut mlme_stream, _info_stream, _time_stream) = create_sme();
        assert_eq!(ClientSmeStatus::Idle, sme.status());

        // Issue a connect command and expect the status to change appropriately.
        let credential = fidl_sme::Credential::Password(b"somepass".to_vec());
        let bss_desc = fake_fidl_bss!(Wpa2, ssid: b"foo".to_vec());
        let req = connect_req(b"foo".to_vec(), bss_desc, credential);
        let _recv = sme.on_connect_command(req);
        assert_eq!(ClientSmeStatus::Connecting(b"foo".to_vec()), sme.status());

        assert_variant!(mlme_stream.try_next(), Ok(Some(MlmeRequest::Join(..))));
    }

    #[test]
    fn connecting_psk_supplied_for_protected_network() {
        let (mut sme, mut mlme_stream, _info_stream, _time_stream) = create_sme();
        assert_eq!(ClientSmeStatus::Idle, sme.status());

        // Issue a connect command and expect the status to change appropriately.

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
        let bss_desc = fake_fidl_bss!(Wpa2, ssid: b"IEEE".to_vec());
        let req = connect_req(b"IEEE".to_vec(), bss_desc, credential);
        let _recv = sme.on_connect_command(req);
        assert_eq!(ClientSmeStatus::Connecting(b"IEEE".to_vec()), sme.status());

        assert_variant!(mlme_stream.try_next(), Ok(Some(MlmeRequest::Join(..))));
    }

    #[test]
    fn connecting_password_supplied_for_unprotected_network() {
        let (mut sme, mut _mlme_stream, _info_stream, _time_stream) = create_sme();
        assert_eq!(ClientSmeStatus::Idle, sme.status());

        let credential = fidl_sme::Credential::Password(b"somepass".to_vec());
        let bss_desc = fake_fidl_bss!(Open, ssid: b"foo".to_vec());
        let req = connect_req(b"foo".to_vec(), bss_desc, credential);
        let mut connect_fut = sme.on_connect_command(req);
        assert_eq!(ClientSmeStatus::Idle, sme.status());

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
        let (mut sme, mut _mlme_stream, _info_stream, _time_stream) = create_sme();
        assert_eq!(ClientSmeStatus::Idle, sme.status());

        let credential = fidl_sme::Credential::Psk(b"somepass".to_vec());
        let bss_desc = fake_fidl_bss!(Open, ssid: b"foo".to_vec());
        let req = connect_req(b"foo".to_vec(), bss_desc, credential);
        let mut connect_fut = sme.on_connect_command(req);
        assert_eq!(ClientSmeStatus::Idle, sme.state.as_ref().unwrap().status());

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
        assert_eq!(ClientSmeStatus::Idle, sme.status());

        let credential = fidl_sme::Credential::None(fidl_sme::Empty);
        let bss_desc = fake_fidl_bss!(Wpa2, ssid: b"foo".to_vec());
        let req = connect_req(b"foo".to_vec(), bss_desc, credential);
        let mut connect_fut = sme.on_connect_command(req);
        assert_eq!(ClientSmeStatus::Idle, sme.state.as_ref().unwrap().status());

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
        assert_eq!(ClientSmeStatus::Idle, sme.status());

        let credential = fidl_sme::Credential::None(fidl_sme::Empty);
        let bss_desc = fake_fidl_bss!(Open, ssid: b"bssname".to_vec());
        let req = connect_req(b"bssname".to_vec(), bss_desc, credential);
        let mut connect_fut = sme.on_connect_command(req);

        assert_eq!(ClientSmeStatus::Connecting(b"bssname".to_vec()), sme.status());
        assert_variant!(mlme_stream.try_next(), Ok(Some(MlmeRequest::Join(..))));
        assert_variant!(connect_fut.try_recv(), Ok(None));
    }

    #[test]
    fn connecting_bypass_join_scan_protected() {
        let (mut sme, mut mlme_stream, _info_stream, _time_stream) = create_sme();
        assert_eq!(ClientSmeStatus::Idle, sme.status());

        let credential = fidl_sme::Credential::Password(b"somepass".to_vec());
        let bss_desc = fake_fidl_bss!(Wpa2, ssid: b"bssname".to_vec());
        let req = connect_req(b"bssname".to_vec(), bss_desc, credential);
        let mut connect_fut = sme.on_connect_command(req);

        assert_eq!(ClientSmeStatus::Connecting(b"bssname".to_vec()), sme.status());
        assert_variant!(mlme_stream.try_next(), Ok(Some(MlmeRequest::Join(..))));
        assert_variant!(connect_fut.try_recv(), Ok(None));
    }

    #[test]
    fn connecting_bypass_join_scan_mismatched_credential() {
        let (mut sme, mut mlme_stream, _info_stream, _time_stream) = create_sme();
        assert_eq!(ClientSmeStatus::Idle, sme.status());

        let credential = fidl_sme::Credential::None(fidl_sme::Empty);
        let bss_desc = fake_fidl_bss!(Wpa2, ssid: b"bssname".to_vec());
        let req = connect_req(b"bssname".to_vec(), bss_desc, credential);
        let mut connect_fut = sme.on_connect_command(req);

        assert_eq!(ClientSmeStatus::Idle, sme.status());
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
        assert_eq!(ClientSmeStatus::Idle, sme.status());

        let credential = fidl_sme::Credential::Password(b"somepass".to_vec());
        let bss_desc = fake_fidl_bss!(Wpa3Enterprise, ssid: b"bssname".to_vec());
        let req = connect_req(b"bssname".to_vec(), bss_desc, credential);
        let mut connect_fut = sme.on_connect_command(req);

        assert_eq!(ClientSmeStatus::Idle, sme.status());
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
    fn connecting_right_credential_type_no_privacy() {
        let (mut sme, _mlme_stream, _info_stream, _time_stream) = create_sme();

        let credential = fidl_sme::Credential::Password(b"password".to_vec());
        let bss_desc = fake_fidl_bss!(
            Wpa2,
            ssid: b"foo".to_vec(),
            capability_info: wlan_common::mac::CapabilityInfo(0).with_privacy(false).0,
        );
        let mut connect_fut =
            sme.on_connect_command(connect_req(b"foo".to_vec(), bss_desc, credential));

        assert_variant!(connect_fut.try_recv(), Ok(Some(failure)) => {
            assert_eq!(failure, SelectNetworkFailure::IncompatibleConnectRequest.into());
        });
    }

    #[test]
    fn connecting_right_credential_type_but_short_password() {
        let (mut sme, _mlme_stream, _info_stream, _time_stream) = create_sme();

        let credential = fidl_sme::Credential::Password(b"pass".to_vec());
        let bss_desc = fake_fidl_bss!(Wpa2, ssid: b"foo".to_vec());
        let mut connect_fut =
            sme.on_connect_command(connect_req(b"foo".to_vec(), bss_desc, credential));
        let bss_desc = fake_fidl_bss!(Wpa2, ssid: b"foo".to_vec());
        report_fake_scan_result(&mut sme, bss_desc);

        assert_variant!(connect_fut.try_recv(), Ok(Some(failure)) => {
            assert_eq!(failure, SelectNetworkFailure::IncompatibleConnectRequest.into());
        });
    }

    #[test]
    fn connection_rejected_ssid_too_long() {
        let (mut sme, _mlme_stream, _info_stream, _time_stream) = create_sme();

        let credential = fidl_sme::Credential::None(fidl_sme::Empty);
        let bss_desc = fake_fidl_bss!(Open, ssid: [65; 33].to_vec());
        // SSID is one byte too long
        let mut connect_fut =
            sme.on_connect_command(connect_req([65; 33].to_vec(), bss_desc, credential));

        assert_variant!(connect_fut.try_recv(), Ok(Some(failure)) => {
            assert_eq!(failure, SelectNetworkFailure::NoScanResultWithSsid.into());
        });
    }

    #[test]
    fn new_connect_attempt_cancels_pending_connect() {
        let (mut sme, _mlme_stream, _info_stream, _time_stream) = create_sme();

        let bss_desc = fake_fidl_bss!(Open, ssid: b"foo".to_vec());
        let req = connect_req(
            b"foo".to_vec(),
            bss_desc.clone(),
            fidl_sme::Credential::None(fidl_sme::Empty),
        );
        let mut connect_fut1 = sme.on_connect_command(req);

        let req2 = connect_req(
            b"foo".to_vec(),
            bss_desc.clone(),
            fidl_sme::Credential::None(fidl_sme::Empty),
        );
        let mut connect_fut2 = sme.on_connect_command(req2);

        // User should get a message that first connection attempt is canceled
        assert_connect_result(&mut connect_fut1, ConnectResult::Canceled);
        // Report scan result to transition second connection attempt past scan. This is to verify
        // that connection attempt will be canceled even in the middle of joining the network
        report_fake_scan_result(&mut sme, fake_fidl_bss!(Open, ssid: b"foo".to_vec()));

        let req3 =
            connect_req(b"foo".to_vec(), bss_desc, fidl_sme::Credential::None(fidl_sme::Empty));
        let mut _connect_fut3 = sme.on_connect_command(req3);

        // Verify that second connection attempt is canceled as new connect request comes in
        assert_connect_result(&mut connect_fut2, ConnectResult::Canceled);
    }

    #[test]
    fn test_info_event_complete_connect() {
        let (mut sme, _mlme_stream, mut info_stream, _time_stream) = create_sme();
        assert_eq!(ClientSmeStatus::Idle, sme.status());

        let credential = fidl_sme::Credential::None(fidl_sme::Empty);
        let bss_desc = fake_fidl_bss!(Open, ssid: b"bssname".to_vec());
        let bssid = bss_desc.bssid;
        let mut req = connect_req(b"bssname".to_vec(), bss_desc, credential);
        req.multiple_bss_candidates = false;
        let _connect_fut = sme.on_connect_command(req);

        sme.on_mlme_event(create_join_conf(fidl_mlme::JoinResultCode::Success));
        sme.on_mlme_event(create_auth_conf(bssid, fidl_mlme::AuthenticateResultCode::Success));
        sme.on_mlme_event(create_assoc_conf(fidl_mlme::AssociateResultCode::Success));

        assert_variant!(info_stream.try_next(), Ok(Some(InfoEvent::ConnectionPing(..))));
        assert_variant!(info_stream.try_next(), Ok(Some(InfoEvent::ConnectStats(stats))) => {
            assert!(stats.auth_time().is_some());
            assert!(stats.assoc_time().is_some());
            assert!(stats.rsna_time().is_none());
            assert!(stats.connect_time().into_nanos() > 0);
            assert_eq!(stats.result, ConnectResult::Success);
            assert_variant!(stats.candidate_network, Some(candidate_network) => {
                assert!(!candidate_network.multiple_bss_candidates);
            });
            assert!(stats.previous_disconnect_info.is_none());
        });
    }

    #[test]
    fn test_info_event_failed_connect() {
        let (mut sme, _mlme_stream, mut info_stream, _time_stream) = create_sme();

        let credential = fidl_sme::Credential::None(fidl_sme::Empty);
        let bss_desc = fake_fidl_bss!(Open, ssid: b"foo".to_vec());
        let _recv =
            sme.on_connect_command(connect_req(b"foo".to_vec(), bss_desc.clone(), credential));

        let bssid = bss_desc.bssid;
        report_fake_scan_result(&mut sme, bss_desc);

        sme.on_mlme_event(create_join_conf(fidl_mlme::JoinResultCode::Success));
        let auth_failure = fidl_mlme::AuthenticateResultCode::Refused;
        sme.on_mlme_event(create_auth_conf(bssid, auth_failure));

        let result = ConnectResult::Failed(ConnectFailure::AuthenticationFailure(auth_failure));
        assert_variant!(info_stream.try_next(), Ok(Some(InfoEvent::ConnectStats(stats))) => {
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
    fn test_info_event_candidate_network_multiple_bss() {
        let (mut sme, _mlme_stream, mut info_stream, _time_stream) = create_sme();

        let credential = fidl_sme::Credential::None(fidl_sme::Empty);
        let bss_desc = fake_fidl_bss!(Open, ssid: b"bssname".to_vec());
        let req = connect_req(b"bssname".to_vec(), bss_desc, credential);
        let _connect_fut = sme.on_connect_command(req);

        // Stop connecting attempt early since we just want to get ConnectStats
        sme.on_mlme_event(create_join_conf(fidl_mlme::JoinResultCode::JoinFailureTimeout));

        assert_variant!(info_stream.try_next(), Ok(Some(InfoEvent::ConnectStats(stats))) => {
            assert_variant!(stats.candidate_network, Some(candidate_network) => {
                assert!(candidate_network.multiple_bss_candidates);
            });
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
            end: fidl_mlme::ScanEnd { txn_id: 1, code: fidl_mlme::ScanResultCode::Success },
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
            assert_eq!(scan_stats.result_code, fidl_mlme::ScanResultCode::Success);
            assert_eq!(scan_stats.bss_count, 1);
        });
    }

    #[test]
    fn test_wmm_status_success() {
        let (mut sme, mut mlme_stream, _info_stream, _time_stream) = create_sme();
        let mut receiver = sme.wmm_status();

        assert_variant!(mlme_stream.try_next(), Ok(Some(MlmeRequest::WmmStatusReq)));

        let resp = fake_wmm_status_resp();
        sme.on_mlme_event(MlmeEvent::OnWmmStatusResp {
            status: zx::sys::ZX_OK,
            resp: resp.clone(),
        });

        assert_eq!(receiver.try_recv(), Ok(Some(Ok(resp))));
    }

    #[test]
    fn test_wmm_status_failed() {
        let (mut sme, mut mlme_stream, _info_stream, _time_stream) = create_sme();
        let mut receiver = sme.wmm_status();

        assert_variant!(mlme_stream.try_next(), Ok(Some(MlmeRequest::WmmStatusReq)));
        sme.on_mlme_event(create_on_wmm_status_resp(zx::sys::ZX_ERR_IO));
        assert_eq!(receiver.try_recv(), Ok(Some(Err(zx::sys::ZX_ERR_IO))));
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

    fn connect_req(
        ssid: Ssid,
        bss_desc: fidl_internal::BssDescription,
        credential: fidl_sme::Credential,
    ) -> fidl_sme::ConnectRequest {
        fidl_sme::ConnectRequest {
            ssid,
            bss_desc,
            credential,
            radio_cfg: RadioConfig::default().to_fidl(),
            deprecated_scan_type: fidl_common::ScanType::Passive,
            multiple_bss_candidates: true,
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
