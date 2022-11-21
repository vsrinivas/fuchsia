// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_ui_composition as ui_comp,
    flatland_frame_scheduling_lib::*,
    fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::channel::{
        mpsc::{UnboundedReceiver, UnboundedSender},
        oneshot,
    },
    futures::prelude::*,
    std::collections::VecDeque,
    std::rc::Weak,
    tracing::warn,
};

/// Unbounded sender used for presentation messages.
pub type PresentationSender = UnboundedSender<oneshot::Sender<()>>;

/// Unbounded receiver used for presentation messages.
pub type PresentationReceiver = UnboundedReceiver<oneshot::Sender<()>>;

pub fn start_flatland_presentation_loop(
    mut receiver: PresentationReceiver,
    weak_flatland: Weak<ui_comp::FlatlandProxy>,
) {
    fasync::Task::local(async move {
        let scheduler = ThroughputScheduler::new();
        let mut flatland_event_stream = {
            if let Some(flatland) = weak_flatland.upgrade() {
                flatland.take_event_stream()
            } else {
                panic!(
                    "failed to upgrade Flatand weak ref"
                );
            }
        };

        let mut channels_awaiting_pingback = VecDeque::from([Vec::new()]);

        loop {
            futures::select! {
                message = receiver.next().fuse() => {
                    match message {
                        Some(channel) => {
                            channels_awaiting_pingback.back_mut().unwrap().push(channel);
                            scheduler.request_present();
                        }
                        None => {}
                    }
                }
                flatland_event = flatland_event_stream.next().fuse() => {
                    match flatland_event {
                        Some(Ok(ui_comp::FlatlandEvent::OnNextFrameBegin{ values })) => {
                            let credits = values
                                          .additional_present_credits
                                          .expect("Present credits must exist");
                            let infos = values
                                .future_presentation_infos
                                .expect("Future presentation infos must exist")
                                .iter()
                                .map(
                                |x| PresentationInfo{
                                    latch_point: zx::Time::from_nanos(x.latch_point.unwrap()),
                                    presentation_time: zx::Time::from_nanos(
                                                        x.presentation_time.unwrap())
                                })
                                .collect();
                            scheduler.on_next_frame_begin(credits, infos);
                        }
                        Some(Ok(ui_comp::FlatlandEvent::OnFramePresented{ frame_presented_info })) => {
                            let actual_presentation_time =
                                zx::Time::from_nanos(frame_presented_info.actual_presentation_time);
                            let presented_infos: Vec<PresentedInfo> =
                                frame_presented_info.presentation_infos
                                .into_iter()
                                .map(|x| x.into())
                                .collect();

                            scheduler.on_frame_presented(actual_presentation_time, presented_infos);
                        }
                        Some(Ok(ui_comp::FlatlandEvent::OnError{ error })) => {
                            panic!(
                                "Received FlatlandError code: {}",
                                error.into_primitive()
                            );
                        }
                        _ => {
                            warn!(
                                "Flatland event stream closed; exiting. This message may be expected during test teardown");
                                return;
                        }
                    }
                }
                present_parameters = scheduler.wait_to_update().fuse() => {
                    if let Some(flatland) = weak_flatland.upgrade() {
                        flatland
                            .present(present_parameters.into())
                            .expect("Present failed");
                    } else {
                        warn!(
                            "Failed to upgrade Flatand weak ref; exiting listener loop"
                        );
                        return;
                    }
            }
        }
    }})
    .detach()
}
