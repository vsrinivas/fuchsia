// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{ap::Context, error::Error},
    failure::{bail, format_err},
    fidl_fuchsia_wlan_mlme as fidl_mlme,
    wlan_common::mac::{
        self, Aid, AuthAlgorithmNumber, FrameClass, MacAddr, ReasonCode, StatusCode,
    },
    zerocopy::ByteSlice,
};

/// The MLME state machine. The actual state machine transitions are managed and validated in the
/// SME: we only use these states to determine when packets can be sent and received.
#[derive(Debug, PartialEq)]
enum State {
    Authenticating,
    Authenticated,
    Associated {
        /// The EAPoL controlled port can be in three states:
        /// - Some(Closed): The EAPoL controlled port is closed. Only unprotected EAPoL frames can
        ///   be sent.
        /// - Some(Open): The EAPoL controlled port is open. All frames can be sent, and will be
        ///   protected.
        /// - None: There is no EAPoL authentication required, i.e. the network is not an RSN. All
        ///   frames can be sent, and will NOT be protected.
        eapol_controlled_port: Option<fidl_mlme::ControlledPortState>,
    },

    // This is a terminal state indicating the client cannot progress any further.
    Deauthenticated,
}

// TODO(37891): Use this code.
#[allow(dead_code)]
impl State {
    fn max_frame_class(&self) -> FrameClass {
        match self {
            State::Deauthenticated | State::Authenticating => FrameClass::Class1,
            State::Authenticated => FrameClass::Class2,
            State::Associated { .. } => FrameClass::Class3,
        }
    }
}

// TODO(37891): Use this code.
#[allow(dead_code)]
pub struct RemoteClient {
    pub addr: MacAddr,
    state: State,
}

// TODO(37891): Use this code.
//
// TODO(37891): Implement capability negotiation in MLME-ASSOCIATE.response.
// TODO(37891): Implement power management.
// TODO(37891): Implement PS-Poll support.
// TODO(37891): Implement inactivity timeout.
// TODO(37891): Implement action frame handling.
// TODO(37891): Implement either handling MLME-SETKEYS.request or just do it in the SME.
#[allow(dead_code)]
impl RemoteClient {
    pub fn new(addr: MacAddr) -> Self {
        Self { addr, state: State::Authenticating }
    }

    /// Returns if the client is deauthenticated. The caller should use this to check if the client
    /// needs to be forgotten from its state.
    pub fn deauthenticated(&self) -> bool {
        self.state == State::Deauthenticated
    }

    fn is_frame_class_permitted(&self, frame_class: FrameClass) -> bool {
        frame_class <= self.state.max_frame_class()
    }

    // MLME SAP handlers.

    /// Handles MLME-AUTHENTICATE.response (IEEE Std 802.11-2016, 6.3.5.5) from the SME.
    ///
    /// If result_code is Success, the SME will have authenticated this client.
    ///
    /// Otherwise, the MLME should forget about this client.
    pub fn handle_mlme_auth_resp(
        &mut self,
        ctx: &mut Context,
        result_code: fidl_mlme::AuthenticateResultCodes,
    ) -> Result<(), failure::Error> {
        self.state = if result_code == fidl_mlme::AuthenticateResultCodes::Success {
            State::Authenticated
        } else {
            State::Deauthenticated
        };

        // We only support open system auth in the SME.
        // IEEE Std 802.11-2016, 12.3.3.2.3 & Table 9-36: Sequence number 2 indicates the response
        // and final part of Open System authentication.
        ctx.send_auth_frame(
            self.addr.clone(),
            AuthAlgorithmNumber::OPEN,
            2,
            match result_code {
                fidl_mlme::AuthenticateResultCodes::Success => StatusCode::SUCCESS,
                fidl_mlme::AuthenticateResultCodes::Refused => StatusCode::REFUSED,
                fidl_mlme::AuthenticateResultCodes::AntiCloggingTokenRequired => {
                    StatusCode::ANTI_CLOGGING_TOKEN_REQUIRED
                }
                fidl_mlme::AuthenticateResultCodes::FiniteCyclicGroupNotSupported => {
                    StatusCode::UNSUPPORTED_FINITE_CYCLIC_GROUP
                }
                fidl_mlme::AuthenticateResultCodes::AuthenticationRejected => {
                    StatusCode::CHALLENGE_FAILURE
                }
                fidl_mlme::AuthenticateResultCodes::AuthFailureTimeout => {
                    StatusCode::REJECTED_SEQUENCE_TIMEOUT
                }
            },
        )
        .map_err(|e| format_err!("failed to send frame: {}", e))
    }

    /// Handles MLME-DEAUTHENTICATE.request (IEEE Std 802.11-2016, 6.3.6.2) from the SME.
    ///
    /// The SME has already deauthenticated this client.
    ///
    /// After this function is called, the MLME must forget about this client.
    pub fn handle_mlme_deauth_req(
        &mut self,
        ctx: &mut Context,
        reason_code: fidl_mlme::ReasonCode,
    ) -> Result<(), failure::Error> {
        self.state = State::Deauthenticated;

        // IEEE Std 802.11-2016, 6.3.6.3.3 states that we should send MLME-DEAUTHENTICATE.confirm
        // to the SME on success. However, our SME only sends MLME-DEAUTHENTICATE.request when it
        // has already forgotten about the client on its side, so sending
        // MLME-DEAUTHENTICATE.confirm is redundant.

        ctx.send_deauth_frame(self.addr.clone(), ReasonCode(reason_code as u16))
            .map_err(|e| format_err!("failed to send frame: {}", e))
    }

