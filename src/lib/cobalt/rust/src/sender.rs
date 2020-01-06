// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Defines `CobaltSender` to encapsulate encoding `CobaltEvent` and sending them to the Cobalt
//! FIDL service.

use {
    cobalt_client::traits::AsEventCodes,
    fidl_fuchsia_cobalt::{CobaltEvent, CountEvent, EventPayload, HistogramBucket},
    futures::channel::mpsc,
    log::{error, info},
    std::sync::{
        atomic::{AtomicBool, Ordering},
        Arc,
    },
};

macro_rules! gen_comment {
    ($x:expr) => {
        #[doc = $x]
        extern {}
    };
    ($x:expr, $($tt:tt)*) => {
        #[doc = $x]
        $($tt)*
    };
}

/// Wraps around an `mpsc::Sender` to allow sending CobaltEvents asynchronously to the Cobalt
/// FIDL service.
#[derive(Clone, Debug)]
pub struct CobaltSender {
    sender: mpsc::Sender<CobaltEvent>,
    is_blocked: Arc<AtomicBool>,
}

macro_rules! impl_wrapped_methods {
    ($(#[$variant:ident] $name:ident($($arg:ident: $ty:ty),*))*) => {
        $(
            gen_comment!{
                concat! {
                    "Logs a CobaltEvent of type `EventPayload::",
                    stringify!($variant),
                    "` by wrapping a call to `CobaltSenderWithComponent::",
                    stringify!($name), "`."
                },
                pub fn $name<Codes: AsEventCodes>(
                    &mut self,
                    metric_id: u32,
                    event_codes: Codes,
                    $($arg: $ty),*
                ) {
                    self.with_component().$name::<_, String, _>(
                        metric_id,
                        event_codes,
                        None,
                        $($arg),*
                    );
                }
            }
        )*
    }
}

impl CobaltSender {
    /// Constructs a new CobaltSender object.
    ///
    /// # Arguments
    ///
    /// * `sender` - The sending end of a `mpsc::channel`
    pub fn new(sender: mpsc::Sender<CobaltEvent>) -> CobaltSender {
        CobaltSender { sender, is_blocked: Arc::new(AtomicBool::new(false)) }
    }

    /// Accesses the sidecar struct `CobaltSenderWithComponent` to allow logging to cobalt with
    /// component strings.
    pub fn with_component(&mut self) -> CobaltSenderWithComponent<'_> {
        CobaltSenderWithComponent(self)
    }

    /// Logs a CobaltEvent of type `EventPayload::Event`.
    pub fn log_event<Codes: AsEventCodes>(&mut self, metric_id: u32, event_codes: Codes) {
        self.log_event_value(CobaltEvent {
            metric_id,
            event_codes: event_codes.as_event_codes(),
            component: None,
            payload: EventPayload::Event(fidl_fuchsia_cobalt::Event {}),
        });
    }

    /// Logs a plain CobaltEvent.
    pub fn log_cobalt_event(&mut self, event: CobaltEvent) {
        self.log_event_value(event);
    }

    fn log_event_value(&mut self, event: CobaltEvent) {
        if self.sender.try_send(event).is_err() {
            let was_blocked = self.is_blocked.compare_and_swap(false, true, Ordering::SeqCst);
            if !was_blocked {
                error!("cobalt sender drops a event/events: either buffer is full or no receiver is waiting");
            }
        } else {
            let was_blocked = self.is_blocked.compare_and_swap(true, false, Ordering::SeqCst);
            if was_blocked {
                info!("cobalt sender recovers and resumes sending")
            }
        }
    }

    impl_wrapped_methods! {
        #[CountEvent]
        log_event_count(period_duration_micros: i64, count: i64)

        #[ElapsedMicros]
        log_elapsed_time(elapsed_micros: i64)

        #[Fps]
        log_frame_rate(fps: f32)

        #[MemoryBytesUsed]
        log_memory_usage(bytes: i64)

        #[IntHistogram]
        log_int_histogram(values: Vec<HistogramBucket>)
    }
}

/// Allows logging to cobalt with component strings
///
/// Component strings are relatively uncommon, so this is a sidecar struct that contains
/// methods with component string arguments. To access it, you call
/// `CobaltSender::with_component()` followed by the method. Only event types that accept
/// component strings are present on this struct.
pub struct CobaltSenderWithComponent<'a>(&'a mut CobaltSender);

