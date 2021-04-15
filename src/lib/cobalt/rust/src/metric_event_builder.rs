// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Helper builder for constructing a `MetricEvent`.

use {
    cobalt_client::traits::AsEventCodes,
    fidl_fuchsia_metrics::{HistogramBucket, MetricEvent, MetricEventPayload},
};

///  Adds the `builder()` method to `MetricEvent`.
pub trait MetricEventExt {
    /// Returns a `MetricEventBuilder` for the specified `metric_id`.
    ///
    /// # Examples
    ///
    /// ```
    /// assert_eq!(MetricEvent::builder(5).as_event().metric_id, 0);
    /// ```
    fn builder(metric_id: u32) -> MetricEventBuilder;
}

impl MetricEventExt for MetricEvent {
    fn builder(metric_id: u32) -> MetricEventBuilder {
        MetricEventBuilder { metric_id, ..MetricEventBuilder::default() }
    }
}

/// MetricEventBuilder allows for a chained construction of `MetricEvent` objects.
#[derive(Debug, Default, Clone)]
pub struct MetricEventBuilder {
    metric_id: u32,
    event_codes: Vec<u32>,
}

impl MetricEventBuilder {
    /// Appends the provided `event_code` to the `event_codes` list.
    ///
    /// # Examples
    ///
    /// ```
    /// assert_eq!(MetricEvent::builder(6).with_event_code(10).as_event().event_codes, vec![10]);
    /// ```
    pub fn with_event_code(mut self, event_code: u32) -> MetricEventBuilder {
        self.event_codes.push(event_code);
        self
    }

    /// Overrides the list of event_codes with the provided `event_codes`.
    ///
    /// # Examples
    ///
    /// ```
    /// assert_eq!(
    ///     MetricEvent::builder(7).with_event_codes([1, 2, 3]).as_event().event_codes,
    ///     vec![1,2,3]);
    /// ```
    pub fn with_event_codes<Codes: AsEventCodes>(
        mut self,
        event_codes: Codes,
    ) -> MetricEventBuilder {
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
    ///     MetricEvent::builder(8).with_event_code_at(1, 10).as_event().event_codes,
    ///     vec![0, 10]);
    /// ```
    ///
    /// # Panics
    ///
    /// Panics if the `value`  is greater than or equal to 5.
    pub fn with_event_code_at(mut self, index: usize, event_code: u32) -> MetricEventBuilder {
        assert!(
            index < 5,
            "Invalid index passed to MetricEventBuilder::with_event_code. Cobalt events cannot support more than 5 event_codes."
        );
        while self.event_codes.len() <= index {
            self.event_codes.push(0);
        }
        self.event_codes[index] = event_code;
        self
    }

    /// Constructs a `MetricEvent` with the provided `MetricEventPayload`.
    ///
    /// # Examples
    /// ```
    /// let payload = MetricEventPayload::Event(fidl_fuchsia_cobalt::Event);
    /// assert_eq!(MetricEvent::builder(10).build(payload.clone()).payload, payload);
    /// ```
    pub fn build(self, payload: MetricEventPayload) -> MetricEvent {
        MetricEvent { metric_id: self.metric_id, event_codes: self.event_codes, payload }
    }

    /// Constructs a `MetricEvent` with a payload type of `MetricEventPayload::Count`.
    ///
    /// # Examples
    /// ```
    /// asert_eq!(
    ///     MetricEvent::builder(11).as_occurrence(10).payload,
    ///     MetricEventPayload::Event(fidl_fuchsia_cobalt::Count(10)));
    /// ```
    pub fn as_occurrence(self, count: u64) -> MetricEvent {
        self.build(MetricEventPayload::Count(count))
    }

    /// Constructs a `MetricEvent` with a payload type of `MetricEventPayload::IntegerValue`.
    ///
    /// # Examples
    /// ```
    /// asert_eq!(
    ///     MetricEvent::builder(12).as_integer(5).payload,
    ///     MetricEventPayload::IntegerValue(5)));
    /// ```
    pub fn as_integer(self, integer_value: i64) -> MetricEvent {
        self.build(MetricEventPayload::IntegerValue(integer_value))
    }

    /// Constructs a `MetricEvent` with a payload type of `MetricEventPayload::Histogram`.
    ///
    /// # Examples
    /// ```
    /// let histogram = vec![HistogramBucket { index: 0, count: 1 }];
    /// asert_eq!(
    ///     MetricEvent::builder(17).as_int_histogram(histogram.clone()).payload,
    ///     MetricEventPayload::Histogram(histogram));
    /// ```
    pub fn as_integer_histogram(self, histogram: Vec<HistogramBucket>) -> MetricEvent {
        self.build(MetricEventPayload::Histogram(histogram))
    }

    /// Constructs a `MetricEvent` with a payload type of `MetricEventPayload::StringValue`.
    ///
    /// # Examples
    /// ```
    /// asert_eq!(
    ///     MetricEvent::builder(17).as_string("test").payload,
    ///     MetricEventPayload::StringValue("test".to_string()));
    /// ```
    pub fn as_string<S: Into<String>>(self, string: S) -> MetricEvent {
        self.build(MetricEventPayload::StringValue(string.into()))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_builder_as_occurrence() {
        let event = MetricEvent::builder(1).with_event_code(2).as_occurrence(3);
        let expected = MetricEvent {
            metric_id: 1,
            event_codes: vec![2],
            payload: MetricEventPayload::Count(3),
        };
        assert_eq!(event, expected);
    }

    #[test]
    fn test_builder_as_integer() {
        let event = MetricEvent::builder(2).with_event_code(3).as_integer(4);
        let expected = MetricEvent {
            metric_id: 2,
            event_codes: vec![3],
            payload: MetricEventPayload::IntegerValue(4),
        };
        assert_eq!(event, expected);
    }

    #[test]
    fn test_as_integer_histogram() {
        let event = MetricEvent::builder(7)
            .with_event_code(8)
            .as_integer_histogram(vec![HistogramBucket { index: 0, count: 1 }]);
        let expected = MetricEvent {
            metric_id: 7,
            event_codes: vec![8],
            payload: MetricEventPayload::Histogram(vec![HistogramBucket { index: 0, count: 1 }]),
        };
        assert_eq!(event, expected);
    }

    #[test]
    fn test_builder_as_string() {
        let event = MetricEvent::builder(2).with_event_code(3).as_string("value");
        let expected = MetricEvent {
            metric_id: 2,
            event_codes: vec![3],
            payload: MetricEventPayload::StringValue("value".into()),
        };
        assert_eq!(event, expected);
    }

    #[test]
    #[should_panic(expected = "Invalid index")]
    fn test_bad_event_code_at_index() {
        MetricEvent::builder(8).with_event_code_at(5, 10).as_occurrence(1);
    }
}