    /// Handles MLME-ASSOCIATE.response (IEEE Std 802.11-2016, 6.3.7.5) from the SME.
    ///
    /// If the result code is Success, the SME will have associated this client.
    ///
    /// Otherwise, the SME has not associated this client. However, the SME has not forgotten about
    /// the client either until MLME-DEAUTHENTICATE.request is received.
    pub fn handle_mlme_assoc_resp(
        &mut self,
        ctx: &mut Context,
        is_rsn: bool,
        capabilities: mac::CapabilityInfo,
        result_code: fidl_mlme::AssociateResultCodes,
        aid: Aid,
        ies: &[u8],
    ) -> Result<(), failure::Error> {
        self.state = if result_code == fidl_mlme::AssociateResultCodes::Success {
            State::Associated {
                eapol_controlled_port: if is_rsn {
                    Some(fidl_mlme::ControlledPortState::Closed)
                } else {
                    None
                },
            }
        } else {
            State::Authenticated
        };

        ctx.send_assoc_resp_frame(
            self.addr.clone(),
            capabilities,
            match result_code {
                fidl_mlme::AssociateResultCodes::Success => StatusCode::SUCCESS,
                fidl_mlme::AssociateResultCodes::RefusedReasonUnspecified => {
                    StatusCode::DENIED_OTHER_REASON
                }
                fidl_mlme::AssociateResultCodes::RefusedNotAuthenticated => {
                    StatusCode::REFUSED_UNAUTHENTICATED_ACCESS_NOT_SUPPORTED
                }
                fidl_mlme::AssociateResultCodes::RefusedCapabilitiesMismatch => {
                    StatusCode::REFUSED_CAPABILITIES_MISMATCH
                }
                fidl_mlme::AssociateResultCodes::RefusedExternalReason => {
                    StatusCode::REFUSED_EXTERNAL_REASON
                }
                fidl_mlme::AssociateResultCodes::RefusedApOutOfMemory => {
                    StatusCode::REFUSED_AP_OUT_OF_MEMORY
                }
                fidl_mlme::AssociateResultCodes::RefusedBasicRatesMismatch => {
                    StatusCode::REFUSED_BASIC_RATES_MISMATCH
                }
                fidl_mlme::AssociateResultCodes::RejectedEmergencyServicesNotSupported => {
                    StatusCode::REJECTED_EMERGENCY_SERVICES_NOT_SUPPORTED
                }
                fidl_mlme::AssociateResultCodes::RefusedTemporarily => {
                    StatusCode::REFUSED_TEMPORARILY
                }
            },
            aid,
            ies,
        )
        .map_err(|e| format_err!("failed to send frame: {}", e))
    }

    /// Handles MLME-DISASSOCIATE.request (IEEE Std 802.11-2016, 6.3.9.1) from the SME.
    ///
    /// The SME has already disassociated this client.
    ///
    /// The MLME doesn't have to do anything other than change its state to acknowledge the
    /// disassociation.
    pub fn handle_mlme_disassoc_req(
        &mut self,
        ctx: &mut Context,
        reason_code: u16,
    ) -> Result<(), failure::Error> {
        self.state = State::Authenticated;

        // IEEE Std 802.11-2016, 6.3.9.2.3 states that we should send MLME-DISASSOCIATE.confirm
        // to the SME on success. Like MLME-DEAUTHENTICATE.confirm, our SME has already forgotten
        // about this client, so sending MLME-DISASSOCIATE.confirm is redundant.

        ctx.send_disassoc_frame(self.addr.clone(), ReasonCode(reason_code))
            .map_err(|e| format_err!("failed to send frame: {}", e))
    }

    /// Handles SET_CONTROLLED_PORT.request (fuchsia.wlan.mlme.SetControlledPortRequest) from the
    /// SME.
    pub fn handle_mlme_set_controlled_port_req(
        &mut self,
        state: fidl_mlme::ControlledPortState,
    ) -> Result<(), failure::Error> {
        match &mut self.state {
            State::Associated {
                eapol_controlled_port: eapol_controlled_port @ Some(_), ..
            } => {
                eapol_controlled_port.replace(state);
            }
            State::Associated { eapol_controlled_port: None, .. } => {
                bail!("cannot set controlled port for non-RSN BSS")
            }
            _ => bail!("cannot set controlled port when not associated"),
        }
        Ok(())
    }

    /// Handles MLME-EAPOL.request (IEEE Std 802.11-2016, 6.3.22.1) from the SME.
    ///
    /// The MLME should forward these frames to the PHY layer.
    pub fn handle_mlme_eapol_req(
        &self,
        ctx: &mut Context,
        data: &[u8],
    ) -> Result<(), failure::Error> {
        // IEEE Std 802.11-2016, 6.3.22.2.3 states that we should send MLME-EAPOL.confirm to the
        // SME on success. Our SME employs a timeout for EAPoL negotiation, so MLME-EAPOL.confirm is
        // redundant.

        ctx.send_eapol_frame(self.addr, ctx.bssid.0.clone(), false, data)
            .map_err(|e| format_err!("failed to send frame: {}", e))
    }