macro_rules! impl_log_methods{
    ($(#[$variant:ident] $name:ident($($arg:ident: $ty:ty),*) => $payload:expr)*) => {
        $(
            gen_comment!{
                concat! {
                    "Logs a CobaltEvent of type `EventPayload::",
                    stringify!($variant),
                    "` with a component string"
                },
                pub fn $name<Codes, S, Component>(
                    &mut self,
                    metric_id: u32,
                    event_codes: Codes,
                    component: Component,
                    $($arg: $ty),*
                ) where
                    Codes: AsEventCodes,
                    S: Into<String>,
                    Component: Into<Option<S>>
                {
                    self.0.log_event_value(CobaltEvent {
                        metric_id,
                        component: component.into().map(|s| s.into()),
                        event_codes: event_codes.as_event_codes(),
                        payload: $payload
                    })
                }
            }
        )*
    }
}

impl<'a> CobaltSenderWithComponent<'a> {
    impl_log_methods! {
        #[CountEvent]
        log_event_count(period_duration_micros: i64, count: i64) => {
            EventPayload::EventCount(CountEvent {
                period_duration_micros,
                count,
            })
        }

        #[ElapsedMicros]
        log_elapsed_time(elapsed_micros: i64) => {
            EventPayload::ElapsedMicros(elapsed_micros)
        }

        #[Fps]
        log_frame_rate(fps: f32) => {
            EventPayload::Fps(fps)
        }

        #[MemoryBytesUsed]
        log_memory_usage(bytes: i64) => {
            EventPayload::MemoryBytesUsed(bytes)
        }

        #[IntHistogram]
        log_int_histogram(values: Vec<HistogramBucket>) => {
            EventPayload::IntHistogram(values)
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_cobalt_sender() {
        let (sender, mut receiver) = mpsc::channel(1);
        let mut sender = CobaltSender::new(sender);
        sender.log_event(1, 1);
        assert_eq!(
            receiver.try_next().unwrap().unwrap(),
            CobaltEvent {
                metric_id: 1,
                event_codes: vec![1],
                component: None,
                payload: EventPayload::Event(fidl_fuchsia_cobalt::Event {}),
            }
        );

        sender.log_event_count(2, (), 10, 1);
        assert_eq!(
            receiver.try_next().unwrap().unwrap(),
            CobaltEvent {
                metric_id: 2,
                event_codes: vec![],
                component: None,
                payload: EventPayload::EventCount(CountEvent {
                    period_duration_micros: 10,
                    count: 1
                })
            }
        );

        sender.with_component().log_event_count(2, (), "test", 11, 2);
        assert_eq!(
            receiver.try_next().unwrap().unwrap(),
            CobaltEvent {
                metric_id: 2,
                event_codes: vec![],
                component: Some("test".to_owned()),
                payload: EventPayload::EventCount(CountEvent {
                    period_duration_micros: 11,
                    count: 2
                })
            }
        );

        sender.log_elapsed_time(3, [1, 2], 30);
        assert_eq!(
            receiver.try_next().unwrap().unwrap(),
            CobaltEvent {
                metric_id: 3,
                event_codes: vec![1, 2],
                component: None,
                payload: EventPayload::ElapsedMicros(30),
            }
        );

        sender.with_component().log_elapsed_time(3, [1, 2], "test".to_owned(), 30);
        assert_eq!(
            receiver.try_next().unwrap().unwrap(),
            CobaltEvent {
                metric_id: 3,
                event_codes: vec![1, 2],
                component: Some("test".to_owned()),
                payload: EventPayload::ElapsedMicros(30),
            }
        );

        sender.log_frame_rate(4, [1, 2, 3, 4], 10.0);
        assert_eq!(
            receiver.try_next().unwrap().unwrap(),
            CobaltEvent {
                metric_id: 4,
                event_codes: vec![1, 2, 3, 4],
                component: None,
                payload: EventPayload::Fps(10.0),
            }
        );

        sender.with_component().log_frame_rate(4, (), "testing", 100.0);
        assert_eq!(
            receiver.try_next().unwrap().unwrap(),
            CobaltEvent {
                metric_id: 4,
                event_codes: vec![],
                component: Some("testing".to_owned()),
                payload: EventPayload::Fps(100.0),
            }
        );

        sender.log_memory_usage(5, [1, 2, 3, 4, 5], 100);
        assert_eq!(
            receiver.try_next().unwrap().unwrap(),
            CobaltEvent {
                metric_id: 5,
                event_codes: vec![1, 2, 3, 4, 5],
                component: None,
                payload: EventPayload::MemoryBytesUsed(100),
            }
        );

        sender.with_component().log_memory_usage(5, [1, 2, 3, 4, 5], "a test", 100);
        assert_eq!(
            receiver.try_next().unwrap().unwrap(),
            CobaltEvent {
                metric_id: 5,
                event_codes: vec![1, 2, 3, 4, 5],
                component: Some("a test".to_owned()),
                payload: EventPayload::MemoryBytesUsed(100),
            }
        );

        sender.log_int_histogram(4, [1, 2, 3], vec![HistogramBucket { index: 2, count: 2 }]);
        assert_eq!(
            receiver.try_next().unwrap().unwrap(),
            CobaltEvent {
                metric_id: 4,
                event_codes: vec![1, 2, 3],
                component: None,
                payload: EventPayload::IntHistogram(vec![HistogramBucket { index: 2, count: 2 }]),
            }
        );

        sender.with_component().log_int_histogram(
            4,
            [1, 2, 3],
            "Component",
            vec![HistogramBucket { index: 2, count: 2 }],
        );
        assert_eq!(
            receiver.try_next().unwrap().unwrap(),
            CobaltEvent {
                metric_id: 4,
                event_codes: vec![1, 2, 3],
                component: Some("Component".to_owned()),
                payload: EventPayload::IntHistogram(vec![HistogramBucket { index: 2, count: 2 }]),
            }
        );
    }
}
