// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use diagnostics_reader::{tree_assertion, Inspect};
use diagnostics_testing::{EnvWithDiagnostics, Launched};
use fuchsia_async as fasync;

#[fasync::run_singlethreaded(test)]
async fn log_attribution() {
    let env = EnvWithDiagnostics::new().await;

    let package = "fuchsia-pkg://fuchsia.com/log-stats-tests#meta/";
    let stats_manifest = "log-stats.cmx";
    let stats_url = format!("{}{}", package, stats_manifest);

    let Launched { mut app, reader } = env.launch(&stats_url, None);
    let _app = fasync::Task::spawn(async move {
        app.wait().await.unwrap();
        panic!("log stats should not exit during test!");
    });

    let assertion = tree_assertion!(root: contains {
        // we expect one log from log-stats itself
        info_logs: 1u64,
        logsink_logs: 1u64,
        total_logs: 1u64,

        by_component: {
            "fuchsia-pkg://fuchsia.com/log-stats-tests#meta/log-stats.cmx": contains {
                info_logs: 1u64,
                total_logs: 1u64,
            }
        },
    });

    loop {
        let hierarchy =
            reader.snapshot::<Inspect>().await.into_iter().next().unwrap().payload.unwrap();

        if assertion.run(&hierarchy).is_ok() {
            break;
        }
    }
}
