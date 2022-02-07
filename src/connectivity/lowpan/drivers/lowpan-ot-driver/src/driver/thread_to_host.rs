// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;

use futures::prelude::*;

use lowpan_driver_common::spinel::Subnet;

impl<OT, NI> OtDriver<OT, NI>
where
    OT: Send + ot::InstanceInterface,
    NI: NetworkInterface,
{
    pub fn on_ot_ip6_receive(&self, msg: OtMessageBox<'_>) {
        // NOTE: DRIVER STATE IS ALREADY LOCKED WHEN THIS IS CALLED!
        //       Calling `lock()` on the driver state will deadlock!

        if !msg.is_link_security_enabled() {
            // TODO: Check firewall.
            return;
        }

        // Unfortunately we must render the packet out before we can pass it along.
        let packet = msg.to_vec();

        if let Err(err) = self.net_if.inbound_packet_to_stack(&packet).now_or_never().transpose() {
            error!("Unable to send packet to netstack: {:?}", err);
        }
    }

    pub(crate) async fn on_ot_state_change(
        &self,
        flags: ot::ChangedFlags,
    ) -> Result<(), anyhow::Error> {
        fx_log_debug!("OpenThread State Change: {:?}", flags);
        self.update_connectivity_state();

        // TODO(rquattle): Consider make this a little more selective, this async-condition
        //                 is a bit of a big hammer.
        if flags.contains(
            ot::ChangedFlags::THREAD_NETWORK_NAME
                | ot::ChangedFlags::THREAD_CHANNEL
                | ot::ChangedFlags::THREAD_PANID
                | ot::ChangedFlags::THREAD_EXT_PANID
                | ot::ChangedFlags::THREAD_ROLE
                | ot::ChangedFlags::JOINER_STATE
                | ot::ChangedFlags::ACTIVE_DATASET,
        ) {
            self.driver_state_change.trigger();
        }

        Ok(())
    }

    pub(crate) fn on_ot_ip6_address_info(&self, info: ot::Ip6AddressInfo<'_>, is_added: bool) {
        // NOTE: DRIVER STATE IS LOCKED WHEN THIS IS CALLED!
        //       Calling `lock()` on the driver state will deadlock!

        trace!("on_ot_ip6_address_info: is_added:{} {:?}", is_added, info);

        let subnet = Subnet { addr: *info.addr(), prefix_len: info.prefix_len() };
        if info.is_multicast() {
            if is_added {
                debug!("OpenThread JOINED multicast group: {:?}", info);
                if let Err(err) = self.net_if.join_mcast_group(info.addr()).ignore_already_exists()
                {
                    warn!("Unable to join multicast group `{:?}`: {:?}", subnet, err);
                }
            } else {
                debug!("OpenThread LEFT multicast group: {:?}", info);
                if let Err(err) = self.net_if.leave_mcast_group(info.addr()).ignore_already_exists()
                {
                    warn!("Unable to leave multicast group `{:?}`: {:?}", subnet, err);
                }
            }
        } else {
            if is_added {
                debug!("OpenThread ADDED address: {:?}", info);
                if let Err(err) = self.net_if.add_address(&subnet).ignore_already_exists() {
                    fx_log_warn!("Unable to add address `{:?}` to interface: {:?}", subnet, err);
                }
            } else {
                debug!("OpenThread REMOVED address: {:?}", info);
                if let Err(err) = self.net_if.remove_address(&subnet).ignore_not_found() {
                    fx_log_warn!(
                        "Unable to remove address `{:?}` from interface: {:?}",
                        subnet,
                        err
                    );
                }
            }
        }
    }
}
