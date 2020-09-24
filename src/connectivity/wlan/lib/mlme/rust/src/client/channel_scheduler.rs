// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        client::{
            channel_listener::{ChannelListener, ChannelListenerSource},
            TimedEvent,
        },
        timer::EventId,
    },
    banjo_ddk_protocol_wlan_info::*,
    fuchsia_zircon as zx,
    log::{debug, error},
    std::collections::VecDeque,
};

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct RequestId(pub u64, pub ChannelListenerSource);

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum QueueState {
    Nonempty,
    Empty,
}

/// ChannelScheduler let the client schedule to go to some channel at a particular time or
/// queue up channel to be transitioned on the best-effort basis. Scheduled channel request
/// has higher priority over queued request and would cancel in-progress queued request.
/// How two conflicting scheduled requests should interact still need to be fleshed out,
/// but right now a scheduled request would cancel existing scheduled request.
///
/// Currently, time-based scheduling is not fully supported, with the only option being
/// scheduling to go to a channel now.
pub struct ChannelScheduler {
    queue: ChannelQueue,
    request_id: u64,
    timeout_id: Option<EventId>,
}

impl ChannelScheduler {
    pub fn new() -> Self {
        Self { queue: ChannelQueue::new(), request_id: 0, timeout_id: None }
    }

    pub fn bind<'a, CL: ChannelListener>(
        &'a mut self,
        listener: &'a mut CL,
        source: ChannelListenerSource,
    ) -> BoundChannelScheduler<'a, CL> {
        BoundChannelScheduler { chan_sched: self, listener, source }
    }
}

pub struct BoundChannelScheduler<'a, CL> {
    chan_sched: &'a mut ChannelScheduler,
    listener: &'a mut CL,
    source: ChannelListenerSource,
}

impl<'a, CL: ChannelListener> BoundChannelScheduler<'a, CL> {
    /// Switch to requested channel immediately, cancelling any channel request that's in
    /// progress. This is a precursor to a `schedule_channel` method that takes in the
    /// desired time to switch channel.
    pub fn schedule_immediate(
        &mut self,
        channel: WlanChannel,
        duration: zx::Duration,
    ) -> RequestId {
        self.chan_sched.request_id += 1;
        let request_id = RequestId(self.chan_sched.request_id, self.source);
        let req = ChannelRequest::Single {
            channel,
            meta: ChannelRequestMeta { dwell_time: duration, retryable: false, request_id },
        };
        self.schedule_channels_helper(req, ChannelPolicy::Immediate);
        request_id
    }

    /// Queue channels to be switched to on a best-effort basis. A queued channel is serviced
    /// as long as it does not conflict with another scheduled channel request. A queued
    /// channel that's being serviced may not reach its full dwell time due to incoming
    /// scheduled request, in which case it will be retried at a later time.
    pub fn queue_channels(
        &mut self,
        channels: Vec<WlanChannel>,
        duration: zx::Duration,
    ) -> RequestId {
        self.chan_sched.request_id += 1;
        let request_id = RequestId(self.chan_sched.request_id, self.source);
        let req = ChannelRequest::List {
            channels,
            current_idx: 0,
            meta: ChannelRequestMeta { dwell_time: duration, retryable: true, request_id },
        };
        self.schedule_channels_helper(req, ChannelPolicy::Queue);
        request_id
    }

    fn schedule_channels_helper(&mut self, req: ChannelRequest, policy: ChannelPolicy) {
        match policy {
            ChannelPolicy::Immediate => {
                // Cancel any ongoing request. Note that the new request may be on the same
                // channel, but we will still "cancel" it.
                self.cancel_existing_timeout();
                let interrupted = true;
                let ended_request_id = self.chan_sched.queue.mark_end_current_channel(interrupted);

                // Queue and service new request
                self.chan_sched.queue.push_front(req);
                self.maybe_service_front_req();

                // Notify completion of previously cancelled request, if any
                if let Some(id) = ended_request_id {
                    self.listener.on_req_complete(id, QueueState::Nonempty);
                }
            }
            ChannelPolicy::Queue => {
                self.chan_sched.queue.push_back(req);
                if !self.busy() {
                    self.maybe_service_front_req();
                }
            }
        }
    }

