// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{borrow::Cow, mem::replace};

use crate::{linealyzer::Linealyzer, Event};

/// `EventSource` `parse`s byte slices from an http sse connection into sse `Events`.
/// It maintains state across `parse` calls, so that an `Event` whose bytes
/// are split across two calls to `parse` will be returned by the second call.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct EventSource {
    linealyzer: Linealyzer,
    processed_first_line: bool,
    event_type: String,
    data: String,
    // http sse spec says to append a newline to the data buffer on each receipt
    // of a data field, and then to remove the last newline from the data buffer
    // before reconstituting the event. Appending the newline forces an extra
    // allocation/str copy on the simple path of events with single-line data,
    // so we keep track of how many newlines to append instead.
    data_trailing_newlines: usize,
}

impl EventSource {
    pub fn new() -> Self {
        Self {
            linealyzer: Linealyzer::new(),
            processed_first_line: false,
            event_type: String::new(),
            data: String::new(),
            data_trailing_newlines: 0,
        }
    }

    /// Ingest more bytes from an http sse stream and return all completed `Event`s.
    pub fn parse(&mut self, bytes: &[u8]) -> Vec<Event> {
        let mut ret = vec![];
        for mut line in self.linealyzer.feed(bytes) {
            // http sse stream is allowed to begin with an optional utf8-encoded
            // byte order mark (bom), that should be ignored.
            if !self.processed_first_line {
                self.processed_first_line = true;
                if line.starts_with(b"\xef\xbb\xbf") {
                    line = match line {
                        Cow::Borrowed(b) => Cow::Borrowed(&b[3..]),
                        Cow::Owned(mut b) => {
                            b.drain(..3);
                            Cow::Owned(b)
                        }
                    };
                }
            }

            if line.is_empty() {
                if self.data.is_empty() && self.data_trailing_newlines == 0 {
                    self.event_type.clear();
                } else {
                    let n = self.data_trailing_newlines - 1;
                    self.data.reserve(n);
                    for _ in 0..n {
                        self.data.push('\n')
                    }
                    self.data_trailing_newlines = 0;
                    ret.push(
                        // self.event_type cannot have carriage returns or newlines
                        // self.data cannot be empty or have carriage returns in it
                        // so event creation should never fail
                        Event::from_type_and_data(
                            replace(&mut self.event_type, String::new()),
                            replace(&mut self.data, String::new()),
                        )
                        .unwrap(),
                    );
                }
            } else if line[0] == b':' { // ignore these lines
            } else {
                let (name, value) = match line.iter().position(|b| *b == b':') {
                    Some(p) => {
                        let (name, mut value) = line.split_at(p);
                        value = &value[1..];
                        if !value.is_empty() && value[0] == b' ' {
                            value = &value[1..];
                        }
                        (
                            String::from_utf8_lossy(name).into_owned(),
                            String::from_utf8_lossy(value).into_owned(),
                        )
                    }
                    None => (String::from_utf8_lossy(&line).into_owned(), String::new()),
                };

                if name == "event" {
                    self.event_type = value;
                } else if name == "data" {
                    if !value.is_empty() {
                        if self.data_trailing_newlines > 0 {
                            self.data.reserve(self.data_trailing_newlines + value.len());
                            for _ in 0..self.data_trailing_newlines {
                                self.data.push('\n');
                            }
                            self.data_trailing_newlines = 0;
                        }
                        if self.data.is_empty() {
                            self.data = value;
                        } else {
                            self.data.push_str(&value);
                        }
                    }
                    self.data_trailing_newlines += 1;
                } // ignoring unrecognized field names, including "id" and "retry"
            }
        }
        ret
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use proptest::prelude::*;

    #[test]
    fn event_and_data() {
        let bs = b"event: type\n\
                   data: data\n\n";
        let mut event_source = EventSource::new();

        let events = event_source.parse(bs);

        assert_eq!(events, vec![Event::from_type_and_data("type", "data").unwrap()]);
    }

    #[test]
    fn data_before_event() {
        let bs = b"data: data\n\
                   event: type\n\n";
        let mut event_source = EventSource::new();

        let events = event_source.parse(bs);

        assert_eq!(events, vec![Event::from_type_and_data("type", "data").unwrap()]);
    }

    #[test]
    fn bom_stripped() {
        let bs = b"\xef\xbb\xbfevent: type\n\
                  data: data\n\n";
        let mut event_source = EventSource::new();

        let events = event_source.parse(bs);

        assert_eq!(events, vec![Event::from_type_and_data("type", "data").unwrap()]);
    }

    #[test]
    fn partial_bom_not_stripped() {
        let bs = b"\xef\xbbevent: type\n\
                   data: data\n\n";
        let mut event_source = EventSource::new();

        let events = event_source.parse(bs);

        assert_eq!(events, vec![Event::from_type_and_data("", "data").unwrap()]);
    }

    #[test]
    fn bom_stripped_only_on_first_line() {
        let bs = b"\xef\xbb\xbfevent: type\n\
                   \xef\xbb\xbfdata: data\n\n";
        let mut event_source = EventSource::new();

        let events = event_source.parse(bs);

        assert_eq!(events, vec![]);
    }

    #[test]
    fn invalid_utf8_replaced() {
        let bs = b"event: type\xff\n\
                   data: data\xff\n\n";
        let mut event_source = EventSource::new();

        let events = event_source.parse(bs);

        assert_eq!(events, vec![Event::from_type_and_data("type�", "data�").unwrap()]);
    }

    #[test]
    fn colon_allowed_in_field_value() {
        let bs = b"event: type\n\
                   data: :da:ta\n\n";
        let mut event_source = EventSource::new();

        let events = event_source.parse(bs);

        assert_eq!(events, vec![Event::from_type_and_data("type", ":da:ta").unwrap()]);
    }

    #[test]
    fn event_with_no_data_is_dropped() {
        let bs = b"event: type\n\n";
        let mut event_source = EventSource::new();

        let events = event_source.parse(bs);
        assert_eq!(events, vec![]);

        let events = event_source.parse(b"data: data\n\n");
        assert_eq!(events, vec![Event::from_type_and_data("", "data").unwrap()]);
    }

    #[test]
    fn consecutive_event_replaces() {
        let bs = b"event: type\n\
                   data: data\n\
                   event: replaced_type\n\n";
        let mut event_source = EventSource::new();

        let events = event_source.parse(bs);
        assert_eq!(events, vec![Event::from_type_and_data("replaced_type", "data").unwrap()]);
    }

    #[test]
    fn consecutive_data_concatenates_with_newline() {
        let bs = b"data: data1\n\
                   data: data2\n\n";
        let mut event_source = EventSource::new();

        let events = event_source.parse(bs);
        assert_eq!(events, vec![Event::from_type_and_data("", "data1\ndata2").unwrap()]);
    }

    #[test]
    fn consecutive_empty_lines_does_nothing() {
        let bs = b"data: data\n\n\n\n\n\n\n\n";
        let mut event_source = EventSource::new();

        let events = event_source.parse(bs);
        assert_eq!(events, vec![Event::from_type_and_data("", "data").unwrap()]);
    }

    #[test]
    fn missing_field_value_is_empty_string() {
        let bs = b"event: type\n\
                   data: data\n\
                   event\n\
                   data\n\n";
        let mut event_source = EventSource::new();

        let events = event_source.parse(bs);
        assert_eq!(events, vec![Event::from_type_and_data("", "data\n").unwrap()]);
    }

    #[test]
    fn field_value_without_leading_space_not_stripped() {
        let bs = b"event: type\n\
                   data:data\n\n";
        let mut event_source = EventSource::new();

        let events = event_source.parse(bs);
        assert_eq!(events, vec![Event::from_type_and_data("type", "data").unwrap()]);
    }

    #[test]
    fn only_first_field_value_space_stripped() {
        let bs = b"event: type\n\
                   data:  data\n\n";
        let mut event_source = EventSource::new();

        let events = event_source.parse(bs);
        assert_eq!(events, vec![Event::from_type_and_data("type", " data").unwrap()]);
    }

    #[test]
    fn comment_lines_ignored() {
        let bs = b"event: type\n\
                   :event: other_type\n\
                   data: data\n\n";
        let mut event_source = EventSource::new();

        let events = event_source.parse(bs);
        assert_eq!(events, vec![Event::from_type_and_data("type", "data").unwrap()]);
    }

    #[test]
    fn unknown_field_names_ignored() {
        let bs = b"event: type\n\
                   event2: other_type\n\
                   data: data\n\n";
        let mut event_source = EventSource::new();

        let events = event_source.parse(bs);
        assert_eq!(events, vec![Event::from_type_and_data("type", "data").unwrap()]);
    }

    #[test]
    fn event_and_data_persisted_across_parse() {
        let mut event_source = EventSource::new();

        assert_eq!(event_source.parse(b"event: type\n"), vec![]);
        assert_eq!(event_source.parse(b"data: data\n"), vec![]);
        assert_eq!(
            event_source.parse(b"\n"),
            vec![Event::from_type_and_data("type", "data").unwrap()]
        );
    }

    #[test]
    fn event_and_data_not_persisted_across_parse_after_dispatch() {
        let bs = b"event: type\n\
                   data: data\n\n";
        let mut event_source = EventSource::new();

        let events = event_source.parse(bs);
        assert_eq!(events, vec![Event::from_type_and_data("type", "data").unwrap()]);

        assert_eq!(
            event_source.parse(b"data: data2\n\n"),
            vec![Event::from_type_and_data("", "data2").unwrap()]
        );
    }

    fn assert_all_3_byte_partitionings(bytes: &[u8], events: Vec<Event>) {
        for i in 0..bytes.len() {
            for j in i..bytes.len() {
                let mut event_source = EventSource::new();
                let mut parsed_events = vec![];
                parsed_events.append(&mut event_source.parse(&bytes[..i]));
                parsed_events.append(&mut event_source.parse(&bytes[i..j]));
                parsed_events.append(&mut event_source.parse(&bytes[j..]));
                assert_eq!(parsed_events, events, "i: {}, j: {}, bytes: {:?}", i, j, bytes);
            }
        }
    }

    #[test]
    fn parse_event_all_3_byte_partitionings() {
        let bs = b"event: type\n\
                   data: data\n\n";
        assert_all_3_byte_partitionings(
            bs,
            vec![Event::from_type_and_data("type", "data").unwrap()],
        );
    }
    #[test]
    fn parse_two_events_all_3_byte_partitionings() {
        let bs = b"event: type\n\
                   data: data\n\
                   \n\
                   data: data2\n\
                   event: type2\n\n";
        assert_all_3_byte_partitionings(
            bs,
            vec![
                Event::from_type_and_data("type", "data").unwrap(),
                Event::from_type_and_data("type2", "data2").unwrap(),
            ],
        );
    }

    prop_compose! {
        fn random_event()
            (event_type in "[^\r\n]{0,20}",
             data in "[^\r]{1,20}") -> Event
        {
            Event::from_type_and_data(event_type, data).unwrap()
        }
    }

    prop_compose! {
        fn random_adversarial_event()
            (event_type in "[a:]{0,3}",
             data in "[a\n]{1,5}") -> Event
        {
            Event::from_type_and_data(event_type, data).unwrap()
        }
    }

    proptest! {
        #[test]
        fn random_event_serialize_deserialize(
            event in random_event())
        {
            let mut bytes = vec![];
            event.to_writer(&mut bytes).unwrap();
            assert_eq!(EventSource::new().parse(&bytes), vec![event.clone()]);
        }

        #[test]
        fn random_events_serialize_deserialize(
            events in prop::collection::vec(random_event(), 0..10))
        {
            let mut bytes = vec![];
            for event in events.iter() {
                event.to_writer(&mut bytes).unwrap();
            }
            assert_eq!(EventSource::new().parse(&bytes), events);
        }

        #[test]
        fn random_adversarial_events_serialize_deserialize(
            events in prop::collection::vec(random_adversarial_event(), 0..10))
        {
            let mut bytes = vec![];
            for event in events.iter() {
                event.to_writer(&mut bytes).unwrap();
            }
            assert_eq!(EventSource::new().parse(&bytes), events);
        }

        #[test]
        fn random_events_serialize_deserialize_all_3_line_partitionings(
            events in prop::collection::vec(random_event(), 0..4))
        {
            assert_all_3_line_partitionings(events);
        }

        #[test]
        fn random_adversarial_events_serialize_deserialize_all_3_line_partitionings(
            events in prop::collection::vec(random_adversarial_event(), 0..4))
        {
            assert_all_3_line_partitionings(events);
        }
    }

    fn assert_all_3_line_partitionings(events: Vec<Event>) {
        let mut bytes = vec![];
        for event in events.iter() {
            event.to_writer(&mut bytes).unwrap();
        }

        let splits: Vec<&[u8]> = bytes.split(|b| *b == b'\n').collect();
        for i in 0..=splits.len() {
            for j in i..=splits.len() {
                let mut event_source = EventSource::new();
                let mut parsed_events = vec![];
                parsed_events.append(&mut event_source.parse(&splits[0..i].join(&b'\n')));
                if i > 0 {
                    parsed_events.append(&mut event_source.parse(b"\n"));
                }
                parsed_events.append(&mut event_source.parse(&splits[i..j].join(&b'\n')));
                if j > i {
                    parsed_events.append(&mut event_source.parse(b"\n"));
                }
                parsed_events.append(&mut event_source.parse(&splits[j..].join(&b'\n')));
                if j < splits.len() {
                    parsed_events.append(&mut event_source.parse(b"\n"));
                }
                assert_eq!(parsed_events, events, "i: {}, j: {}", i, j);
            }
        }
    }
}
