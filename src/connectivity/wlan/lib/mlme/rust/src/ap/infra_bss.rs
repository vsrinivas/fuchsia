// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        ap::{
            frame_writer::{write_beacon_frame, BeaconOffloadParams},
            remote_client::{ClientRejection, RemoteClient},
            Context, Rejection, TimedEvent,
        },
        error::Error,
        key::KeyConfig,
        timer::EventId,
    },
    banjo_ddk_hw_wlan_wlaninfo::WlanInfoDriverFeature,
    banjo_ddk_protocol_wlan_info::{WlanChannel, WlanChannelBandwidth},
    failure::format_err,
    fidl_fuchsia_wlan_mlme as fidl_mlme, fuchsia_zircon as zx,
    std::collections::HashMap,
    wlan_common::{
        appendable::Appendable,
        ie,
        mac::{self, CapabilityInfo, MacAddr},
        TimeUnit,
    },
    zerocopy::ByteSlice,
};

pub struct InfraBss {
    pub ssid: Vec<u8>,
    pub rsne: Option<Vec<u8>>,
    pub beacon_interval: TimeUnit,
    pub capabilities: CapabilityInfo,
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
    pub fn new(
        ctx: &mut Context,
        ssid: Vec<u8>,
        beacon_interval: TimeUnit,
        capabilities: CapabilityInfo,
        rates: Vec<u8>,
        channel: u8,
        rsne: Option<Vec<u8>>,
    ) -> Result<Self, Error> {
        let bss = Self {
            ssid,
            rsne,
            beacon_interval,
            rates,
            capabilities,
            channel,
            clients: HashMap::new(),
        };

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
        let mut buf = vec![];
        let beacon_offload_params = bss.write_beacon_template(ctx, &mut buf)?;
        ctx.device
            .enable_beaconing(&buf, beacon_offload_params.tim_ele_offset, beacon_interval)
            .map_err(|s| Error::Status(format!("failed to enable beaconing"), s))?;

        Ok(bss)
    }

    pub fn stop(&self, ctx: &mut Context) -> Result<(), Error> {
        ctx.device
            .disable_beaconing()
            .map_err(|s| Error::Status(format!("failed to disable beaconing"), s))
    }