    /// Handles MLME-SETKEYS.request (IEEE Std 802.11-2016, 6.3.19.1) from the SME.
    ///
    /// The MLME should set the keys on the PHY.
    pub fn handle_mlme_setkeys_request(
        &mut self,
        _ctx: &mut Context,
        _keylist: &[fidl_mlme::SetKeyDescriptor],
    ) -> Result<(), Error> {
        // TODO(37891): This should be removed from the MLME and handled in the SME.
        unimplemented!();
    }

    // WLAN frame handlers.

    /// Handles disassociation frames (IEEE Std 802.11-2016, 9.3.3.5) from the PHY.
    ///
    /// self is mutable here as receiving a disassociation immediately disassociates us.
    fn handle_disassoc_frame(
        &mut self,
        ctx: &mut Context,
        reason_code: ReasonCode,
    ) -> Result<(), failure::Error> {
        self.state = State::Authenticated;
        ctx.send_mlme_disassoc_ind(self.addr.clone(), reason_code.0)
            .map_err(|e| format_err!("failed to send frame: {}", e))
    }

    /// Handles association request frames (IEEE Std 802.11-2016, 9.3.3.6) from the PHY.
    fn handle_assoc_req_frame(
        &self,
        ctx: &mut Context,
        ssid: Option<Vec<u8>>,
        listen_interval: u16,
        rsn: Option<Vec<u8>>,
    ) -> Result<(), failure::Error> {
        ctx.send_mlme_assoc_ind(self.addr.clone(), listen_interval, ssid, rsn)
            .map_err(|e| format_err!("failed to send frame: {}", e))
    }

    /// Handles authentication frames (IEEE Std 802.11-2016, 9.3.3.12) from the PHY.
    ///
    /// self is mutable here as we may deauthenticate without even getting to the SME if we don't
    /// recognize the authentication algorithm.
    fn handle_auth_frame(
        &mut self,
        ctx: &mut Context,
        auth_alg_num: AuthAlgorithmNumber,
    ) -> Result<(), failure::Error> {
        ctx.send_mlme_auth_ind(
            self.addr.clone(),
            match auth_alg_num {
                AuthAlgorithmNumber::OPEN => fidl_mlme::AuthenticationTypes::OpenSystem,
                AuthAlgorithmNumber::SHARED_KEY => fidl_mlme::AuthenticationTypes::SharedKey,
                AuthAlgorithmNumber::FAST_BSS_TRANSITION => {
                    fidl_mlme::AuthenticationTypes::FastBssTransition
                }
                AuthAlgorithmNumber::SAE => fidl_mlme::AuthenticationTypes::Sae,
                _ => {
                    self.state = State::Deauthenticated;

                    // Don't even bother sending this to the SME if we don't understand the auth
                    // algorithm.
                    return ctx
                        .send_auth_frame(
                            self.addr.clone(),
                            auth_alg_num,
                            2,
                            StatusCode::UNSUPPORTED_AUTH_ALGORITHM,
                        )
                        .map_err(|e| format_err!("failed to send frame: {}", e));
                }
            },
        )
        .map_err(|e| format_err!("failed to send frame: {}", e))
    }

    /// Handles deauthentication frames (IEEE Std 802.11-2016, 9.3.3.13) from the PHY.
    ///
    /// self is mutable here as receiving a deauthentication immediately deauthenticates us.
    fn handle_deauth_frame(
        &mut self,
        ctx: &mut Context,
        reason_code: ReasonCode,
    ) -> Result<(), failure::Error> {
        self.state = State::Deauthenticated;
        ctx.send_mlme_deauth_ind(
            self.addr.clone(),
            fidl_mlme::ReasonCode::from_primitive(reason_code.0)
                .unwrap_or(fidl_mlme::ReasonCode::UnspecifiedReason),
        )
        .map_err(|e| format_err!("failed to send frame: {}", e))
    }

    /// Handles action frames (IEEE Std 802.11-2016, 9.3.3.14) from the PHY.
    fn handle_action_frame(&self, _ctx: &mut Context) -> Result<(), failure::Error> {
        // TODO(37891): Implement me!
        Ok(())
    }

    /// Handles PS-Poll (IEEE Std 802.11-2016, 9.3.1.5) from the PHY.
    fn handle_ps_poll(&self, _ctx: &mut Context) -> Result<(), failure::Error> {
        // TODO(37891): Implement me!
        unimplemented!()
    }

    /// Handles EAPoL requests (IEEE Std 802.1X-2010, 11.3) from PHY data frames.
    fn handle_eapol_llc_frame(
        &self,
        ctx: &mut Context,
        dst_addr: MacAddr,
        src_addr: MacAddr,
        body: &[u8],
    ) -> Result<(), failure::Error> {
        ctx.send_mlme_eapol_ind(dst_addr, src_addr, &body)
            .map_err(|e| format_err!("failed to send frame: {}", e))
    }

