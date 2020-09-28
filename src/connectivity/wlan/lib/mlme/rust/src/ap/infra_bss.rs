// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        ap::{
            frame_writer,
            remote_client::{ClientRejection, RemoteClient},
            BeaconOffloadParams, BufferedFrame, Context, Rejection, TimedEvent,
        },
        buffer::{InBuf, OutBuf},
        device::TxFlags,
        error::Error,
        key::KeyConfig,
        timer::EventId,
    },
    anyhow::format_err,
    banjo_ddk_hw_wlan_wlaninfo::WlanInfoDriverFeature,
    banjo_ddk_protocol_wlan_info::{WlanChannel, WlanChannelBandwidth},
    banjo_ddk_protocol_wlan_mac as banjo_wlan_mac, fidl_fuchsia_wlan_mlme as fidl_mlme,
    fuchsia_zircon as zx,
    log::error,
    std::collections::{HashMap, VecDeque},
    wlan_common::{
        ie,
        mac::{self, is_multicast, CapabilityInfo, EthernetIIHdr, MacAddr},
        tim, TimeUnit,
    },
    zerocopy::ByteSlice,
};

pub struct InfraBss {
    pub ssid: Vec<u8>,
    pub rsne: Option<Vec<u8>>,
    pub beacon_interval: TimeUnit,
    pub dtim_period: u8,
    pub capabilities: CapabilityInfo,
    pub rates: Vec<u8>,
    pub channel: u8,
    pub clients: HashMap<MacAddr, RemoteClient>,

    group_buffered: VecDeque<BufferedFrame>,
    dtim_count: u8,
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
/// anyhow::Error error), but will still preserve the underlying zx::Status code.
fn make_client_error(addr: MacAddr, e: Error) -> Error {
    Error::Status(format!("client {:02X?}: {}", addr, e), e.into())
}

impl InfraBss {
    pub fn new(
        ctx: &mut Context,
        ssid: Vec<u8>,
        beacon_interval: TimeUnit,
        dtim_period: u8,
        capabilities: CapabilityInfo,
        rates: Vec<u8>,
        channel: u8,
        rsne: Option<Vec<u8>>,
    ) -> Result<Self, Error> {
        let bss = Self {
            ssid,
            rsne,
            beacon_interval,
            dtim_period,
            rates,
            capabilities,
            channel,
            clients: HashMap::new(),

            group_buffered: VecDeque::new(),
            dtim_count: 0,
        };

        ctx.device
            .set_channel(WlanChannel {
                primary: channel,

                // TODO(fxbug.dev/40917): Correctly support this.
                cbw: WlanChannelBandwidth::_20,
                secondary80: 0,
            })
            .map_err(|s| Error::Status(format!("failed to set channel"), s))?;

        // TODO(fxbug.dev/37891): Support DTIM.

        let (in_buf, bytes_written, beacon_offload_params) = bss.make_beacon_frame(ctx)?;
        ctx.device
            .enable_beaconing(
                OutBuf::from(in_buf, bytes_written),
                beacon_offload_params.tim_ele_offset,
                beacon_interval,
            )
            .map_err(|s| Error::Status(format!("failed to enable beaconing"), s))?;

        Ok(bss)
    }

    pub fn stop(&self, ctx: &mut Context) -> Result<(), Error> {
        ctx.device
            .disable_beaconing()
            .map_err(|s| Error::Status(format!("failed to disable beaconing"), s))
    }

    fn make_tim(&self) -> tim::TrafficIndicationMap {
        let mut tim = tim::TrafficIndicationMap::new();
        for client in self.clients.values() {
            let aid = match client.aid() {
                Some(aid) => aid,
                None => {
                    continue;
                }
            };
            tim.set_traffic_buffered(aid, client.has_buffered_frames());
        }
        tim
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
        &mut self,
        ctx: &mut Context,
        req: fidl_mlme::EapolRequest,
    ) -> Result<(), Error> {
        let client = get_client_mut(&mut self.clients, req.dst_addr)?;
        client
            .handle_mlme_eapol_req(ctx, req.src_addr, &req.data)
            .map_err(|e| make_client_error(client.addr, e))
    }