    pub fn handle_mlme_setkeys_req(
        &mut self,
        ctx: &mut Context,
        keylist: &[fidl_mlme::SetKeyDescriptor],
    ) -> Result<(), Error> {
        if self.rsne.is_none() {
            return Err(Error::Status(
                format!("cannot set keys for an unprotected BSS"),
                zx::Status::BAD_STATE,
            ));
        }

        for key_desc in keylist {
            ctx.device
                .set_key(KeyConfig::from(key_desc))
                .map_err(|s| Error::Status(format!("failed to set keys on PHY"), s))?;
        }
        Ok(())
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
                self.channel,
                CapabilityInfo(resp.cap),
                resp.result_code,
                resp.association_id,
                &resp.rates,
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

    fn write_beacon_template<B: Appendable>(
        &self,
        ctx: &Context,
        buf: &mut B,
    ) -> Result<BeaconOffloadParams, Error> {
        write_beacon_frame(
            buf,
            ctx.bssid,
            0,
            self.beacon_interval,
            self.capabilities,
            &self.ssid,
            &self.rates,
            self.channel,
            self.rsne.as_ref().map_or(&[], |rsne| &rsne),
        )
    }

    fn handle_probe_req(
        &mut self,
        ctx: &mut Context,
        client_addr: MacAddr,
    ) -> Result<(), Rejection> {
        // According to IEEE Std 802.11-2016, 11.1.4.1, we should intersect our IEs with the probe
        // request IEs. However, the client is able to do this anyway so we just send the same IEs
        // as we would with a beacon frame.
        ctx.send_probe_resp_frame(
            client_addr,
            0,
            self.beacon_interval,
            self.capabilities,
            &self.ssid,
            &self.rates,
            self.channel,
            self.rsne.as_ref().map_or(&[], |rsne| &rsne),
        )
        .map_err(|e| Rejection::Client(client_addr, ClientRejection::WlanSendError(e)))
    }

    pub fn handle_mgmt_frame<B: ByteSlice>(
        &mut self,
        ctx: &mut Context,
        mgmt_hdr: mac::MgmtHdr,
        body: B,
    ) -> Result<(), Rejection> {
        if *&{ mgmt_hdr.frame_ctrl }.to_ds() || *&{ mgmt_hdr.frame_ctrl }.from_ds() {
            // IEEE Std 802.11-2016, 9.2.4.1.4 and Table 9-4: The To DS bit is only set for QMF
            // (QoS Management frame) management frames, and the From DS bit is reserved.
            return Err(Rejection::BadDsBits);
        }

        let to_bss = mgmt_hdr.addr1 == ctx.bssid.0 && mgmt_hdr.addr3 == ctx.bssid.0;
        let client_addr = mgmt_hdr.addr2;

        let mgmt_subtype = *&{ mgmt_hdr.frame_ctrl }.mgmt_subtype();
        if mgmt_subtype == mac::MgmtSubtype::PROBE_REQ {
            let driver_features = ctx.device.wlan_info().ifc_info.driver_features;
            if (driver_features & WlanInfoDriverFeature::PROBE_RESP_OFFLOAD).0 > 0 {
                // We expected the probe response to be handled by hardware.
                return Err(Rejection::Error(format_err!(
                    "driver indicates probe response offload but MLME received a probe response!"
                )));
            }

            if to_bss || (mgmt_hdr.addr1 == mac::BCAST_ADDR && mgmt_hdr.addr3 == mac::BCAST_ADDR) {
                // Allow either probe request sent directly to the AP, or ones that are broadcast.
                for (id, ie_body) in ie::Reader::new(&body[..]) {
                    match id {
                        ie::Id::SSID => {
                            if ie_body != &[][..] && ie_body != &self.ssid[..] {
                                // Frame is not for this BSS.
                                return Err(Rejection::OtherBss);
                            }
                        }
                        _ => {}
                    }
                }

                // Technically, the probe request must contain an SSID IE (IEEE Std 802.11-2016,
                // 11.1.4.1), but we just treat it here as the same as it being an empty SSID.
                return self.handle_probe_req(ctx, client_addr);
            } else {
                // Frame is not for this BSS.
                return Err(Rejection::OtherBss);
            }
        } else if !to_bss {
            // Frame is not for this BSS.
            return Err(Rejection::OtherBss);
        }

        // We might allocate a client into the Option if there is none present in the map. We do not
        // allocate directly into the map as we do not know yet if the client will even be added
        // (e.g. if the frame being handled is bogus, or the client did not even authenticate).
        let mut new_client = None;
        let client = match self.clients.get_mut(&client_addr) {
            Some(client) => client,
            None => new_client.get_or_insert(RemoteClient::new(client_addr)),
        };

        if let Err(e) = client.handle_mgmt_frame(
            ctx,
            self.capabilities,
            Some(self.ssid.clone()),
            mgmt_hdr,
            body,
        ) {
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
    pub fn handle_data_frame<B: ByteSlice>(
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

    pub fn handle_eth_frame(&mut self, ctx: &mut Context, frame: &[u8]) -> Result<(), Rejection> {
        let mac::EthernetFrame { hdr, body } =
            mac::EthernetFrame::parse(frame).ok_or_else(|| Rejection::FrameMalformed)?;

        // Handle the frame, pretending that the client is an unauthenticated client if we don't
        // know about it.
        let mut maybe_client = None;
        let client = self
            .clients
            .get_mut(&hdr.da)
            .unwrap_or_else(|| maybe_client.get_or_insert(RemoteClient::new(hdr.da)));
        client
            .handle_eth_frame(ctx, hdr.da, hdr.sa, hdr.ether_type.to_native(), body)
            .map_err(|e| Rejection::Client(client.addr, e))
    }

    // Timed event functions

    /// Handles timed events.
    pub fn handle_timed_event(
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

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{
            ap::remote_client::{ClientEvent, ClientRejection},
            buffer::FakeBufferProvider,
            device::{Device, FakeDevice},
            key::{KeyType, Protection},
            timer::{FakeScheduler, Scheduler, Timer},
        },
        wlan_common::{
            assert_variant,
            mac::{Bssid, CapabilityInfo},
            test_utils::fake_frames::fake_wpa2_rsne,
        },
    };

    const CLIENT_ADDR: MacAddr = [1u8; 6];
    const BSSID: Bssid = Bssid([2u8; 6]);
    const CLIENT_ADDR2: MacAddr = [3u8; 6];

    fn make_context(device: Device, scheduler: Scheduler) -> Context {
        Context::new(device, FakeBufferProvider::new(), Timer::<TimedEvent>::new(scheduler), BSSID)
    }

    fn make_eth_frame(
        dst_addr: MacAddr,
        src_addr: MacAddr,
        protocol_id: u16,
        body: &[u8],
    ) -> Vec<u8> {
        let mut buf = vec![];
        crate::write_eth_frame(&mut buf, dst_addr, src_addr, protocol_id, body)
            .expect("writing to vec always succeeds");
        buf
    }

    #[test]
    fn new() {
        let mut fake_device = FakeDevice::new();
        let mut fake_scheduler = FakeScheduler::new();
        let mut ctx = make_context(fake_device.as_device(), fake_scheduler.as_scheduler());
        InfraBss::new(
            &mut ctx,
            vec![1, 2, 3, 4, 5],
            TimeUnit::DEFAULT_BEACON_INTERVAL,
            CapabilityInfo(0).with_ess(true),
            vec![0b11111000],
            1,
            None,
        )
        .expect("expected InfraBss::new ok");

        assert_eq!(
            fake_device.wlan_channel,
            WlanChannel { primary: 1, cbw: WlanChannelBandwidth::_20, secondary80: 0 }
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
            5, 4, 0, 0, 0, 0, // TIM
        ];

        assert_eq!(
            fake_device.bcn_cfg.expect("expected bcn_cfg"),
            (beacon_tmpl, 49, TimeUnit::DEFAULT_BEACON_INTERVAL)
        );
    }

    #[test]
    fn stop() {
        let mut fake_device = FakeDevice::new();
        let mut fake_scheduler = FakeScheduler::new();
        let mut ctx = make_context(fake_device.as_device(), fake_scheduler.as_scheduler());
        let bss = InfraBss::new(
            &mut ctx,
            vec![1, 2, 3, 4, 5],
            TimeUnit::DEFAULT_BEACON_INTERVAL,
            CapabilityInfo(0),
            vec![0b11111000],
            1,
            None,
        )
        .expect("expected InfraBss::new ok");
        bss.stop(&mut ctx).expect("expected InfraBss::stop ok");
        assert!(fake_device.bcn_cfg.is_none());
    }

    #[test]
    fn handle_mlme_auth_resp() {
        let mut fake_device = FakeDevice::new();
        let mut fake_scheduler = FakeScheduler::new();
        let mut ctx = make_context(fake_device.as_device(), fake_scheduler.as_scheduler());
        let mut bss = InfraBss::new(
            &mut ctx,
            b"coolnet".to_vec(),
            TimeUnit::DEFAULT_BEACON_INTERVAL,
            CapabilityInfo(0),
            vec![0b11111000],
            1,
            None,
        )
        .expect("expected InfraBss::new ok");

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
    fn handle_mlme_auth_resp_no_such_client() {
        let mut fake_device = FakeDevice::new();
        let mut fake_scheduler = FakeScheduler::new();
        let mut ctx = make_context(fake_device.as_device(), fake_scheduler.as_scheduler());
        let mut bss = InfraBss::new(
            &mut ctx,
            b"coolnet".to_vec(),
            TimeUnit::DEFAULT_BEACON_INTERVAL,
            CapabilityInfo(0),
            vec![0b11111000],
            1,
            None,
        )
        .expect("expected InfraBss::new ok");

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
    fn handle_mlme_deauth_req() {
        let mut fake_device = FakeDevice::new();
        let mut fake_scheduler = FakeScheduler::new();
        let mut ctx = make_context(fake_device.as_device(), fake_scheduler.as_scheduler());
        let mut bss = InfraBss::new(
            &mut ctx,
            b"coolnet".to_vec(),
            TimeUnit::DEFAULT_BEACON_INTERVAL,
            CapabilityInfo(0),
            vec![0b11111000],
            1,
            None,
        )
        .expect("expected InfraBss::new ok");

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
    fn handle_mlme_assoc_resp() {
        let mut fake_device = FakeDevice::new();
        let mut fake_scheduler = FakeScheduler::new();
        let mut ctx = make_context(fake_device.as_device(), fake_scheduler.as_scheduler());
        let mut bss = InfraBss::new(
            &mut ctx,
            b"coolnet".to_vec(),
            TimeUnit::DEFAULT_BEACON_INTERVAL,
            CapabilityInfo(0),
            vec![0b11111000],
            1,
            None,
        )
        .expect("expected InfraBss::new ok");

        bss.clients.insert(CLIENT_ADDR, RemoteClient::new(CLIENT_ADDR));

        bss.handle_mlme_assoc_resp(
            &mut ctx,
            fidl_mlme::AssociateResponse {
                peer_sta_address: CLIENT_ADDR,
                result_code: fidl_mlme::AssociateResultCodes::Success,
                association_id: 1,
                cap: 0,
                rates: vec![1, 2, 3, 4, 5, 6, 7, 8, 9, 10],
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
                // IEs
                1, 8, 1, 2, 3, 4, 5, 6, 7, 8, // Rates
                50, 2, 9, 10, // Extended rates
                90, 3, 90, 0, 0, // BSS max idle period
            ][..]
        );
        assert!(fake_device.assocs.contains_key(&CLIENT_ADDR));
    }

    #[test]
    fn handle_mlme_assoc_resp_with_caps() {
        let mut fake_device = FakeDevice::new();
        let mut fake_scheduler = FakeScheduler::new();
        let mut ctx = make_context(fake_device.as_device(), fake_scheduler.as_scheduler());
        let mut bss = InfraBss::new(
            &mut ctx,
            b"coolnet".to_vec(),
            TimeUnit::DEFAULT_BEACON_INTERVAL,
            CapabilityInfo(0).with_short_preamble(true).with_ess(true),
            vec![0b11111000],
            1,
            None,
        )
        .expect("expected InfraBss::new ok");

        bss.clients.insert(CLIENT_ADDR, RemoteClient::new(CLIENT_ADDR));

        bss.handle_mlme_assoc_resp(
            &mut ctx,
            fidl_mlme::AssociateResponse {
                peer_sta_address: CLIENT_ADDR,
                result_code: fidl_mlme::AssociateResultCodes::Success,
                association_id: 1,
                cap: CapabilityInfo(0).with_short_preamble(true).raw(),
                rates: vec![1, 2, 3, 4, 5, 6, 7, 8, 9, 10],
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
                0b00100000, 0b00000000, // Capabilities
                0, 0, // status code
                1, 0, // AID
                // IEs
                1, 8, 1, 2, 3, 4, 5, 6, 7, 8, // Rates
                50, 2, 9, 10, // Extended rates
                90, 3, 90, 0, 0, // BSS max idle period
            ][..]
        );
        assert!(fake_device.assocs.contains_key(&CLIENT_ADDR));
    }

    #[test]
    fn handle_mlme_disassoc_req() {
        let mut fake_device = FakeDevice::new();
        let mut fake_scheduler = FakeScheduler::new();
        let mut ctx = make_context(fake_device.as_device(), fake_scheduler.as_scheduler());
        let mut bss = InfraBss::new(
            &mut ctx,
            b"coolnet".to_vec(),
            TimeUnit::DEFAULT_BEACON_INTERVAL,
            CapabilityInfo(0),
            vec![0b11111000],
            1,
            None,
        )
        .expect("expected InfraBss::new ok");

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
    fn handle_mlme_set_controlled_port_req() {
        let mut fake_device = FakeDevice::new();
        let mut fake_scheduler = FakeScheduler::new();
        let mut ctx = make_context(fake_device.as_device(), fake_scheduler.as_scheduler());
        let mut bss = InfraBss::new(
            &mut ctx,
            b"coolnet".to_vec(),
            TimeUnit::DEFAULT_BEACON_INTERVAL,
            CapabilityInfo(0),
            vec![0b11111000],
            1,
            Some(fake_wpa2_rsne()),
        )
        .expect("expected InfraBss::new ok");

        bss.clients.insert(CLIENT_ADDR, RemoteClient::new(CLIENT_ADDR));

        bss.handle_mlme_assoc_resp(
            &mut ctx,
            fidl_mlme::AssociateResponse {
                peer_sta_address: CLIENT_ADDR,
                result_code: fidl_mlme::AssociateResultCodes::Success,
                association_id: 1,
                cap: 0,
                rates: vec![1, 2, 3],
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
    fn handle_mlme_eapol_req() {
        let mut fake_device = FakeDevice::new();
        let mut fake_scheduler = FakeScheduler::new();
        let mut ctx = make_context(fake_device.as_device(), fake_scheduler.as_scheduler());
        let mut bss = InfraBss::new(
            &mut ctx,
            b"coolnet".to_vec(),
            TimeUnit::DEFAULT_BEACON_INTERVAL,
            CapabilityInfo(0),
            vec![0b11111000],
            1,
            None,
        )
        .expect("expected InfraBss::new ok");

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
    fn handle_mgmt_frame_auth() {
        let mut fake_device = FakeDevice::new();
        let mut fake_scheduler = FakeScheduler::new();
        let mut ctx = make_context(fake_device.as_device(), fake_scheduler.as_scheduler());
        let mut bss = InfraBss::new(
            &mut ctx,
            b"coolnet".to_vec(),
            TimeUnit::DEFAULT_BEACON_INTERVAL,
            CapabilityInfo(0),
            vec![0b11111000],
            1,
            None,
        )
        .expect("expected InfraBss::new ok");

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
    fn handle_mgmt_frame_assoc_req() {
        let mut fake_device = FakeDevice::new();
        let mut fake_scheduler = FakeScheduler::new();
        let mut ctx = make_context(fake_device.as_device(), fake_scheduler.as_scheduler());
        let mut bss = InfraBss::new(
            &mut ctx,
            b"coolnet".to_vec(),
            TimeUnit::DEFAULT_BEACON_INTERVAL,
            CapabilityInfo(0),
            vec![0b11111000],
            1,
            None,
        )
        .expect("expected InfraBss::new ok");

        bss.clients.insert(CLIENT_ADDR, RemoteClient::new(CLIENT_ADDR));
        let client = bss.clients.get_mut(&CLIENT_ADDR).unwrap();
        client
            .handle_mlme_auth_resp(&mut ctx, fidl_mlme::AuthenticateResultCodes::Success)
            .expect("expected OK");

        bss.handle_mgmt_frame(
            &mut ctx,
            mac::MgmtHdr {
                frame_ctrl: mac::FrameControl(0)
                    .with_frame_type(mac::FrameType::MGMT)
                    .with_mgmt_subtype(mac::MgmtSubtype::ASSOC_REQ),
                duration: 0,
                addr1: BSSID.0,
                addr2: CLIENT_ADDR,
                addr3: BSSID.0,
                seq_ctrl: mac::SequenceControl(10),
            },
            &[
                // Assoc req body
                0, 0, // Capability info
                10, 0, // Listen interval
                // IEs
                1, 8, 1, 2, 3, 4, 5, 6, 7, 8, // Rates
                50, 2, 9, 10, // Extended rates
                48, 2, 77, 88, // RSNE
            ][..],
        )
        .expect("expected OK");

        assert_eq!(bss.clients.contains_key(&CLIENT_ADDR), true);

        let msg = fake_device
            .next_mlme_msg::<fidl_mlme::AssociateIndication>()
            .expect("expected MLME message");
        assert_eq!(
            msg,
            fidl_mlme::AssociateIndication {
                peer_sta_address: CLIENT_ADDR,
                listen_interval: 10,
                ssid: Some(b"coolnet".to_vec()),
                cap: 0,
                rates: vec![1, 2, 3, 4, 5, 6, 7, 8, 9, 10],
                rsne: Some(vec![48, 2, 77, 88]),
            },
        );
    }

    #[test]
    fn handle_mgmt_frame_bad_ds_bits_to_ds() {
        let mut fake_device = FakeDevice::new();
        let mut fake_scheduler = FakeScheduler::new();
        let mut ctx = make_context(fake_device.as_device(), fake_scheduler.as_scheduler());
        let mut bss = InfraBss::new(
            &mut ctx,
            b"coolnet".to_vec(),
            TimeUnit::DEFAULT_BEACON_INTERVAL,
            CapabilityInfo(0),
            vec![0b11111000],
            1,
            None,
        )
        .expect("expected InfraBss::new ok");

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
    fn handle_mgmt_frame_bad_ds_bits_from_ds() {
        let mut fake_device = FakeDevice::new();
        let mut fake_scheduler = FakeScheduler::new();
        let mut ctx = make_context(fake_device.as_device(), fake_scheduler.as_scheduler());
        let mut bss = InfraBss::new(
            &mut ctx,
            b"coolnet".to_vec(),
            TimeUnit::DEFAULT_BEACON_INTERVAL,
            CapabilityInfo(0),
            vec![0b11111000],
            1,
            None,
        )
        .expect("expected InfraBss::new ok");

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
    fn handle_mgmt_frame_no_such_client() {
        let mut fake_device = FakeDevice::new();
        let mut fake_scheduler = FakeScheduler::new();
        let mut ctx = make_context(fake_device.as_device(), fake_scheduler.as_scheduler());
        let mut bss = InfraBss::new(
            &mut ctx,
            b"coolnet".to_vec(),
            TimeUnit::DEFAULT_BEACON_INTERVAL,
            CapabilityInfo(0),
            vec![0b11111000],
            1,
            None,
        )
        .expect("expected InfraBss::new ok");

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
    fn handle_mgmt_frame_bogus() {
        let mut fake_device = FakeDevice::new();
        let mut fake_scheduler = FakeScheduler::new();
        let mut ctx = make_context(fake_device.as_device(), fake_scheduler.as_scheduler());
        let mut bss = InfraBss::new(
            &mut ctx,
            b"coolnet".to_vec(),
            TimeUnit::DEFAULT_BEACON_INTERVAL,
            CapabilityInfo(0),
            vec![0b11111000],
            1,
            None,
        )
        .expect("expected InfraBss::new ok");

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
    fn handle_data_frame() {
        let mut fake_device = FakeDevice::new();
        let mut fake_scheduler = FakeScheduler::new();
        let mut ctx = make_context(fake_device.as_device(), fake_scheduler.as_scheduler());
        let mut bss = InfraBss::new(
            &mut ctx,
            b"coolnet".to_vec(),
            TimeUnit::DEFAULT_BEACON_INTERVAL,
            CapabilityInfo(0),
            vec![0b11111000],
            1,
            None,
        )
        .expect("expected InfraBss::new ok");

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
                1,
                mac::CapabilityInfo(0),
                fidl_mlme::AssociateResultCodes::Success,
                1,
                &[1, 2, 3, 4, 5, 6, 7, 8, 9, 10][..],
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
    fn handle_data_frame_bad_ds_bits() {
        let mut fake_device = FakeDevice::new();
        let mut fake_scheduler = FakeScheduler::new();
        let mut ctx = make_context(fake_device.as_device(), fake_scheduler.as_scheduler());
        let mut bss = InfraBss::new(
            &mut ctx,
            b"coolnet".to_vec(),
            TimeUnit::DEFAULT_BEACON_INTERVAL,
            CapabilityInfo(0),
            vec![0b11111000],
            1,
            None,
        )
        .expect("expected InfraBss::new ok");

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
    fn handle_client_event() {
        let mut fake_device = FakeDevice::new();
        let mut fake_scheduler = FakeScheduler::new();
        let mut ctx = make_context(fake_device.as_device(), fake_scheduler.as_scheduler());
        let mut bss = InfraBss::new(
            &mut ctx,
            b"coolnet".to_vec(),
            TimeUnit::DEFAULT_BEACON_INTERVAL,
            CapabilityInfo(0),
            vec![0b11111000],
            1,
            None,
        )
        .expect("expected InfraBss::new ok");

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
                1,
                mac::CapabilityInfo(0),
                fidl_mlme::AssociateResultCodes::Success,
                1,
                &[1, 2, 3, 4, 5, 6, 7, 8, 9, 10][..],
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
    fn handle_data_frame_no_such_client() {
        let mut fake_device = FakeDevice::new();
        let mut fake_scheduler = FakeScheduler::new();
        let mut ctx = make_context(fake_device.as_device(), fake_scheduler.as_scheduler());
        let mut bss = InfraBss::new(
            &mut ctx,
            b"coolnet".to_vec(),
            TimeUnit::DEFAULT_BEACON_INTERVAL,
            CapabilityInfo(0),
            vec![0b11111000],
            1,
            None,
        )
        .expect("expected InfraBss::new ok");

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
    fn handle_data_frame_client_not_associated() {
        let mut fake_device = FakeDevice::new();
        let mut fake_scheduler = FakeScheduler::new();
        let mut ctx = make_context(fake_device.as_device(), fake_scheduler.as_scheduler());
        let mut bss = InfraBss::new(
            &mut ctx,
            b"coolnet".to_vec(),
            TimeUnit::DEFAULT_BEACON_INTERVAL,
            CapabilityInfo(0),
            vec![0b11111000],
            1,
            None,
        )
        .expect("expected InfraBss::new ok");

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
    fn handle_eth_frame_no_rsn() {
        let mut fake_device = FakeDevice::new();
        let mut fake_scheduler = FakeScheduler::new();
        let mut ctx = make_context(fake_device.as_device(), fake_scheduler.as_scheduler());
        let mut bss = InfraBss::new(
            &mut ctx,
            b"coolnet".to_vec(),
            TimeUnit::DEFAULT_BEACON_INTERVAL,
            CapabilityInfo(0),
            vec![0b11111000],
            1,
            None,
        )
        .expect("expected InfraBss::new ok");
        bss.clients.insert(CLIENT_ADDR, RemoteClient::new(CLIENT_ADDR));

        let client = bss.clients.get_mut(&CLIENT_ADDR).unwrap();
        client
            .handle_mlme_auth_resp(&mut ctx, fidl_mlme::AuthenticateResultCodes::Success)
            .expect("expected OK");
        client
            .handle_mlme_assoc_resp(
                &mut ctx,
                false,
                1,
                mac::CapabilityInfo(0),
                fidl_mlme::AssociateResultCodes::Success,
                1,
                &[1, 2, 3, 4, 5, 6, 7, 8, 9, 10][..],
            )
            .expect("expected OK");
        fake_device.wlan_queue.clear();

        bss.handle_eth_frame(
            &mut ctx,
            &make_eth_frame(CLIENT_ADDR, CLIENT_ADDR2, 0x1234, &[1, 2, 3, 4, 5][..]),
        )
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
    fn handle_eth_frame_no_client() {
        let mut fake_device = FakeDevice::new();
        let mut fake_scheduler = FakeScheduler::new();
        let mut ctx = make_context(fake_device.as_device(), fake_scheduler.as_scheduler());
        let mut bss = InfraBss::new(
            &mut ctx,
            b"coolnet".to_vec(),
            TimeUnit::DEFAULT_BEACON_INTERVAL,
            CapabilityInfo(0),
            vec![0b11111000],
            1,
            None,
        )
        .expect("expected InfraBss::new ok");

        assert_variant!(
            bss.handle_eth_frame(
                &mut ctx,
                &make_eth_frame(CLIENT_ADDR, CLIENT_ADDR2, 0x1234, &[1, 2, 3, 4, 5][..])
            )
            .expect_err("expected error"),
            Rejection::Client(_, ClientRejection::NotAssociated)
        );

        assert_eq!(fake_device.wlan_queue.len(), 0);
    }

    #[test]
    fn handle_eth_frame_is_rsn_eapol_controlled_port_closed() {
        let mut fake_device = FakeDevice::new();
        let mut fake_scheduler = FakeScheduler::new();
        let mut ctx = make_context(fake_device.as_device(), fake_scheduler.as_scheduler());
        let mut bss = InfraBss::new(
            &mut ctx,
            b"coolnet".to_vec(),
            TimeUnit::DEFAULT_BEACON_INTERVAL,
            CapabilityInfo(0),
            vec![0b11111000],
            1,
            Some(fake_wpa2_rsne()),
        )
        .expect("expected InfraBss::new ok");
        bss.clients.insert(CLIENT_ADDR, RemoteClient::new(CLIENT_ADDR));

        let client = bss.clients.get_mut(&CLIENT_ADDR).unwrap();
        client
            .handle_mlme_auth_resp(&mut ctx, fidl_mlme::AuthenticateResultCodes::Success)
            .expect("expected OK");
        client
            .handle_mlme_assoc_resp(
                &mut ctx,
                true,
                1,
                mac::CapabilityInfo(0),
                fidl_mlme::AssociateResultCodes::Success,
                1,
                &[1, 2, 3, 4, 5, 6, 7, 8, 9, 10][..],
            )
            .expect("expected OK");
        fake_device.wlan_queue.clear();

        assert_variant!(
            bss.handle_eth_frame(
                &mut ctx,
                &make_eth_frame(CLIENT_ADDR, CLIENT_ADDR2, 0x1234, &[1, 2, 3, 4, 5][..])
            )
            .expect_err("expected error"),
            Rejection::Client(_, ClientRejection::ControlledPortClosed)
        );
    }

    #[test]
    fn handle_eth_frame_is_rsn_eapol_controlled_port_open() {
        let mut fake_device = FakeDevice::new();
        let mut fake_scheduler = FakeScheduler::new();
        let mut ctx = make_context(fake_device.as_device(), fake_scheduler.as_scheduler());
        let mut bss = InfraBss::new(
            &mut ctx,
            b"coolnet".to_vec(),
            TimeUnit::DEFAULT_BEACON_INTERVAL,
            CapabilityInfo(0),
            vec![0b11111000],
            1,
            Some(fake_wpa2_rsne()),
        )
        .expect("expected InfraBss::new ok");
        bss.clients.insert(CLIENT_ADDR, RemoteClient::new(CLIENT_ADDR));

        let client = bss.clients.get_mut(&CLIENT_ADDR).unwrap();
        client
            .handle_mlme_auth_resp(&mut ctx, fidl_mlme::AuthenticateResultCodes::Success)
            .expect("expected OK");
        client
            .handle_mlme_assoc_resp(
                &mut ctx,
                true,
                1,
                mac::CapabilityInfo(0),
                fidl_mlme::AssociateResultCodes::Success,
                1,
                &[1, 2, 3, 4, 5, 6, 7, 8, 9, 10][..],
            )
            .expect("expected OK");
        fake_device.wlan_queue.clear();

        client
            .handle_mlme_set_controlled_port_req(fidl_mlme::ControlledPortState::Open)
            .expect("expected OK");

        bss.handle_eth_frame(
            &mut ctx,
            &make_eth_frame(CLIENT_ADDR, CLIENT_ADDR2, 0x1234, &[1, 2, 3, 4, 5][..]),
        )
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
    fn handle_mlme_setkeys_req() {
        let mut fake_device = FakeDevice::new();
        let mut fake_scheduler = FakeScheduler::new();
        let mut ctx = make_context(fake_device.as_device(), fake_scheduler.as_scheduler());
        let mut bss = InfraBss::new(
            &mut ctx,
            b"coolnet".to_vec(),
            TimeUnit::DEFAULT_BEACON_INTERVAL,
            CapabilityInfo(0),
            vec![0b11111000],
            1,
            Some(fake_wpa2_rsne()),
        )
        .expect("expected InfraBss::new ok");
        bss.handle_mlme_setkeys_req(
            &mut ctx,
            &[fidl_mlme::SetKeyDescriptor {
                cipher_suite_oui: [1, 2, 3],
                cipher_suite_type: 4,
                key_type: fidl_mlme::KeyType::Pairwise,
                address: [5; 6],
                key_id: 6,
                key: vec![1, 2, 3, 4, 5, 6, 7],
                rsc: 8,
            }][..],
        )
        .expect("expected InfraBss::handle_mlme_setkeys_req OK");
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
    fn handle_mlme_setkeys_req_no_rsne() {
        let mut fake_device = FakeDevice::new();
        let mut fake_scheduler = FakeScheduler::new();
        let mut ctx = make_context(fake_device.as_device(), fake_scheduler.as_scheduler());
        let mut bss = InfraBss::new(
            &mut ctx,
            b"coolnet".to_vec(),
            TimeUnit::DEFAULT_BEACON_INTERVAL,
            CapabilityInfo(0),
            vec![0b11111000],
            1,
            None,
        )
        .expect("expected InfraBss::new ok");
        assert_variant!(
            bss.handle_mlme_setkeys_req(
                &mut ctx,
                &[fidl_mlme::SetKeyDescriptor {
                    cipher_suite_oui: [1, 2, 3],
                    cipher_suite_type: 4,
                    key_type: fidl_mlme::KeyType::Pairwise,
                    address: [5; 6],
                    key_id: 6,
                    key: vec![1, 2, 3, 4, 5, 6, 7],
                    rsc: 8,
                }][..]
            )
            .expect_err("expected InfraBss::handle_mlme_setkeys_req error"),
            Error::Status(_, zx::Status::BAD_STATE)
        );
    }

    #[test]
    fn handle_probe_req() {
        let mut fake_device = FakeDevice::new();
        let mut fake_scheduler = FakeScheduler::new();
        let mut ctx = make_context(fake_device.as_device(), fake_scheduler.as_scheduler());
        let mut bss = InfraBss::new(
            &mut ctx,
            vec![1, 2, 3, 4, 5],
            TimeUnit::DEFAULT_BEACON_INTERVAL,
            mac::CapabilityInfo(33),
            vec![248],
            1,
            Some(vec![48, 2, 77, 88]),
        )
        .expect("expected InfraBss::new ok");

        bss.handle_probe_req(&mut ctx, CLIENT_ADDR)
            .expect("expected InfraBss::handle_probe_req ok");
        assert_eq!(fake_device.wlan_queue.len(), 1);
        assert_eq!(
            &fake_device.wlan_queue[0].0[..],
            &[
                // Mgmt header
                0b01010000, 0, // Frame Control
                0, 0, // Duration
                1, 1, 1, 1, 1, 1, // addr1
                2, 2, 2, 2, 2, 2, // addr2
                2, 2, 2, 2, 2, 2, // addr3
                0x10, 0, // Sequence Control
                // Beacon header:
                0, 0, 0, 0, 0, 0, 0, 0, // Timestamp
                100, 0, // Beacon interval
                33, 0, // Capabilities
                // IEs:
                0, 5, 1, 2, 3, 4, 5, // SSID
                1, 1, 248, // Supported rates
                3, 1, 1, // DSSS parameter set
                48, 2, 77, 88, // RSNE
            ][..]
        );
    }

    #[test]
    fn handle_probe_req_has_offload() {
        let mut fake_device = FakeDevice::new();
        fake_device.info.ifc_info.driver_features |= WlanInfoDriverFeature::PROBE_RESP_OFFLOAD;
        let mut fake_scheduler = FakeScheduler::new();
        let mut ctx = make_context(fake_device.as_device(), fake_scheduler.as_scheduler());
        let mut bss = InfraBss::new(
            &mut ctx,
            vec![1, 2, 3, 4, 5],
            TimeUnit::DEFAULT_BEACON_INTERVAL,
            CapabilityInfo(33),
            vec![0b11111000],
            1,
            Some(vec![48, 2, 77, 88]),
        )
        .expect("expected InfraBss::new ok");

        bss.handle_mgmt_frame(
            &mut ctx,
            mac::MgmtHdr {
                frame_ctrl: mac::FrameControl(0)
                    .with_frame_type(mac::FrameType::MGMT)
                    .with_mgmt_subtype(mac::MgmtSubtype::PROBE_REQ),
                duration: 0,
                addr1: BSSID.0,
                addr2: CLIENT_ADDR,
                addr3: BSSID.0,
                seq_ctrl: mac::SequenceControl(10),
            },
            &[][..],
        )
        .expect_err("expected InfraBss::handle_mgmt_frame error");
    }

    #[test]
    fn handle_probe_req_wildcard_ssid() {
        let mut fake_device = FakeDevice::new();
        let mut fake_scheduler = FakeScheduler::new();
        let mut ctx = make_context(fake_device.as_device(), fake_scheduler.as_scheduler());
        let mut bss = InfraBss::new(
            &mut ctx,
            vec![1, 2, 3, 4, 5],
            TimeUnit::DEFAULT_BEACON_INTERVAL,
            CapabilityInfo(33),
            vec![0b11111000],
            1,
            Some(vec![48, 2, 77, 88]),
        )
        .expect("expected InfraBss::new ok");

        bss.handle_mgmt_frame(
            &mut ctx,
            mac::MgmtHdr {
                frame_ctrl: mac::FrameControl(0)
                    .with_frame_type(mac::FrameType::MGMT)
                    .with_mgmt_subtype(mac::MgmtSubtype::PROBE_REQ),
                duration: 0,
                addr1: BSSID.0,
                addr2: CLIENT_ADDR,
                addr3: BSSID.0,
                seq_ctrl: mac::SequenceControl(10),
            },
            &[
                0, 0, // Wildcard SSID
            ][..],
        )
        .expect("expected InfraBss::handle_mgmt_frame ok");

        assert_eq!(fake_device.wlan_queue.len(), 1);
        assert_eq!(
            &fake_device.wlan_queue[0].0[..],
            &[
                // Mgmt header
                0b01010000, 0, // Frame Control
                0, 0, // Duration
                1, 1, 1, 1, 1, 1, // addr1
                2, 2, 2, 2, 2, 2, // addr2
                2, 2, 2, 2, 2, 2, // addr3
                0x10, 0, // Sequence Control
                // Beacon header:
                0, 0, 0, 0, 0, 0, 0, 0, // Timestamp
                100, 0, // Beacon interval
                33, 0, // Capabilities
                // IEs:
                0, 5, 1, 2, 3, 4, 5, // SSID
                1, 1, 248, // Supported rates
                3, 1, 1, // DSSS parameter set
                48, 2, 77, 88, // RSNE
            ][..]
        );
    }

    #[test]
    fn handle_probe_req_matching_ssid() {
        let mut fake_device = FakeDevice::new();
        let mut fake_scheduler = FakeScheduler::new();
        let mut ctx = make_context(fake_device.as_device(), fake_scheduler.as_scheduler());
        let mut bss = InfraBss::new(
            &mut ctx,
            vec![1, 2, 3, 4, 5],
            TimeUnit::DEFAULT_BEACON_INTERVAL,
            CapabilityInfo(33),
            vec![0b11111000],
            1,
            Some(vec![48, 2, 77, 88]),
        )
        .expect("expected InfraBss::new ok");

        bss.handle_mgmt_frame(
            &mut ctx,
            mac::MgmtHdr {
                frame_ctrl: mac::FrameControl(0)
                    .with_frame_type(mac::FrameType::MGMT)
                    .with_mgmt_subtype(mac::MgmtSubtype::PROBE_REQ),
                duration: 0,
                addr1: BSSID.0,
                addr2: CLIENT_ADDR,
                addr3: BSSID.0,
                seq_ctrl: mac::SequenceControl(10),
            },
            &[0, 5, 1, 2, 3, 4, 5][..],
        )
        .expect("expected InfraBss::handle_mgmt_frame ok");

        assert_eq!(fake_device.wlan_queue.len(), 1);
        assert_eq!(
            &fake_device.wlan_queue[0].0[..],
            &[
                // Mgmt header
                0b01010000, 0, // Frame Control
                0, 0, // Duration
                1, 1, 1, 1, 1, 1, // addr1
                2, 2, 2, 2, 2, 2, // addr2
                2, 2, 2, 2, 2, 2, // addr3
                0x10, 0, // Sequence Control
                // Beacon header:
                0, 0, 0, 0, 0, 0, 0, 0, // Timestamp
                100, 0, // Beacon interval
                33, 0, // Capabilities
                // IEs:
                0, 5, 1, 2, 3, 4, 5, // SSID
                1, 1, 248, // Supported rates
                3, 1, 1, // DSSS parameter set
                48, 2, 77, 88, // RSNE
            ][..]
        );
    }

    #[test]
    fn handle_probe_req_mismatching_ssid() {
        let mut fake_device = FakeDevice::new();
        let mut fake_scheduler = FakeScheduler::new();
        let mut ctx = make_context(fake_device.as_device(), fake_scheduler.as_scheduler());
        let mut bss = InfraBss::new(
            &mut ctx,
            vec![1, 2, 3, 4, 5],
            TimeUnit::DEFAULT_BEACON_INTERVAL,
            CapabilityInfo(33),
            vec![0b11111000],
            1,
            Some(vec![48, 2, 77, 88]),
        )
        .expect("expected InfraBss::new ok");

        assert_variant!(
            bss.handle_mgmt_frame(
                &mut ctx,
                mac::MgmtHdr {
                    frame_ctrl: mac::FrameControl(0)
                        .with_frame_type(mac::FrameType::MGMT)
                        .with_mgmt_subtype(mac::MgmtSubtype::PROBE_REQ),
                    duration: 0,
                    addr1: BSSID.0,
                    addr2: CLIENT_ADDR,
                    addr3: BSSID.0,
                    seq_ctrl: mac::SequenceControl(10),
                },
                &[0, 5, 1, 2, 3, 4, 6][..],
            )
            .expect_err("expected InfraBss::handle_mgmt_frame error"),
            Rejection::OtherBss
        );
    }
}
