// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Helper builder for constructing a `CobaltEvent`.

use {
    cobalt_client::traits::AsEventCodes,
    fidl_fuchsia_cobalt::{CobaltEvent, CountEvent, EventPayload, HistogramBucket},
};

///  Adds the `builder()` method to `CobaltEvent`.
pub trait CobaltEventExt {
    /// Returns a `CobaltEventBuilder` for the specified `metric_id`.
    ///
    /// # Examples
    ///
    /// ```
    /// assert_eq!(CobaltEvent::builder(5).as_event().metric_id, 0);
    /// ```
    fn builder(metric_id: u32) -> CobaltEventBuilder;
}

impl CobaltEventExt for CobaltEvent {
    fn builder(metric_id: u32) -> CobaltEventBuilder {
        CobaltEventBuilder { metric_id, ..CobaltEventBuilder::default() }
    }
}

/// CobaltEventBuilder allows for a chained construction of `CobaltEvent` objects.
#[derive(Debug, Default, Clone)]
pub struct CobaltEventBuilder {
    metric_id: u32,
    event_codes: Vec<u32>,
    component: Option<String>,
}

impl CobaltEventBuilder {
    /// Appends the provided `event_code` to the `event_codes` list.
    ///
    /// # Examples
    ///
    /// ```
    /// assert_eq!(CobaltEvent::builder(6).with_event_code(10).as_event().event_codes, vec![10]);
    /// ```
    pub fn with_event_code(mut self, event_code: u32) -> CobaltEventBuilder {
        self.event_codes.push(event_code);
        self
    }

    /// Overrides the list of event_codes with the provided `event_codes`.
    ///
    /// # Examples
    ///
    /// ```
    /// assert_eq!(
    ///     CobaltEvent::builder(7).with_event_codes([1, 2, 3]).as_event().event_codes,
    ///     vec![1,2,3]);
    /// ```
    pub fn with_event_codes<Codes: AsEventCodes>(
        mut self,
        event_codes: Codes,
    ) -> CobaltEventBuilder {
        self.event_codes = event_codes.as_event_codes();
        self
    }

    /// Writes an `event_code` to a particular `index`. This method is useful when not assigning
    /// event codes in order.
    ///
    /// # Examples
    ///
    /// ```
    /// assert_eq!(
    ///     CobaltEvent::builder(8).with_event_code_at(1, 10).as_event().event_codes,
    ///     vec![0, 10]);
    /// ```
    ///
    /// # Panics
    ///
    /// Panics if the `value`  is greater than or equal to 5.
    pub fn with_event_code_at(mut self, index: usize, event_code: u32) -> CobaltEventBuilder {
        assert!(
            index < 5,
            "Invalid index passed to CobaltEventBuilder::with_event_code. Cobalt events cannot support more than 5 event_codes."
        );
        while self.event_codes.len() <= index {
            self.event_codes.push(0);
        }
        self.event_codes[index] = event_code;
        self
    }

    /// Adds the provided `component` string to the resulting `CobaltEvent`.
    ///
    /// # Examples
    ///
    /// ```
    /// assert_eq!(
    ///     CobaltEvent::builder(9).with_component("Comp").as_event.component,
    ///     Some("Comp".to_owned()));
    /// ```
    pub fn with_component<S: Into<String>>(mut self, component: S) -> CobaltEventBuilder {
        self.component = Some(component.into());
        self
    }

    /// Constructs a `CobaltEvent` with the provided `EventPayload`.
    ///
    /// # Examples
    /// ```
    /// let payload = EventPayload::Event(fidl_fuchsia_cobalt::Event);
    /// assert_eq!(CobaltEvent::builder(10).build(payload.clone()).payload, payload);
    /// ```
    pub fn build(self, payload: EventPayload) -> CobaltEvent {
        CobaltEvent {
            metric_id: self.metric_id,
            event_codes: self.event_codes,
            component: self.component,
            payload,
        }
    }

    /// Constructs a `CobaltEvent` with a payload type of `EventPayload::Event`.
    ///
    /// # Examples
    /// ```
    /// asert_eq!(
    ///     CobaltEvent::builder(11).as_event().payload,
    ///     EventPayload::Event(fidl_fuchsia_cobalt::Event));
    /// ```
    pub fn as_event(self) -> CobaltEvent {
        self.build(EventPayload::Event(fidl_fuchsia_cobalt::Event))
    }

    /// Constructs a `CobaltEvent` with a payload type of `EventPayload::EventCount`.
    ///
    /// # Examples
    /// ```
    /// asert_eq!(
    ///     CobaltEvent::builder(12).as_count_event(5, 10).payload,
    ///     EventPayload::EventCount(CountEvent { period_duration_micros: 5, count: 10 }));
    /// ```
    pub fn as_count_event(self, period_duration_micros: i64, count: i64) -> CobaltEvent {
        self.build(EventPayload::EventCount(CountEvent { period_duration_micros, count }))
    }