    /// Handle end of channel period. Returning true if channel scheduler is still servicing
    /// requests.
    pub fn handle_timeout(&mut self) {
        self.cancel_existing_timeout();
        let interrupted = false;
        let ended_request_id = self.chan_sched.queue.mark_end_current_channel(interrupted);
        self.maybe_service_front_req();
        if let Some(id) = ended_request_id {
            let queue_state = if self.chan_sched.queue.front().is_some() {
                QueueState::Nonempty
            } else {
                QueueState::Empty
            };
            self.listener.on_req_complete(id, queue_state);
        }
    }

    /// Service request at front of the queue if there's one.
    fn maybe_service_front_req(&mut self) {
        let (channel, meta) = match self.chan_sched.queue.front() {
            Some(info) => info,
            None => return,
        };

        let existing_channel = self.listener.device().channel();
        self.listener.on_pre_switch_channel(existing_channel, channel, meta.request_id);
        if existing_channel != channel {
            debug!("Channel Scheduler switching channel {:?} -> {:?}", existing_channel, channel);
            if let Err(e) = self.listener.device().set_channel(channel) {
                error!("Failed setting channel {:?} - error {}", channel, e);
            }
        } else {
            debug!(
                "Channel Scheduler skipped channel switch {:?} -> {:?}",
                existing_channel, channel
            );
        }
        let deadline = self.listener.timer().now() + meta.dwell_time;
        self.chan_sched.timeout_id =
            Some(self.listener.timer().schedule_event(deadline, TimedEvent::ChannelScheduler));
        self.listener.on_post_switch_channel(existing_channel, channel, meta.request_id);
    }

    fn cancel_existing_timeout(&mut self) {
        if let Some(timeout_id) = self.chan_sched.timeout_id.take() {
            self.listener.timer().cancel_event(timeout_id);
        }
    }

    fn busy(&self) -> bool {
        self.chan_sched.timeout_id.is_some()
    }
}

enum ChannelPolicy {
    /// Channel is immediately transitioned to. Any current ongoing channel request is canceled.
    Immediate,
    /// Channel request is put at the back of the queue. It may be started immediately if there's
    /// no other ongoing or pending request.
    Queue,
}

struct ChannelQueue {
    queue: VecDeque<ChannelRequest>,
}

impl ChannelQueue {
    fn new() -> Self {
        Self { queue: VecDeque::new() }
    }

    fn push_front(&mut self, req: ChannelRequest) {
        self.queue.push_front(req)
    }

    fn push_back(&mut self, req: ChannelRequest) {
        self.queue.push_back(req)
    }

    fn front(&self) -> Option<(WlanChannel, ChannelRequestMeta)> {
        self.queue.front().map(|req| match req {
            ChannelRequest::Single { channel, meta } => (*channel, *meta),
            ChannelRequest::List { channels, current_idx, meta } => (channels[*current_idx], *meta),
        })
    }

    /// Marked end of current channel request.
    /// Return request ID if request is complete or cancelled.
    fn mark_end_current_channel(&mut self, interrupted: bool) -> Option<RequestId> {
        let complete_or_cancelled = self.queue.front_mut().map(|req| match req {
            ChannelRequest::Single { meta, .. } => !interrupted || !meta.retryable,
            ChannelRequest::List { channels, current_idx, meta } => {
                let should_retry = interrupted && meta.retryable;
                if should_retry {
                    return false;
                }
                if *current_idx + 1 < channels.len() {
                    *current_idx += 1;
                    false
                } else {
                    true
                }
            }
        });

        if let Some(true) = complete_or_cancelled {
            return self.queue.pop_front().map(|req| req.request_id());
        }
        None
    }
}

#[derive(Debug, Copy, Clone)]
struct ChannelRequestMeta {
    dwell_time: zx::Duration,
    request_id: RequestId,
    retryable: bool,
}

#[derive(Debug, Clone)]
enum ChannelRequest {
    Single { channel: WlanChannel, meta: ChannelRequestMeta },
    List { channels: Vec<WlanChannel>, current_idx: usize, meta: ChannelRequestMeta },
}

