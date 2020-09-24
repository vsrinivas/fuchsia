// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        client::{channel_scheduler, scanner::Scanner, Client, Context, TimedEvent},
        device::Device,
        timer::Timer,
    },
    banjo_ddk_protocol_wlan_info as banjo_wlan_info,
    log::{debug, error, warn},
};

#[cfg(test)]
pub use test_utils::*;

/// Listeners to channel events from ChannelScheduler
pub trait ChannelListener {
    fn device(&mut self) -> &mut Device;
    fn timer(&mut self) -> &mut Timer<TimedEvent>;

    /// Triggered by ChannelScheduler before switching to a requested channel.
    /// Note that if existing channel is the same as the new channel, this event is still emitted,
    /// primarily so client can be notified that request is about to be served.
    fn on_pre_switch_channel(
        &mut self,
        from: banjo_wlan_info::WlanChannel,
        to: banjo_wlan_info::WlanChannel,
        request_id: channel_scheduler::RequestId,
    );
    /// Triggered by ChannelScheduler after switching to a requested channel.
    /// Note that if existing channel is the same as the new channel, this event is still emitted,
    /// primarily so client can be notified that request has started.
    fn on_post_switch_channel(
        &mut self,
        from: banjo_wlan_info::WlanChannel,
        to: banjo_wlan_info::WlanChannel,
        request_id: channel_scheduler::RequestId,
    );
    /// Triggered when request is fully serviced, or the request is interrupted but will not
    /// be retried anymore.
    fn on_req_complete(
        &mut self,
        request_id: channel_scheduler::RequestId,
        queue_state: channel_scheduler::QueueState,
    );
}

/// Source of channel scheduling event. Used to help ChannelListener differentiate where
/// a channel event comes from.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum ChannelListenerSource {
    Scanner,
    Others,
}

#[derive(Default)]
pub struct ChannelListenerState {
    pub(crate) main_channel: Option<banjo_wlan_info::WlanChannel>,
    pub(crate) off_channel_req_id: Option<channel_scheduler::RequestId>,
}

impl ChannelListenerState {
    pub fn bind<'a>(
        &'a mut self,
        ctx: &'a mut Context,
        scanner: &'a mut Scanner,
        station: Option<&'a mut Client>,
    ) -> MlmeChannelListener<'a> {
        MlmeChannelListener { state: self, ctx, scanner, station }
    }
}

pub struct MlmeChannelListener<'a> {
    state: &'a mut ChannelListenerState,
    ctx: &'a mut Context,
    scanner: &'a mut Scanner,
    station: Option<&'a mut Client>,
}

impl<'a> ChannelListener for MlmeChannelListener<'a> {
    fn device(&mut self) -> &mut Device {
        &mut self.ctx.device
    }

    fn timer(&mut self) -> &mut Timer<TimedEvent> {
        &mut self.ctx.timer
    }

    fn on_pre_switch_channel(
        &mut self,
        from: banjo_wlan_info::WlanChannel,
        to: banjo_wlan_info::WlanChannel,
        _request_id: channel_scheduler::RequestId,
    ) {
        let main_channel = match self.state.main_channel {
            Some(chan) => chan,
            None => return,
        };
        let station = match &mut self.station {
            Some(station) => station,
            None => return,
        };
        // on -> off channel
        if from == main_channel && to != main_channel {
            station.pre_switch_off_channel(&mut self.ctx);
        }
    }

    fn on_post_switch_channel(
        &mut self,
        from: banjo_wlan_info::WlanChannel,
        to: banjo_wlan_info::WlanChannel,
        request_id: channel_scheduler::RequestId,
    ) {
        if let ChannelListenerSource::Scanner = request_id.1 {
            let old_id = self.state.off_channel_req_id.replace(request_id);
            if old_id.is_some() && old_id != self.state.off_channel_req_id {
                warn!("evicted old off-channel request from listener");
            }
            self.scanner.bind(self.ctx).begin_requested_channel_time(to);
        }

        // off -> on channel
        let main_channel = match self.state.main_channel {
            Some(chan) => chan,
            None => return,
        };
        let station = match &mut self.station {
            Some(station) => station,
            None => return,
        };
        if from != main_channel && to == main_channel {
            station.handle_back_on_channel(&mut self.ctx);
        }
    }

