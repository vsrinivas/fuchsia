// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{constants::*, test_topology};
use archivist_lib::logs::redact::{REDACTED_CANARY_MESSAGE, UNREDACTED_CANARY_MESSAGE};
use diagnostics_message::{fx_log_metadata_t, fx_log_packet_t};
use diagnostics_reader::{ArchiveReader, Data, Logs};
use fidl_fuchsia_diagnostics::ArchiveAccessorMarker;
use fidl_fuchsia_logger::{LogLevelFilter, LogSinkMarker, LogSinkProxy};
use fuchsia_component_test::RealmInstance;
use fuchsia_zircon as zx;
use tracing::debug;

#[fuchsia::test]
async fn canary_is_redacted_with_filtering() {
    let test = RedactionTest::new(ARCHIVIST_WITH_FEEDBACK_FILTERING).await;
    let redacted = test.get_feedback_canary().await;
    assert_eq!(redacted.msg().unwrap().trim_end(), REDACTED_CANARY_MESSAGE);
}

#[fuchsia::test]
async fn canary_is_unredacted_without_filtering() {
    let test = RedactionTest::new(ARCHIVIST_WITH_FEEDBACK_FILTERING_DISABLED).await;
    let redacted = test.get_feedback_canary().await;
    assert_eq!(redacted.msg().unwrap().trim_end(), UNREDACTED_CANARY_MESSAGE);
}

struct RedactionTest {
    _instance: RealmInstance,
    _log_sink: LogSinkProxy,
    all_reader: ArchiveReader,
    feedback_reader: ArchiveReader,
}

impl RedactionTest {
    async fn new(archivist_url: &'static str) -> Self {
        let builder = test_topology::create(test_topology::Options { archivist_url })
            .await
            .expect("create base topology");

        let instance = builder.build().create().await.expect("create instance");
        let mut packet = fx_log_packet_t {
            metadata: fx_log_metadata_t {
                time: 3000,
                pid: 1000,
                tid: 2000,
                severity: LogLevelFilter::Info.into_primitive().into(),
                ..fx_log_metadata_t::default()
            },
            ..fx_log_packet_t::default()
        };
        packet.add_data(1, UNREDACTED_CANARY_MESSAGE.as_bytes());
        let (snd, rcv) = zx::Socket::create(zx::SocketOpts::DATAGRAM).unwrap();
        snd.write(packet.as_bytes()).unwrap();
        let log_sink = instance.root.connect_to_protocol_at_exposed_dir::<LogSinkMarker>().unwrap();
        log_sink.connect(rcv).unwrap();

        let mut all_reader = ArchiveReader::new();
        all_reader.with_archive(
            instance.root.connect_to_protocol_at_exposed_dir::<ArchiveAccessorMarker>().unwrap(),
        );
        let mut feedback_reader = ArchiveReader::new();
        feedback_reader.with_archive(
            instance
                .root
                .connect_to_named_protocol_at_exposed_dir::<ArchiveAccessorMarker>(
                    "fuchsia.diagnostics.FeedbackArchiveAccessor",
                )
                .unwrap(),
        );

        Self { _instance: instance, _log_sink: log_sink, all_reader, feedback_reader }
    }

    async fn get_feedback_canary(&self) -> Data<Logs> {
        debug!("retrieving logs from feedback accessor");
        let feedback_logs = self.feedback_reader.snapshot::<Logs>().await.unwrap();
        let all_logs = self.all_reader.snapshot::<Logs>().await.unwrap();

        let (unredacted, redacted) = all_logs
            .into_iter()
            .zip(feedback_logs)
            .find(|(u, _)| u.msg().unwrap().contains(UNREDACTED_CANARY_MESSAGE))
            .unwrap();
        debug!(unredacted = %unredacted.msg().unwrap());
        redacted
    }
}
