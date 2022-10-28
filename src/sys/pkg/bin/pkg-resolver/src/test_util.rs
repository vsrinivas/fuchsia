// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    cobalt_client::traits::AsEventCodes,
    fidl_contrib::protocol_connector::ProtocolSender,
    fidl_fuchsia_metrics::{MetricEvent, MetricEventPayload},
    futures::channel::mpsc,
    serde::Serialize,
    std::{
        fs::File,
        io::{self, Write as _},
        str,
    },
    tempfile::{self, TempDir},
};

pub(crate) fn create_dir<'a, T, S>(iter: T) -> TempDir
where
    T: IntoIterator<Item = (&'a str, S)>,
    S: Serialize,
{
    let dir = tempfile::tempdir().unwrap();

    for (name, config) in iter {
        let path = dir.path().join(name);
        let mut f = io::BufWriter::new(File::create(path).unwrap());
        serde_json::to_writer(&mut f, &config).unwrap();
        f.flush().unwrap();
    }

    dir
}

pub(crate) fn get_mock_cobalt_sender() -> (ProtocolSender<MetricEvent>, mpsc::Receiver<MetricEvent>)
{
    let (sender, cobalt_receiver) = mpsc::channel(1);
    (ProtocolSender::new(sender), cobalt_receiver)
}

pub(crate) fn verify_cobalt_emits_event(
    cobalt_receiver: &mut mpsc::Receiver<MetricEvent>,
    metric_id: u32,
    expected_event_codes: impl AsEventCodes,
) {
    assert_eq!(
        cobalt_receiver.try_next().unwrap().unwrap(),
        MetricEvent {
            metric_id,
            event_codes: expected_event_codes.as_event_codes(),
            payload: MetricEventPayload::Count(1),
        }
    );
}