    fn on_req_complete(
        &mut self,
        request_id: channel_scheduler::RequestId,
        queue_state: channel_scheduler::QueueState,
    ) {
        if Some(request_id) == self.state.off_channel_req_id {
            self.state.off_channel_req_id.take();
            self.scanner.bind(self.ctx).handle_channel_req_complete();
        }

        match (queue_state, self.state.main_channel) {
            (channel_scheduler::QueueState::Empty, Some(main_channel))
                if self.ctx.device.channel() != main_channel =>
            {
                debug!("Reverting back to main_channel {:?}", main_channel);
                if let Err(e) = self.ctx.device.set_channel(main_channel) {
                    error!("Unable to revert back to main channel {:?}", e);
                }
                if let Some(station) = self.station.as_mut() {
                    station.handle_back_on_channel(&mut self.ctx);
                }
            }
            _ => (),
        }
    }
}

#[cfg(test)]
mod test_utils {
    use {
        super::*,
        std::{cell::RefCell, rc::Rc},
    };

    pub struct MockListenerState {
        pub events: Rc<RefCell<Vec<LEvent>>>,
    }

    impl MockListenerState {
        pub fn drain_events(&mut self) -> Vec<LEvent> {
            self.events.borrow_mut().drain(..).collect()
        }

        pub fn create_channel_listener_fn<'a>(
            &self,
        ) -> impl FnOnce(&'a mut Context, &mut Scanner) -> MockListener<'a> {
            let events = Rc::clone(&self.events);
            |ctx, _| MockListener { events, device: &mut ctx.device, timer: &mut ctx.timer }
        }

        pub fn bind<'a>(
            &self,
            device: &'a mut Device,
            timer: &'a mut Timer<TimedEvent>,
        ) -> MockListener<'a> {
            MockListener { events: Rc::clone(&self.events), device, timer }
        }
    }

    pub struct MockListener<'a> {
        events: Rc<RefCell<Vec<LEvent>>>,
        device: &'a mut Device,
        timer: &'a mut Timer<TimedEvent>,
    }

    #[derive(Debug, PartialEq)]
    pub enum LEvent {
        PreSwitch {
            from: banjo_wlan_info::WlanChannel,
            to: banjo_wlan_info::WlanChannel,
            req_id: channel_scheduler::RequestId,
        },
        PostSwitch {
            from: banjo_wlan_info::WlanChannel,
            to: banjo_wlan_info::WlanChannel,
            req_id: channel_scheduler::RequestId,
        },
        ReqComplete(channel_scheduler::RequestId, channel_scheduler::QueueState),
    }

    impl ChannelListener for MockListener<'_> {
        fn device(&mut self) -> &mut Device {
            self.device
        }

        fn timer(&mut self) -> &mut Timer<TimedEvent> {
            self.timer
        }

        fn on_pre_switch_channel(
            &mut self,
            from: banjo_wlan_info::WlanChannel,
            to: banjo_wlan_info::WlanChannel,
            request_id: channel_scheduler::RequestId,
        ) {
            self.events.borrow_mut().push(LEvent::PreSwitch { from, to, req_id: request_id });
        }

        fn on_post_switch_channel(
            &mut self,
            from: banjo_wlan_info::WlanChannel,
            to: banjo_wlan_info::WlanChannel,
            request_id: channel_scheduler::RequestId,
        ) {
            self.events.borrow_mut().push(LEvent::PostSwitch { from, to, req_id: request_id });
        }

        fn on_req_complete(
            &mut self,
            req_id: channel_scheduler::RequestId,
            queue_state: channel_scheduler::QueueState,
        ) {
            self.events.borrow_mut().push(LEvent::ReqComplete(req_id, queue_state));
        }
    }
}
