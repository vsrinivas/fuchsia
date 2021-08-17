// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use diagnostics_reader::{tree_assertion, ArchiveReader, Inspect};
use fidl_fuchsia_component as fcomponent;
use fuchsia_async as fasync;
use fuchsia_component::client;
use fuchsia_zircon::DurationNum;

const RETRY_DELAY_MS: i64 = 300;

#[fuchsia::test]
async fn log_attribution() {
    // Connecting to `fuchsia.component.Binder` will start the `log-stats` component.
    let _ = client::connect_to_childs_protocol::<fcomponent::BinderMarker>(
        "log-stats".to_owned(),
        None,
    )
    .await
    .expect("child is running");

    // We expect two logs from log-stats itself:
    // - INFO: Maintaining.
    // - INFO: Failed to open component map file ...
    let assertion = tree_assertion!(root: contains {
        info_logs: 2u64,
        logsink_logs: 2u64,
        total_logs: 2u64,

        by_component: {
            "fuchsia-pkg://fuchsia.com/log-stats-tests#meta/log-stats.cm": contains {
                info_logs: 2u64,
                total_logs: 2u64,
            }
        },
    });

    let mut reader = ArchiveReader::new();
    reader.add_selector("log-stats:root");
    loop {
        let hierarchy = reader
            .snapshot::<Inspect>()
            .await
            .expect("got results")
            .into_iter()
            .next()
            .unwrap()
            .payload
            .unwrap();

        if assertion.run(&hierarchy).is_ok() {
            break;
        }

        fasync::Timer::new(fasync::Time::after(RETRY_DELAY_MS.millis())).await;
    }
}