impl ChannelRequest {
    fn request_id(&self) -> RequestId {
        match self {
            Self::Single { meta, .. } | Self::List { meta, .. } => meta.request_id,
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{
            client::channel_listener::{LEvent, MockListenerState},
            device::{Device, FakeDevice},
            timer::{FakeScheduler, Timer},
        },
        fuchsia_zircon::prelude::DurationNum,
        std::{cell::RefCell, rc::Rc},
    };

    const IMMEDIATE_CHANNEL: WlanChannel =
        WlanChannel { primary: 11, cbw: WlanChannelBandwidth::_40, secondary80: 0 };

    const QUEUE_CHANNEL_1: WlanChannel =
        WlanChannel { primary: 5, cbw: WlanChannelBandwidth::_20, secondary80: 0 };

    const QUEUE_CHANNEL_2: WlanChannel =
        WlanChannel { primary: 6, cbw: WlanChannelBandwidth::_20, secondary80: 0 };

    #[test]
    fn test_schedule_immediate_on_empty_queue() {
        let mut m = MockObjects::new();
        let mut chan_sched = ChannelScheduler::new();
        let mut listener = m.listener_state.bind(&mut m.device, &mut m.timer);
        let mut chan_sched = chan_sched.bind(&mut listener, ChannelListenerSource::Others);

        let from = m.fake_device.wlan_channel;
        let req_id = chan_sched.schedule_immediate(IMMEDIATE_CHANNEL, 100.millis());
        assert_eq!(m.fake_device.wlan_channel, IMMEDIATE_CHANNEL);
        assert_eq!(
            m.listener_state.drain_events(),
            vec![
                LEvent::PreSwitch { from, to: IMMEDIATE_CHANNEL, req_id },
                LEvent::PostSwitch { from, to: IMMEDIATE_CHANNEL, req_id },
            ]
        );

        // Trigger timeout, should stay on same channel and notify req complete.
        chan_sched.handle_timeout();
        assert_eq!(m.fake_device.wlan_channel, IMMEDIATE_CHANNEL);
        assert_eq!(
            m.listener_state.drain_events(),
            vec![LEvent::ReqComplete(req_id, QueueState::Empty)]
        );
    }

    #[test]
    fn test_queue_channels_on_empty_queue() {
        let mut m = MockObjects::new();
        let mut chan_sched = ChannelScheduler::new();
        let mut listener = m.listener_state.bind(&mut m.device, &mut m.timer);
        let mut chan_sched = chan_sched.bind(&mut listener, ChannelListenerSource::Others);

        let from = m.fake_device.wlan_channel;
        let req_id = chan_sched.queue_channels(vec![QUEUE_CHANNEL_1], 200.millis());
        assert_eq!(m.fake_device.wlan_channel, QUEUE_CHANNEL_1);
        assert_eq!(
            m.listener_state.drain_events(),
            vec![
                LEvent::PreSwitch { from, to: QUEUE_CHANNEL_1, req_id },
                LEvent::PostSwitch { from, to: QUEUE_CHANNEL_1, req_id },
            ]
        );

        // Trigger timeout, should stay on same channel and notify req complete.
        chan_sched.handle_timeout();
        assert_eq!(m.fake_device.wlan_channel, QUEUE_CHANNEL_1);
        assert_eq!(
            m.listener_state.drain_events(),
            vec![LEvent::ReqComplete(req_id, QueueState::Empty)]
        );
    }

    #[test]
    fn test_schedule_immediate_interrupting_queued_request() {
        let mut m = MockObjects::new();
        let mut chan_sched = ChannelScheduler::new();
        let mut listener = m.listener_state.bind(&mut m.device, &mut m.timer);
        let mut chan_sched = chan_sched.bind(&mut listener, ChannelListenerSource::Others);

        // Schedule back request. Should switch immediately since queue is empty.
        let id1 = chan_sched.queue_channels(vec![QUEUE_CHANNEL_1], 200.millis());
        assert_eq!(m.fake_device.wlan_channel, QUEUE_CHANNEL_1);
        m.listener_state.drain_events();

        // Schedule immediate request. Verify switch immediately.
        let id2 = chan_sched.schedule_immediate(IMMEDIATE_CHANNEL, 100.millis());
        assert_eq!(m.fake_device.wlan_channel, IMMEDIATE_CHANNEL);
        assert_eq!(
            m.listener_state.drain_events(),
            vec![
                LEvent::PreSwitch { from: QUEUE_CHANNEL_1, to: IMMEDIATE_CHANNEL, req_id: id2 },
                LEvent::PostSwitch { from: QUEUE_CHANNEL_1, to: IMMEDIATE_CHANNEL, req_id: id2 },
            ]
        );

        // Trigger timeout, should retry first channel request.
        chan_sched.handle_timeout();
        assert_eq!(m.fake_device.wlan_channel, QUEUE_CHANNEL_1);
        assert_eq!(
            m.listener_state.drain_events(),
            vec![
                LEvent::PreSwitch { from: IMMEDIATE_CHANNEL, to: QUEUE_CHANNEL_1, req_id: id1 },
                LEvent::PostSwitch { from: IMMEDIATE_CHANNEL, to: QUEUE_CHANNEL_1, req_id: id1 },
                LEvent::ReqComplete(id2, QueueState::Nonempty),
            ]
        );

        // Trigger timeout, should stay on same channel and notify first req complete.
        chan_sched.handle_timeout();
        assert_eq!(m.fake_device.wlan_channel, QUEUE_CHANNEL_1);
        assert_eq!(
            m.listener_state.drain_events(),
            vec![LEvent::ReqComplete(id1, QueueState::Empty)]
        );
    }

    #[test]
    fn test_queuing_channels() {
        let mut m = MockObjects::new();
        let mut chan_sched = ChannelScheduler::new();
        let mut listener = m.listener_state.bind(&mut m.device, &mut m.timer);
        let mut chan_sched = chan_sched.bind(&mut listener, ChannelListenerSource::Others);

        let id1 = chan_sched.schedule_immediate(IMMEDIATE_CHANNEL, 100.millis());
        assert_eq!(m.fake_device.wlan_channel, IMMEDIATE_CHANNEL);
        m.listener_state.drain_events();

        // Queue back request, which should not fire yet.
        let id2 = chan_sched.queue_channels(vec![QUEUE_CHANNEL_1, QUEUE_CHANNEL_2], 200.millis());
        assert_eq!(m.fake_device.wlan_channel, IMMEDIATE_CHANNEL);
        assert_eq!(m.listener_state.drain_events(), vec![]);

        // Trigger timeout, should transition to first back channel.
        chan_sched.handle_timeout();
        assert_eq!(m.fake_device.wlan_channel, QUEUE_CHANNEL_1);
        assert_eq!(
            m.listener_state.drain_events(),
            vec![
                LEvent::PreSwitch { from: IMMEDIATE_CHANNEL, to: QUEUE_CHANNEL_1, req_id: id2 },
                LEvent::PostSwitch { from: IMMEDIATE_CHANNEL, to: QUEUE_CHANNEL_1, req_id: id2 },
                LEvent::ReqComplete(id1, QueueState::Nonempty),
            ]
        );

        // Trigger timeout, should transition to second back channel
        chan_sched.handle_timeout();
        assert_eq!(m.fake_device.wlan_channel, QUEUE_CHANNEL_2);
        assert_eq!(
            m.listener_state.drain_events(),
            vec![
                LEvent::PreSwitch { from: QUEUE_CHANNEL_1, to: QUEUE_CHANNEL_2, req_id: id2 },
                LEvent::PostSwitch { from: QUEUE_CHANNEL_1, to: QUEUE_CHANNEL_2, req_id: id2 },
            ]
        );

        // Trigger timeout, should stay on same channel and notify req complete
        chan_sched.handle_timeout();
        assert_eq!(m.fake_device.wlan_channel, QUEUE_CHANNEL_2);
        assert_eq!(
            m.listener_state.drain_events(),
            vec![LEvent::ReqComplete(id2, QueueState::Empty)]
        );
    }

    #[test]
    fn test_transitioning_to_same_channel_still_trigger_events() {
        let mut m = MockObjects::new();
        let mut chan_sched = ChannelScheduler::new();
        let mut listener = m.listener_state.bind(&mut m.device, &mut m.timer);
        let mut chan_sched = chan_sched.bind(&mut listener, ChannelListenerSource::Others);

        let id1 = chan_sched.schedule_immediate(IMMEDIATE_CHANNEL, 100.millis());
        assert_eq!(m.fake_device.wlan_channel, IMMEDIATE_CHANNEL);
        m.listener_state.drain_events();

        // Queue back request, which should not fire yet
        let id2 = chan_sched.queue_channels(vec![IMMEDIATE_CHANNEL], 200.millis());
        assert_eq!(m.fake_device.wlan_channel, IMMEDIATE_CHANNEL);
        assert_eq!(m.listener_state.drain_events(), vec![]);

        // Trigger timeout, should transition to first "back" channel
        chan_sched.handle_timeout();
        assert_eq!(m.fake_device.wlan_channel, IMMEDIATE_CHANNEL);
        assert_eq!(
            m.listener_state.drain_events(),
            vec![
                LEvent::PreSwitch { from: IMMEDIATE_CHANNEL, to: IMMEDIATE_CHANNEL, req_id: id2 },
                LEvent::PostSwitch { from: IMMEDIATE_CHANNEL, to: IMMEDIATE_CHANNEL, req_id: id2 },
                LEvent::ReqComplete(id1, QueueState::Nonempty),
            ]
        );

        // Trigger timeout, should stay on same channel and notify req complete
        chan_sched.handle_timeout();
        assert_eq!(m.fake_device.wlan_channel, IMMEDIATE_CHANNEL);
        assert_eq!(
            m.listener_state.drain_events(),
            vec![LEvent::ReqComplete(id2, QueueState::Empty)]
        );
    }

    #[test]
    fn test_schedule_immediate_cancels_existing_scheduled_request() {
        // Current behavior is that a scheduled request cancels an existing scheduled request.
        // An alternative is to reject new scheduled request if it prevents ChannelScheduler
        // from fulfilling existing one.
        //
        // In practice, scheduled request is only used on the same main channel, so either
        // behavior works.
        let mut m = MockObjects::new();
        let mut chan_sched = ChannelScheduler::new();
        let mut listener = m.listener_state.bind(&mut m.device, &mut m.timer);
        let mut chan_sched = chan_sched.bind(&mut listener, ChannelListenerSource::Others);

        let id1 = chan_sched.schedule_immediate(IMMEDIATE_CHANNEL, 100.millis());
        assert_eq!(m.fake_device.wlan_channel, IMMEDIATE_CHANNEL);
        m.listener_state.drain_events();

        // Switch channel immediately. Verify existing one is marked complete (cancelled).
        let id2 = chan_sched.schedule_immediate(IMMEDIATE_CHANNEL, 100.millis());
        assert_eq!(m.fake_device.wlan_channel, IMMEDIATE_CHANNEL);
        assert_eq!(
            m.listener_state.drain_events(),
            vec![
                LEvent::PreSwitch { from: IMMEDIATE_CHANNEL, to: IMMEDIATE_CHANNEL, req_id: id2 },
                LEvent::PostSwitch { from: IMMEDIATE_CHANNEL, to: IMMEDIATE_CHANNEL, req_id: id2 },
                LEvent::ReqComplete(id1, QueueState::Nonempty)
            ]
        );

        // Trigger timeout, should stay on same channel and notify req complete
        chan_sched.handle_timeout();
        assert_eq!(m.fake_device.wlan_channel, IMMEDIATE_CHANNEL);
        assert_eq!(
            m.listener_state.drain_events(),
            vec![LEvent::ReqComplete(id2, QueueState::Empty)]
        );
    }

    struct MockObjects {
        fake_device: Box<FakeDevice>,
        _fake_scheduler: Box<FakeScheduler>,
        device: Device,
        timer: Timer<TimedEvent>,
        listener_state: MockListenerState,
    }

    impl MockObjects {
        fn new() -> Self {
            let mut fake_device = Box::new(FakeDevice::new());
            let mut fake_scheduler = Box::new(FakeScheduler::new());
            let device = fake_device.as_device();
            let timer = Timer::<TimedEvent>::new(fake_scheduler.as_scheduler());
            Self {
                fake_device,
                _fake_scheduler: fake_scheduler,
                device,
                timer,
                listener_state: MockListenerState { events: Rc::new(RefCell::new(vec![])) },
            }
        }
    }
}