    // Handles LLC frames from PHY data frames.
    fn handle_llc_frame(
        &self,
        ctx: &mut Context,
        dst_addr: MacAddr,
        src_addr: MacAddr,
        ether_type: u16,
        body: &[u8],
    ) -> Result<(), failure::Error> {
        ctx.deliver_eth_frame(dst_addr, src_addr, ether_type, body)
            .map_err(|e| format_err!("failed to send frame: {}", e))
    }

    // Public handler functions.

    /// Handles management frames (IEEE Std 802.11-2016, 9.3.3) from the PHY.
    pub fn handle_mgmt_frame<B: ByteSlice>(
        &mut self,
        ctx: &mut Context,
        ssid: Option<Vec<u8>>,
        mgmt_hdr: mac::MgmtHdr,
        body: B,
    ) -> Result<(), failure::Error> {
        let mgmt_subtype = *&{ mgmt_hdr.frame_ctrl }.mgmt_subtype();

        if !self.is_frame_class_permitted(mac::frame_class(&{ mgmt_hdr.frame_ctrl })) {
            bail!("unpermitted management frame for subtype: {:?}", mgmt_subtype);
        }

        match mac::MgmtBody::parse(mgmt_subtype, body)
            .ok_or(format_err!("failed to parse management frame"))?
        {
            mac::MgmtBody::Authentication { auth_hdr, .. } => {
                self.handle_auth_frame(ctx, auth_hdr.auth_alg_num)
            }
            mac::MgmtBody::AssociationReq { assoc_req_hdr, .. } => {
                // TODO(tonyy): Support RSN from elements here.
                self.handle_assoc_req_frame(ctx, ssid, assoc_req_hdr.listen_interval, None)
            }
            mac::MgmtBody::Deauthentication { deauth_hdr, .. } => {
                self.handle_deauth_frame(ctx, deauth_hdr.reason_code)
            }
            mac::MgmtBody::Disassociation { disassoc_hdr, .. } => {
                self.handle_disassoc_frame(ctx, disassoc_hdr.reason_code)
            }
            mac::MgmtBody::Action { action_hdr: _, .. } => self.handle_action_frame(ctx),
            _ => bail!("unknown management frame: {:?}", mgmt_subtype),
        }
    }

    /// Handles data frames (IEEE Std 802.11-2016, 9.3.2) from the PHY.
    ///
    /// These data frames may be in A-MSDU format (IEEE Std 802.11-2016, 9.3.2.2). However, the
    /// individual frames will be passed to |handle_msdu| and we don't need to care what format
    /// they're in.
    pub fn handle_data_frame<B: ByteSlice>(
        &self,
        ctx: &mut Context,
        fixed_data_fields: mac::FixedDataHdrFields,
        addr4: Option<mac::Addr4>,
        qos_ctrl: Option<mac::QosControl>,
        body: B,
    ) -> Result<(), failure::Error> {
        if !self.is_frame_class_permitted(mac::frame_class(&{ fixed_data_fields.frame_ctrl })) {
            bail!("unpermitted data frame");
        }

        for msdu in
            mac::MsduIterator::from_data_frame_parts(fixed_data_fields, addr4, qos_ctrl, body)
        {
            let mac::Msdu { dst_addr, src_addr, llc_frame } = &msdu;
            match llc_frame.hdr.protocol_id.to_native() {
                mac::ETHER_TYPE_EAPOL => {
                    self.handle_eapol_llc_frame(ctx, *dst_addr, *src_addr, &llc_frame.body)?;
                }
                // Disallow handling LLC frames if the controlled port is closed. If there is no
                // controlled port, sending frames is OK.
                _ if match self.state {
                    State::Associated {
                        eapol_controlled_port: Some(fidl_mlme::ControlledPortState::Closed),
                        ..
                    } => false,
                    _ => true,
                } =>
                {
                    self.handle_llc_frame(
                        ctx,
                        *dst_addr,
                        *src_addr,
                        llc_frame.hdr.protocol_id.to_native(),
                        &llc_frame.body,
                    )?
                }
                // Drop all non-EAPoL MSDUs if the controlled port is closed.
                _ => (),
            }
        }
        Ok(())
    }

    /// Handles Ethernet II frames from the netstack.
    pub fn handle_eth_frame(
        &self,
        ctx: &mut Context,
        dst_addr: MacAddr,
        src_addr: MacAddr,
        ether_type: u16,
        body: &[u8],
    ) -> Result<(), failure::Error> {
        let eapol_controlled_port = match self.state {
            State::Associated { eapol_controlled_port, .. } => eapol_controlled_port,
            _ => bail!("client is not associated"),
        };

        let protection = match eapol_controlled_port {
            None => false,
            Some(fidl_mlme::ControlledPortState::Open) => true,
            Some(fidl_mlme::ControlledPortState::Closed) => {
                bail!("EAPoL controlled port is closed");
            }
        };

        ctx.send_data_frame(
            dst_addr, src_addr, protection, false, // TODO(37891): Support QoS.
            ether_type, body,
        )
        .map_err(|e| format_err!("failed to send frame: {}", e))
    }
}