    fn make_beacon_frame(
        &self,
        ctx: &Context,
    ) -> Result<(InBuf, usize, BeaconOffloadParams), Error> {
        let tim = self.make_tim();
        let (pvb_offset, pvb_bitmap) = tim.make_partial_virtual_bitmap();

        ctx.make_beacon_frame(
            0,
            self.beacon_interval,
            self.capabilities,
            &self.ssid,
            &self.rates,
            self.channel,
            ie::TimHeader {
                dtim_count: self.dtim_count,
                dtim_period: self.dtim_period,
                bmp_ctrl: ie::BitmapControl(0)
                    .with_group_traffic(!self.group_buffered.is_empty())
                    .with_offset(pvb_offset),
            },
            pvb_bitmap,
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
        let (in_buf, bytes_written) = ctx
            .make_probe_resp_frame(
                client_addr,
                0,
                self.beacon_interval,
                self.capabilities,
                &self.ssid,
                &self.rates,
                self.channel,
                self.rsne.as_ref().map_or(&[], |rsne| &rsne),
            )
            .map_err(|e| Rejection::Client(client_addr, ClientRejection::WlanSendError(e)))?;
        ctx.device.send_wlan_frame(OutBuf::from(in_buf, bytes_written), TxFlags::NONE).map_err(
            |s| {
                Rejection::Client(
                    client_addr,
                    ClientRejection::WlanSendError(Error::Status(
                        format!("failed to send probe resp"),
                        s,
                    )),
                )
            },
        )
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
            if (driver_features & WlanInfoDriverFeature::PROBE_RESP_OFFLOAD).0 != 0 {
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

        // IEEE Std 802.11-2016, 9.2.4.1.7: The value [of the Power Management subfield] indicates
        // the mode of the STA after the successful completion of the frame exchange sequence.
        match client.set_power_state(ctx, { mgmt_hdr.frame_ctrl }.power_mgmt()) {
            Err(ClientRejection::NotAssociated) => {
                error!("client {:02X?} tried to doze but is not associated", client_addr);
            }
            Err(e) => {
                return Err(Rejection::Client(client.addr, e));
            }
            Ok(()) => {}
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
            .map_err(|e| Rejection::Client(client.addr, e))?;

        // IEEE Std 802.11-2016, 9.2.4.1.7: The value [of the Power Management subfield] indicates
        // the mode of the STA after the successful completion of the frame exchange sequence.
        match client.set_power_state(ctx, { fixed_fields.frame_ctrl }.power_mgmt()) {
            Err(ClientRejection::NotAssociated) => {
                error!("client {:02X?} tried to doze but is not associated", client.addr);
            }
            Err(e) => {
                return Err(Rejection::Client(client.addr, e));
            }
            Ok(()) => {}
        }

        Ok(())
    }

    pub fn handle_ctrl_frame<B: ByteSlice>(
        &mut self,
        ctx: &mut Context,
        frame_ctrl: mac::FrameControl,
        body: B,
    ) -> Result<(), Rejection> {
        match mac::CtrlBody::parse(frame_ctrl.ctrl_subtype(), body)
            .ok_or(Rejection::FrameMalformed)?
        {
            mac::CtrlBody::PsPoll { ps_poll } => {
                let client = match self.clients.get_mut(&ps_poll.ta) {
                    Some(client) => client,
                    _ => {
                        return Err(Rejection::Client(
                            ps_poll.ta,
                            ClientRejection::NotAuthenticated,
                        ));
                    }
                };

                // IEEE 802.11-2016 9.3.1.5 states the ID in the PS-Poll frame is the association ID
                // with the 2 MSBs set to 1.
                const PS_POLL_MASK: u16 = 0b11000000_00000000;
                client
                    .handle_ps_poll(ctx, ps_poll.masked_aid & !PS_POLL_MASK)
                    .map_err(|e| Rejection::Client(client.addr, e))
            }
            _ => Err(Rejection::FrameMalformed),
        }
    }

    pub fn handle_multicast_eth_frame(
        &mut self,
        ctx: &mut Context,
        hdr: EthernetIIHdr,
        body: &[u8],
    ) -> Result<(), Rejection> {
        let (in_buf, bytes_written) = ctx
            .make_data_frame(
                hdr.da,
                hdr.sa,
                self.rsne.is_some(),
                false, // TODO(fxbug.dev/37891): Support QoS.
                hdr.ether_type.to_native(),
                body,
            )
            .map_err(|e| Rejection::Client(hdr.da, ClientRejection::WlanSendError(e)))?;
        let tx_flags = TxFlags::NONE;

        if !self.clients.values().any(|client| client.dozing()) {
            ctx.device.send_wlan_frame(OutBuf::from(in_buf, bytes_written), tx_flags).map_err(
                |s| {
                    Rejection::Client(
                        hdr.da,
                        ClientRejection::WlanSendError(Error::Status(
                            format!("error sending multicast data frame"),
                            s,
                        )),
                    )
                },
            )?;
        } else {
            self.group_buffered.push_back(BufferedFrame { in_buf, bytes_written, tx_flags });
        }

        Ok(())
    }

    pub fn handle_eth_frame(
        &mut self,
        ctx: &mut Context,
        hdr: EthernetIIHdr,
        body: &[u8],
    ) -> Result<(), Rejection> {
        if is_multicast(hdr.da) {
            return self.handle_multicast_eth_frame(ctx, hdr, body);
        }

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

    pub fn handle_hw_indication(
        &mut self,
        ctx: &mut Context,
        ind: banjo_wlan_mac::WlanIndication,
    ) -> Result<(), Error> {
        match ind {
            banjo_wlan_mac::WlanIndication::PRE_TBTT => self.handle_pre_tbtt_hw_indication(ctx),
            banjo_wlan_mac::WlanIndication::BCN_TX_COMPLETE => {
                self.handle_bcn_tx_complete_indication(ctx)
            }
            _ => {
                // Ignore unknown HW indications.
                error!("unknown HW indication: {:?}", ind);
                Ok(())
            }
        }
    }

    pub fn handle_pre_tbtt_hw_indication(&mut self, ctx: &mut Context) -> Result<(), Error> {
        // We don't need the parameters here again (i.e. tim_ele_offset), as the offset of the TIM
        // element never changes within the beacon frame template.
        let (in_buf, bytes_written, _) = self.make_beacon_frame(ctx)?;
        ctx.device
            .configure_beacon(OutBuf::from(in_buf, bytes_written))
            .map_err(|s| Error::Status(format!("failed to configure beaconing"), s))?;
        Ok(())
    }

    pub fn handle_bcn_tx_complete_indication(&mut self, ctx: &mut Context) -> Result<(), Error> {
        if self.dtim_count > 0 {
            self.dtim_count -= 1;
            return Ok(());
        }

        self.dtim_count = self.dtim_period;

        let mut buffered = self.group_buffered.drain(..).peekable();
        while let Some(BufferedFrame { mut in_buf, bytes_written, tx_flags }) = buffered.next() {
            if buffered.peek().is_some() {
                frame_writer::set_more_data(&mut in_buf.as_mut_slice()[..bytes_written])?;
            }
            ctx.device
                .send_wlan_frame(OutBuf::from(in_buf, bytes_written), tx_flags)
                .map_err(|s| Error::Status(format!("error sending buffered frame on wake"), s))?;
        }

        Ok(())
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
            big_endian::BigEndianU16,
            mac::{Bssid, CapabilityInfo},
            test_utils::fake_frames::fake_wpa2_rsne,
        },
    };

    const CLIENT_ADDR: MacAddr = [4u8; 6];
    const BSSID: Bssid = Bssid([2u8; 6]);
    const CLIENT_ADDR2: MacAddr = [6u8; 6];

    fn make_context(device: Device, scheduler: Scheduler) -> Context {
        Context::new(device, FakeBufferProvider::new(), Timer::<TimedEvent>::new(scheduler), BSSID)
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
            2,
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
            5, 4, 0, 2, 0, 0, // TIM
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
            2,
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
            2,
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
                4, 4, 4, 4, 4, 4, // addr1
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
            2,
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
            2,
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
                4, 4, 4, 4, 4, 4, // addr1
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
            2,
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
                4, 4, 4, 4, 4, 4, // addr1
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
            2,
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
                4, 4, 4, 4, 4, 4, // addr1
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
            2,
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
                4, 4, 4, 4, 4, 4, // addr1
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
            2,
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
            2,
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
                4, 4, 4, 4, 4, 4, // addr1
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
            2,
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
            2,
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
            2,
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
            2,
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
            2,
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
            2,
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
            2,
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
                6, 6, 6, 6, 6, 6, // dest
                4, 4, 4, 4, 4, 4, // src
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
            2,
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
            2,
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
            4, 4, 4, 4, 4, 4, // addr1
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
                locally_initiated: true,
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
            2,
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
                4, 4, 4, 4, 4, 4, // addr1
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
            2,
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
                4, 4, 4, 4, 4, 4, // addr1
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
            2,
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
            EthernetIIHdr {
                da: CLIENT_ADDR,
                sa: CLIENT_ADDR2,
                ether_type: BigEndianU16::from_native(0x1234),
            },
            &[1, 2, 3, 4, 5][..],
        )
        .expect("expected OK");

        assert_eq!(fake_device.wlan_queue.len(), 1);
        assert_eq!(
            &fake_device.wlan_queue[0].0[..],
            &[
                // Mgmt header
                0b00001000, 0b00000010, // Frame Control
                0, 0, // Duration
                4, 4, 4, 4, 4, 4, // addr1
                2, 2, 2, 2, 2, 2, // addr2
                6, 6, 6, 6, 6, 6, // addr3
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
            2,
            CapabilityInfo(0),
            vec![0b11111000],
            1,
            None,
        )
        .expect("expected InfraBss::new ok");

        assert_variant!(
            bss.handle_eth_frame(
                &mut ctx,
                EthernetIIHdr {
                    da: CLIENT_ADDR,
                    sa: CLIENT_ADDR2,
                    ether_type: BigEndianU16::from_native(0x1234)
                },
                &[1, 2, 3, 4, 5][..],
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
            2,
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
                EthernetIIHdr {
                    da: CLIENT_ADDR,
                    sa: CLIENT_ADDR2,
                    ether_type: BigEndianU16::from_native(0x1234)
                },
                &[1, 2, 3, 4, 5][..],
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
            2,
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
            EthernetIIHdr {
                da: CLIENT_ADDR,
                sa: CLIENT_ADDR2,
                ether_type: BigEndianU16::from_native(0x1234),
            },
            &[1, 2, 3, 4, 5][..],
        )
        .expect("expected OK");

        assert_eq!(fake_device.wlan_queue.len(), 1);
        assert_eq!(
            &fake_device.wlan_queue[0].0[..],
            &[
                // Mgmt header
                0b00001000, 0b01000010, // Frame Control
                0, 0, // Duration
                4, 4, 4, 4, 4, 4, // addr1
                2, 2, 2, 2, 2, 2, // addr2
                6, 6, 6, 6, 6, 6, // addr3
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
    fn handle_ps_poll() {
        let mut fake_device = FakeDevice::new();
        let mut fake_scheduler = FakeScheduler::new();
        let mut ctx = make_context(fake_device.as_device(), fake_scheduler.as_scheduler());
        let mut bss = InfraBss::new(
            &mut ctx,
            b"coolnet".to_vec(),
            TimeUnit::DEFAULT_BEACON_INTERVAL,
            2,
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
        client.set_power_state(&mut ctx, mac::PowerState::DOZE).expect("expected doze ok");
        fake_device.wlan_queue.clear();

        bss.handle_eth_frame(
            &mut ctx,
            EthernetIIHdr {
                da: CLIENT_ADDR,
                sa: CLIENT_ADDR2,
                ether_type: BigEndianU16::from_native(0x1234),
            },
            &[1, 2, 3, 4, 5][..],
        )
        .expect("expected OK");
        assert_eq!(fake_device.wlan_queue.len(), 0);

        bss.handle_ctrl_frame(
            &mut ctx,
            mac::FrameControl(0)
                .with_frame_type(mac::FrameType::CTRL)
                .with_ctrl_subtype(mac::CtrlSubtype::PS_POLL),
            &[
                0b00000001, 0b11000000, // Masked AID
                2, 2, 2, 2, 2, 2, // RA
                4, 4, 4, 4, 4, 4, // TA
            ][..],
        )
        .expect("expected OK");
        assert_eq!(fake_device.wlan_queue.len(), 1);
        assert_eq!(
            &fake_device.wlan_queue[0].0[..],
            &[
                // Mgmt header
                0b00001000, 0b00000010, // Frame Control
                0, 0, // Duration
                4, 4, 4, 4, 4, 4, // addr1
                2, 2, 2, 2, 2, 2, // addr2
                6, 6, 6, 6, 6, 6, // addr3
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
            2,
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
            2,
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
            2,
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
                4, 4, 4, 4, 4, 4, // addr1
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
            2,
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
            2,
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
                4, 4, 4, 4, 4, 4, // addr1
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
            2,
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
                4, 4, 4, 4, 4, 4, // addr1
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
            2,
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

    #[test]
    fn make_tim() {
        let mut fake_device = FakeDevice::new();
        let mut fake_scheduler = FakeScheduler::new();
        let mut ctx = make_context(fake_device.as_device(), fake_scheduler.as_scheduler());
        let mut bss = InfraBss::new(
            &mut ctx,
            b"coolnet".to_vec(),
            TimeUnit::DEFAULT_BEACON_INTERVAL,
            2,
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
        client.set_power_state(&mut ctx, mac::PowerState::DOZE).expect("expected doze OK");

        bss.handle_eth_frame(
            &mut ctx,
            EthernetIIHdr {
                da: CLIENT_ADDR,
                sa: CLIENT_ADDR2,
                ether_type: BigEndianU16::from_native(0x1234),
            },
            &[1, 2, 3, 4, 5][..],
        )
        .expect("expected OK");

        let tim = bss.make_tim();
        let (pvb_offset, pvb_bitmap) = tim.make_partial_virtual_bitmap();
        assert_eq!(pvb_offset, 0);
        assert_eq!(pvb_bitmap, &[0b00000010][..]);
    }

    #[test]
    fn make_tim_empty() {
        let mut fake_device = FakeDevice::new();
        let mut fake_scheduler = FakeScheduler::new();
        let mut ctx = make_context(fake_device.as_device(), fake_scheduler.as_scheduler());
        let bss = InfraBss::new(
            &mut ctx,
            b"coolnet".to_vec(),
            TimeUnit::DEFAULT_BEACON_INTERVAL,
            2,
            CapabilityInfo(0),
            vec![0b11111000],
            1,
            None,
        )
        .expect("expected InfraBss::new ok");

        let tim = bss.make_tim();
        let (pvb_offset, pvb_bitmap) = tim.make_partial_virtual_bitmap();
        assert_eq!(pvb_offset, 0);
        assert_eq!(pvb_bitmap, &[0b00000000][..]);
    }

    #[test]
    fn handle_pre_tbtt_hw_indication() {
        let mut fake_device = FakeDevice::new();
        let mut fake_scheduler = FakeScheduler::new();
        let mut ctx = make_context(fake_device.as_device(), fake_scheduler.as_scheduler());
        let mut bss = InfraBss::new(
            &mut ctx,
            b"coolnet".to_vec(),
            TimeUnit::DEFAULT_BEACON_INTERVAL,
            2,
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
        client.set_power_state(&mut ctx, mac::PowerState::DOZE).expect("expected doze OK");

        bss.handle_eth_frame(
            &mut ctx,
            EthernetIIHdr {
                da: CLIENT_ADDR,
                sa: CLIENT_ADDR2,
                ether_type: BigEndianU16::from_native(0x1234),
            },
            &[1, 2, 3, 4, 5][..],
        )
        .expect("expected OK");

        bss.handle_pre_tbtt_hw_indication(&mut ctx)
            .expect("expected handle pre-TBTT hw indication OK");
        let (bcn, _, _) = fake_device.bcn_cfg.as_ref().expect("expected beacon configuration");
        assert_eq!(
            &bcn[..],
            &[
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
                0, 0, // Capabilities
                // IEs:
                0, 7, 99, 111, 111, 108, 110, 101, 116, // SSID
                1, 1, 248, // Supported rates
                3, 1, 1, // DSSS parameter set
                5, 4, 0, 2, 0, 2, // TIM
            ][..],
        );
        bss.handle_bcn_tx_complete_indication(&mut ctx)
            .expect("expected handle bcn tx complete hw indication OK");

        bss.handle_pre_tbtt_hw_indication(&mut ctx)
            .expect("expected handle pre-TBTT hw indication OK");
        let (bcn, _, _) = fake_device.bcn_cfg.as_ref().expect("expected beacon configuration");
        assert_eq!(
            &bcn[..],
            &[
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
                0, 0, // Capabilities
                // IEs:
                0, 7, 99, 111, 111, 108, 110, 101, 116, // SSID
                1, 1, 248, // Supported rates
                3, 1, 1, // DSSS parameter set
                5, 4, 2, 2, 0, 2, // TIM
            ][..],
        );
        bss.handle_bcn_tx_complete_indication(&mut ctx)
            .expect("expected handle bcn tx complete hw indication OK");

        bss.handle_pre_tbtt_hw_indication(&mut ctx)
            .expect("expected handle pre-TBTT hw indication OK");
        let (bcn, _, _) = fake_device.bcn_cfg.as_ref().expect("expected beacon configuration");
        assert_eq!(
            &bcn[..],
            &[
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
                0, 0, // Capabilities
                // IEs:
                0, 7, 99, 111, 111, 108, 110, 101, 116, // SSID
                1, 1, 248, // Supported rates
                3, 1, 1, // DSSS parameter set
                5, 4, 1, 2, 0, 2, // TIM
            ][..],
        );
        bss.handle_bcn_tx_complete_indication(&mut ctx)
            .expect("expected handle bcn tx complete hw indication OK");

        bss.handle_pre_tbtt_hw_indication(&mut ctx)
            .expect("expected handle pre-TBTT hw indication OK");
        let (bcn, _, _) = fake_device.bcn_cfg.as_ref().expect("expected beacon configuration");
        assert_eq!(
            &bcn[..],
            &[
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
                0, 0, // Capabilities
                // IEs:
                0, 7, 99, 111, 111, 108, 110, 101, 116, // SSID
                1, 1, 248, // Supported rates
                3, 1, 1, // DSSS parameter set
                5, 4, 0, 2, 0, 2, // TIM
            ][..],
        );
        bss.handle_bcn_tx_complete_indication(&mut ctx)
            .expect("expected handle bcn tx complete hw indication OK");
    }

    #[test]
    fn handle_pre_tbtt_hw_indication_has_group_traffic() {
        let mut fake_device = FakeDevice::new();
        let mut fake_scheduler = FakeScheduler::new();
        let mut ctx = make_context(fake_device.as_device(), fake_scheduler.as_scheduler());
        let mut bss = InfraBss::new(
            &mut ctx,
            b"coolnet".to_vec(),
            TimeUnit::DEFAULT_BEACON_INTERVAL,
            2,
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
        client.set_power_state(&mut ctx, mac::PowerState::DOZE).expect("expect doze OK");

        bss.handle_eth_frame(
            &mut ctx,
            EthernetIIHdr {
                da: [1u8; 6],
                sa: CLIENT_ADDR2,
                ether_type: BigEndianU16::from_native(0x1234),
            },
            &[1, 2, 3, 4, 5][..],
        )
        .expect("expected OK");

        bss.handle_pre_tbtt_hw_indication(&mut ctx)
            .expect("expected handle pre-TBTT hw indication OK");
        let (bcn, _, _) = fake_device.bcn_cfg.as_ref().expect("expected beacon configuration");
        assert_eq!(
            &bcn[..],
            &[
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
                0, 0, // Capabilities
                // IEs:
                0, 7, 99, 111, 111, 108, 110, 101, 116, // SSID
                1, 1, 248, // Supported rates
                3, 1, 1, // DSSS parameter set
                5, 4, 0, 2, 1, 0, // TIM
            ][..],
        );
        fake_device.wlan_queue.clear();
        bss.handle_bcn_tx_complete_indication(&mut ctx)
            .expect("expected handle bcn tx complete hw indication OK");

        assert_eq!(fake_device.wlan_queue.len(), 1);
        assert_eq!(
            &fake_device.wlan_queue[0].0[..],
            &[
                // Mgmt header
                0b00001000, 0b00000010, // Frame Control
                0, 0, // Duration
                1, 1, 1, 1, 1, 1, // addr1
                2, 2, 2, 2, 2, 2, // addr2
                6, 6, 6, 6, 6, 6, // addr3
                0x10, 0, // Sequence Control
                0xAA, 0xAA, 0x03, // DSAP, SSAP, Control, OUI
                0, 0, 0, // OUI
                0x12, 0x34, // Protocol ID
                // Data
                1, 2, 3, 4, 5,
            ][..]
        );

        bss.handle_pre_tbtt_hw_indication(&mut ctx)
            .expect("expected handle pre-TBTT hw indication OK");
        let (bcn, _, _) = fake_device.bcn_cfg.as_ref().expect("expected beacon configuration");
        assert_eq!(
            &bcn[..],
            &[
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
                0, 0, // Capabilities
                // IEs:
                0, 7, 99, 111, 111, 108, 110, 101, 116, // SSID
                1, 1, 248, // Supported rates
                3, 1, 1, // DSSS parameter set
                5, 4, 2, 2, 0, 0, // TIM
            ][..],
        );
    }
}