    /// Constructs a `CobaltEvent` with a payload type of `EventPayload::ElapsedMicros`.
    ///
    /// # Examples
    /// ```
    /// asert_eq!(
    ///     CobaltEvent::builder(13).as_elapsed_time(30).payload,
    ///     EventPayload::ElapsedMicros(30));
    /// ```
    pub fn as_elapsed_time(self, elapsed_micros: i64) -> CobaltEvent {
        self.build(EventPayload::ElapsedMicros(elapsed_micros))
    }

    /// Constructs a `CobaltEvent` with a payload type of `EventPayload::Fps`.
    ///
    /// # Examples
    /// ```
    /// asert_eq!(
    ///     CobaltEvent::builder(14).as_frame_rate(99.).payload,
    ///     EventPayload::Fps(99.));
    /// ```
    pub fn as_frame_rate(self, fps: f32) -> CobaltEvent {
        self.build(EventPayload::Fps(fps))
    }

    /// Constructs a `CobaltEvent` with a payload type of `EventPayload::MemoryBytesUsed`.
    ///
    /// # Examples
    /// ```
    /// asert_eq!(
    ///     CobaltEvent::builder(15).as_memory_usage(1000).payload,
    ///     EventPayload::MemoryBytesUsed(1000));
    /// ```
    pub fn as_memory_usage(self, memory_bytes_used: i64) -> CobaltEvent {
        self.build(EventPayload::MemoryBytesUsed(memory_bytes_used))
    }

    /// Constructs a `CobaltEvent` with a payload type of `EventPayload::IntHistogram`.
    ///
    /// # Examples
    /// ```
    /// let histogram = vec![HistogramBucket { index: 0, count: 1 }];
    /// asert_eq!(
    ///     CobaltEvent::builder(17).as_int_histogram(histogram.clone()).payload,
    ///     EventPayload::IntHistogram(histogram));
    /// ```
    pub fn as_int_histogram(self, int_histogram: Vec<HistogramBucket>) -> CobaltEvent {
        self.build(EventPayload::IntHistogram(int_histogram))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_builder_as_event() {
        let event = CobaltEvent::builder(1).with_event_code(2).as_event();
        let expected = CobaltEvent {
            metric_id: 1,
            event_codes: vec![2],
            component: None,
            payload: EventPayload::Event(fidl_fuchsia_cobalt::Event),
        };
        assert_eq!(event, expected);
    }

    #[test]
    fn test_builder_as_count_event() {
        let event =
            CobaltEvent::builder(2).with_event_code(3).with_component("A").as_count_event(4, 5);
        let expected = CobaltEvent {
            metric_id: 2,
            event_codes: vec![3],
            component: Some("A".into()),
            payload: EventPayload::EventCount(CountEvent { count: 5, period_duration_micros: 4 }),
        };
        assert_eq!(event, expected);
    }

    #[test]
    fn test_as_elapsed_time() {
        let event =
            CobaltEvent::builder(3).with_event_code(4).with_component("B").as_elapsed_time(5);
        let expected = CobaltEvent {
            metric_id: 3,
            event_codes: vec![4],
            component: Some("B".into()),
            payload: EventPayload::ElapsedMicros(5),
        };
        assert_eq!(event, expected);
    }

    #[test]
    fn test_as_frame_rate() {
        let event =
            CobaltEvent::builder(4).with_event_code(5).with_component("C").as_frame_rate(6.);
        let expected = CobaltEvent {
            metric_id: 4,
            event_codes: vec![5],
            component: Some("C".into()),
            payload: EventPayload::Fps(6.),
        };
        assert_eq!(event, expected);
    }

    #[test]
    fn test_as_memory_usage() {
        let event =
            CobaltEvent::builder(5).with_event_code(6).with_component("D").as_memory_usage(7);
        let expected = CobaltEvent {
            metric_id: 5,
            event_codes: vec![6],
            component: Some("D".into()),
            payload: EventPayload::MemoryBytesUsed(7),
        };
        assert_eq!(event, expected);
    }

    #[test]
    fn test_as_int_histogram() {
        let event = CobaltEvent::builder(7)
            .with_event_code(8)
            .with_component("E")
            .as_int_histogram(vec![HistogramBucket { index: 0, count: 1 }]);
        let expected = CobaltEvent {
            metric_id: 7,
            event_codes: vec![8],
            component: Some("E".into()),
            payload: EventPayload::IntHistogram(vec![HistogramBucket { index: 0, count: 1 }]),
        };
        assert_eq!(event, expected);
    }

    #[test]
    #[should_panic(expected = "Invalid index")]
    fn test_bad_event_code_at_index() {
        CobaltEvent::builder(8).with_event_code_at(5, 10).as_event();
    }
}