// TODO(37891): Test handle_mlme_setkeys_request.
#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{
            buffer::FakeBufferProvider,
            device::{Device, FakeDevice},
        },
        wlan_common::{
            assert_variant,
            mac::{Bssid, CapabilityInfo},
            test_utils::fake_frames::*,
        },
    };

    const CLIENT_ADDR: MacAddr = [1; 6];
    const AP_ADDR: Bssid = Bssid([2; 6]);
    const CLIENT_ADDR2: MacAddr = [3; 6];

    fn make_remote_client() -> RemoteClient {
        RemoteClient::new(CLIENT_ADDR)
    }

    fn make_context(device: Device) -> Context {
        Context::new(device, FakeBufferProvider::new(), AP_ADDR)
    }

    #[test]
    fn handle_mlme_auth_resp() {
        let mut fake_device = FakeDevice::new();
        let mut r_sta = make_remote_client();
        let mut ctx = make_context(fake_device.as_device());
        r_sta
            .handle_mlme_auth_resp(&mut ctx, fidl_mlme::AuthenticateResultCodes::Success)
            .expect("expected OK");
        assert_eq!(r_sta.state, State::Authenticated);
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
            0, 0, // auth algorithm
            2, 0, // auth txn seq num
            0, 0, // status code
        ][..]);
    }

    #[test]
    fn handle_mlme_auth_resp_failure() {
        let mut fake_device = FakeDevice::new();
        let mut r_sta = make_remote_client();
        let mut ctx = make_context(fake_device.as_device());
        r_sta
            .handle_mlme_auth_resp(
                &mut ctx,
                fidl_mlme::AuthenticateResultCodes::AntiCloggingTokenRequired,
            )
            .expect("expected OK");
        assert_eq!(r_sta.state, State::Deauthenticated);
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
            0, 0, // auth algorithm
            2, 0, // auth txn seq num
            76, 0, // status code
        ][..]);
    }

    #[test]
    fn handle_mlme_deauth_req() {
        let mut fake_device = FakeDevice::new();
        let mut r_sta = make_remote_client();
        let mut ctx = make_context(fake_device.as_device());
        r_sta
            .handle_mlme_deauth_req(&mut ctx, fidl_mlme::ReasonCode::LeavingNetworkDeauth)
            .expect("expected OK");
        assert_eq!(r_sta.state, State::Deauthenticated);
        assert_eq!(fake_device.wlan_queue.len(), 1);
        #[rustfmt::skip]
        assert_eq!(&fake_device.wlan_queue[0].0[..], &[
            // Mgmt header
            0b11000000, 0, // Frame Control
            0, 0, // Duration
            1, 1, 1, 1, 1, 1, // addr1
            2, 2, 2, 2, 2, 2, // addr2
            2, 2, 2, 2, 2, 2, // addr3
            0x10, 0, // Sequence Control
            // Deauth header:
            3, 0, // reason code
        ][..]);
    }

    #[test]
    fn handle_mlme_assoc_resp() {
        let mut fake_device = FakeDevice::new();
        let mut r_sta = make_remote_client();
        let mut ctx = make_context(fake_device.as_device());
        r_sta
            .handle_mlme_assoc_resp(
                &mut ctx,
                true,
                CapabilityInfo(0),
                fidl_mlme::AssociateResultCodes::Success,
                1,
                &[0, 4, 1, 2, 3, 4][..],
            )
            .expect("expected OK");
        assert_variant!(r_sta.state, State::Associated {
            eapol_controlled_port: Some(fidl_mlme::ControlledPortState::Closed), ..
        });
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
            0, 4, 1, 2, 3, 4 // SSID
        ][..]);
    }

    #[test]
    fn handle_mlme_assoc_resp_no_rsn() {
        let mut fake_device = FakeDevice::new();
        let mut r_sta = make_remote_client();
        let mut ctx = make_context(fake_device.as_device());
        r_sta
            .handle_mlme_assoc_resp(
                &mut ctx,
                false,
                CapabilityInfo(0),
                fidl_mlme::AssociateResultCodes::Success,
                1,
                &[0, 4, 1, 2, 3, 4][..],
            )
            .expect("expected OK");
        assert_variant!(r_sta.state, State::Associated {
            eapol_controlled_port: None, ..
        });
    }

    #[test]
    fn handle_mlme_assoc_resp_failure() {
        let mut fake_device = FakeDevice::new();
        let mut r_sta = make_remote_client();
        let mut ctx = make_context(fake_device.as_device());
        r_sta
            .handle_mlme_assoc_resp(
                &mut ctx,
                false,
                CapabilityInfo(0),
                fidl_mlme::AssociateResultCodes::RejectedEmergencyServicesNotSupported,
                1,
                &[0, 4, 1, 2, 3, 4][..],
            )
            .expect("expected OK");
        assert_eq!(r_sta.state, State::Authenticated);
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
            0, 4, 1, 2, 3, 4 // SSID
        ][..]);
    }

    #[test]
    fn handle_mlme_disassoc_req() {
        let mut fake_device = FakeDevice::new();
        let mut r_sta = make_remote_client();
        let mut ctx = make_context(fake_device.as_device());
        r_sta
            .handle_mlme_disassoc_req(
                &mut ctx,
                fidl_mlme::ReasonCode::LeavingNetworkDisassoc as u16,
            )
            .expect("expected OK");
        assert_eq!(r_sta.state, State::Authenticated);
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
    fn handle_mlme_set_controlled_port_req() {
        let mut r_sta = make_remote_client();
        r_sta.state = State::Associated {
            eapol_controlled_port: Some(fidl_mlme::ControlledPortState::Closed),
        };
        r_sta
            .handle_mlme_set_controlled_port_req(fidl_mlme::ControlledPortState::Open)
            .expect("expected OK");
        assert_variant!(r_sta.state, State::Associated {
            eapol_controlled_port: Some(fidl_mlme::ControlledPortState::Open),
            ..
        });
    }

    #[test]
    fn handle_mlme_set_controlled_port_req_closed() {
        let mut r_sta = make_remote_client();
        r_sta.state =
            State::Associated { eapol_controlled_port: Some(fidl_mlme::ControlledPortState::Open) };
        r_sta
            .handle_mlme_set_controlled_port_req(fidl_mlme::ControlledPortState::Closed)
            .expect("expected OK");
        assert_variant!(r_sta.state, State::Associated {
            eapol_controlled_port: Some(fidl_mlme::ControlledPortState::Closed),
            ..
        });
    }

    #[test]
    fn handle_mlme_set_controlled_port_req_no_rsn() {
        let mut r_sta = make_remote_client();
        r_sta.state = State::Associated { eapol_controlled_port: None };
        r_sta
            .handle_mlme_set_controlled_port_req(fidl_mlme::ControlledPortState::Open)
            .expect_err("expected err");
        assert_variant!(r_sta.state, State::Associated {
            eapol_controlled_port: None,
            ..
        });
    }

    #[test]
    fn handle_mlme_set_controlled_port_req_wrong_state() {
        let mut r_sta = make_remote_client();
        r_sta.state = State::Authenticating;
        r_sta
            .handle_mlme_set_controlled_port_req(fidl_mlme::ControlledPortState::Open)
            .expect_err("expected err");
    }

    #[test]
    fn handle_mlme_eapol_req() {
        let mut fake_device = FakeDevice::new();
        let r_sta = make_remote_client();
        let mut ctx = make_context(fake_device.as_device());
        r_sta.handle_mlme_eapol_req(&mut ctx, &[1, 2, 3][..]).expect("expected OK");
        assert_eq!(fake_device.wlan_queue.len(), 1);
        #[rustfmt::skip]
        assert_eq!(&fake_device.wlan_queue[0].0[..], &[
            // Mgmt header
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
        ][..]);
    }

    #[test]
    fn handle_mlme_setkeys_req() {
        // TODO(37891): Implement me!
    }

    #[test]
    fn handle_disassoc_frame() {
        let mut fake_device = FakeDevice::new();
        let mut r_sta = make_remote_client();
        let mut ctx = make_context(fake_device.as_device());
        r_sta
            .handle_disassoc_frame(
                &mut ctx,
                ReasonCode(fidl_mlme::ReasonCode::LeavingNetworkDisassoc as u16),
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
        assert_eq!(r_sta.state, State::Authenticated);
    }

    #[test]
    fn handle_assoc_req_frame() {
        let mut fake_device = FakeDevice::new();
        let r_sta = make_remote_client();
        let mut ctx = make_context(fake_device.as_device());
        r_sta
            .handle_assoc_req_frame(&mut ctx, Some(b"coolnet".to_vec()), 1, None)
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
    fn handle_auth_frame() {
        let mut fake_device = FakeDevice::new();
        let mut r_sta = make_remote_client();
        let mut ctx = make_context(fake_device.as_device());

        r_sta.handle_auth_frame(&mut ctx, AuthAlgorithmNumber::SHARED_KEY).expect("expected OK");
        let msg = fake_device
            .next_mlme_msg::<fidl_mlme::AuthenticateIndication>()
            .expect("expected MLME message");
        assert_eq!(
            msg,
            fidl_mlme::AuthenticateIndication {
                peer_sta_address: CLIENT_ADDR,
                auth_type: fidl_mlme::AuthenticationTypes::SharedKey,
            },
        );
    }

    #[test]
    fn handle_auth_frame_unknown_algorithm() {
        let mut fake_device = FakeDevice::new();
        let mut r_sta = make_remote_client();
        let mut ctx = make_context(fake_device.as_device());

        r_sta.handle_auth_frame(&mut ctx, AuthAlgorithmNumber(0xffff)).expect("expected OK");
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
            0xff, 0xff, // auth algorithm
            2, 0, // auth txn seq num
            13, 0, // status code
        ][..]);
        assert_eq!(r_sta.state, State::Deauthenticated);
    }

    #[test]
    fn handle_deauth_frame() {
        let mut fake_device = FakeDevice::new();
        let mut r_sta = make_remote_client();
        let mut ctx = make_context(fake_device.as_device());

        r_sta
            .handle_deauth_frame(
                &mut ctx,
                ReasonCode(fidl_mlme::ReasonCode::LeavingNetworkDeauth as u16),
            )
            .expect("expected OK");
        let msg = fake_device
            .next_mlme_msg::<fidl_mlme::DeauthenticateIndication>()
            .expect("expected MLME message");
        assert_eq!(
            msg,
            fidl_mlme::DeauthenticateIndication {
                peer_sta_address: CLIENT_ADDR,
                reason_code: fidl_mlme::ReasonCode::LeavingNetworkDeauth,
            }
        );
        assert_eq!(r_sta.state, State::Deauthenticated);
    }

    #[test]
    fn handle_action_frame() {
        // TODO(37891): Implement me!
    }

    #[test]
    fn handle_ps_poll() {
        // TODO(37891): Implement me!
    }

    #[test]
    fn handle_eapol_llc_frame() {
        let mut fake_device = FakeDevice::new();
        let mut r_sta = make_remote_client();
        let mut ctx = make_context(fake_device.as_device());

        r_sta.state = State::Associated { eapol_controlled_port: None };
        r_sta
            .handle_eapol_llc_frame(&mut ctx, CLIENT_ADDR2, CLIENT_ADDR, &[1, 2, 3, 4, 5][..])
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
    fn handle_llc_frame() {
        let mut fake_device = FakeDevice::new();
        let mut r_sta = make_remote_client();
        let mut ctx = make_context(fake_device.as_device());

        r_sta.state = State::Associated { eapol_controlled_port: None };
        r_sta
            .handle_llc_frame(&mut ctx, CLIENT_ADDR2, CLIENT_ADDR, 0x1234, &[1, 2, 3, 4, 5][..])
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
    fn handle_eth_frame_no_eapol_controlled_port() {
        let mut fake_device = FakeDevice::new();
        let mut r_sta = make_remote_client();
        let mut ctx = make_context(fake_device.as_device());

        r_sta.state = State::Associated { eapol_controlled_port: None };
        r_sta
            .handle_eth_frame(&mut ctx, CLIENT_ADDR2, CLIENT_ADDR, 0x1234, &[1, 2, 3, 4, 5][..])
            .expect("expected OK");
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
    fn handle_eth_frame_not_associated() {
        let mut fake_device = FakeDevice::new();
        let mut r_sta = make_remote_client();
        let mut ctx = make_context(fake_device.as_device());

        r_sta.state = State::Authenticated;
        r_sta
            .handle_eth_frame(&mut ctx, CLIENT_ADDR2, CLIENT_ADDR, 0x1234, &[1, 2, 3, 4, 5][..])
            .expect_err("expected error");
    }

    #[test]
    fn handle_eth_frame_eapol_controlled_port_closed() {
        let mut fake_device = FakeDevice::new();
        let mut r_sta = make_remote_client();
        let mut ctx = make_context(fake_device.as_device());

        r_sta.state = State::Associated {
            eapol_controlled_port: Some(fidl_mlme::ControlledPortState::Closed),
        };
        r_sta
            .handle_eth_frame(&mut ctx, CLIENT_ADDR2, CLIENT_ADDR, 0x1234, &[1, 2, 3, 4, 5][..])
            .expect_err("expected error");
    }

    #[test]
    fn handle_eth_frame_eapol_controlled_port_open() {
        let mut fake_device = FakeDevice::new();
        let mut r_sta = make_remote_client();
        let mut ctx = make_context(fake_device.as_device());

        r_sta.state =
            State::Associated { eapol_controlled_port: Some(fidl_mlme::ControlledPortState::Open) };
        r_sta
            .handle_eth_frame(&mut ctx, CLIENT_ADDR2, CLIENT_ADDR, 0x1234, &[1, 2, 3, 4, 5][..])
            .expect("expected OK");
        assert_eq!(fake_device.wlan_queue.len(), 1);
        #[rustfmt::skip]
        assert_eq!(&fake_device.wlan_queue[0].0[..], &[
            // Mgmt header
            0b00001000, 0b01000010, // Frame Control
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
    fn handle_data_frame_not_permitted() {
        let mut fake_device = FakeDevice::new();
        let mut r_sta = make_remote_client();
        r_sta.state = State::Authenticating;
        let mut ctx = make_context(fake_device.as_device());

        r_sta
            .handle_data_frame(
                &mut ctx,
                mac::FixedDataHdrFields {
                    frame_ctrl: mac::FrameControl(0),
                    duration: 0,
                    addr1: CLIENT_ADDR,
                    addr2: AP_ADDR.0.clone(),
                    addr3: CLIENT_ADDR2,
                    seq_ctrl: mac::SequenceControl(10),
                },
                None,
                None,
                &[
                    7, 7, 7, // DSAP, SSAP & control
                    8, 8, 8, // OUI
                    9, 10, // eth type
                    // Trailing bytes
                    11, 11, 11,
                ][..],
            )
            .expect_err("expected err");
    }

    #[test]
    fn handle_data_frame_single_llc() {
        let mut fake_device = FakeDevice::new();
        let mut r_sta = make_remote_client();
        r_sta.state = State::Associated { eapol_controlled_port: None };
        let mut ctx = make_context(fake_device.as_device());

        r_sta
            .handle_data_frame(
                &mut ctx,
                mac::FixedDataHdrFields {
                    frame_ctrl: mac::FrameControl(0),
                    duration: 0,
                    addr1: CLIENT_ADDR,
                    addr2: AP_ADDR.0.clone(),
                    addr3: CLIENT_ADDR2,
                    seq_ctrl: mac::SequenceControl(10),
                },
                None,
                None,
                &[
                    7, 7, 7, // DSAP, SSAP & control
                    8, 8, 8, // OUI
                    9, 10, // eth type
                    // Trailing bytes
                    11, 11, 11,
                ][..],
            )
            .expect("expected OK");

        assert_eq!(fake_device.eth_queue.len(), 1);
    }

    #[test]
    fn handle_data_frame_amsdu() {
        let mut fake_device = FakeDevice::new();
        let mut r_sta = make_remote_client();
        r_sta.state = State::Associated { eapol_controlled_port: None };
        let mut ctx = make_context(fake_device.as_device());

        let mut amsdu_data_frame_body = vec![];
        amsdu_data_frame_body.extend(&[
            // A-MSDU Subframe #1
            0x78, 0x8a, 0x20, 0x0d, 0x67, 0x03, // dst_addr
            0xb4, 0xf7, 0xa1, 0xbe, 0xb9, 0xab, // src_addr
            0x00, 0x74, // MSDU length
        ]);
        amsdu_data_frame_body.extend(MSDU_1_LLC_HDR);
        amsdu_data_frame_body.extend(MSDU_1_PAYLOAD);
        amsdu_data_frame_body.extend(&[
            // Padding
            0x00, 0x00, // A-MSDU Subframe #2
            0x78, 0x8a, 0x20, 0x0d, 0x67, 0x04, // dst_addr
            0xb4, 0xf7, 0xa1, 0xbe, 0xb9, 0xac, // src_addr
            0x00, 0x66, // MSDU length
        ]);
        amsdu_data_frame_body.extend(MSDU_2_LLC_HDR);
        amsdu_data_frame_body.extend(MSDU_2_PAYLOAD);

        r_sta
            .handle_data_frame(
                &mut ctx,
                mac::FixedDataHdrFields {
                    frame_ctrl: mac::FrameControl(0),
                    duration: 0,
                    addr1: CLIENT_ADDR,
                    addr2: AP_ADDR.0.clone(),
                    addr3: CLIENT_ADDR2,
                    seq_ctrl: mac::SequenceControl(10),
                },
                None,
                Some(mac::QosControl(0).with_amsdu_present(true)),
                &amsdu_data_frame_body[..],
            )
            .expect("expected OK");

        assert_eq!(fake_device.eth_queue.len(), 2);
    }

    #[test]
    fn handle_mgmt_frame() {
        let mut fake_device = FakeDevice::new();
        let mut r_sta = make_remote_client();
        r_sta.state = State::Authenticating;
        let mut ctx = make_context(fake_device.as_device());

        r_sta
            .handle_mgmt_frame(
                &mut ctx,
                None,
                mac::MgmtHdr {
                    frame_ctrl: mac::FrameControl(0b00000000_10110000), // Auth frame
                    duration: 0,
                    addr1: [1; 6],
                    addr2: [2; 6],
                    addr3: [3; 6],
                    seq_ctrl: mac::SequenceControl(10),
                },
                &[
                    0, 0, // Auth algorithm number
                    1, 0, // Auth txn seq number
                    0, 0, // Status code
                ][..],
            )
            .expect("expected OK");
    }

    #[test]
    fn handle_mgmt_frame_not_permitted() {
        let mut fake_device = FakeDevice::new();
        let mut r_sta = make_remote_client();
        r_sta.state = State::Authenticating;
        let mut ctx = make_context(fake_device.as_device());

        r_sta
            .handle_mgmt_frame(
                &mut ctx,
                None,
                mac::MgmtHdr {
                    frame_ctrl: mac::FrameControl(0b00000000_00000000), // Assoc req frame
                    duration: 0,
                    addr1: [1; 6],
                    addr2: [2; 6],
                    addr3: [3; 6],
                    seq_ctrl: mac::SequenceControl(10),
                },
                &[
                    0, 0, // Capability info
                    10, 0, // Listen interval
                ][..],
            )
            .expect_err("expected error");
    }

    #[test]
    fn handle_mgmt_frame_not_handled() {
        let mut fake_device = FakeDevice::new();
        let mut r_sta = make_remote_client();
        r_sta.state = State::Authenticating;
        let mut ctx = make_context(fake_device.as_device());

        r_sta
            .handle_mgmt_frame(
                &mut ctx,
                None,
                mac::MgmtHdr {
                    frame_ctrl: mac::FrameControl(0b00000000_00010000), // Assoc resp frame
                    duration: 0,
                    addr1: [1; 6],
                    addr2: [2; 6],
                    addr3: [3; 6],
                    seq_ctrl: mac::SequenceControl(10),
                },
                &[
                    0, 0, // Capability info
                    0, 0, // Status code
                    1, 0, // AID
                ][..],
            )
            .expect_err("expected error");
    }
}
