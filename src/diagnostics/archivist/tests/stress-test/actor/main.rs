// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This actor has actions to write/read logs to/from Archivist.

use {
    anyhow::{Context, Result},
    diagnostics_reader::{ArchiveReader, Data, Logs, Subscription},
    fidl_fuchsia_diagnostics::ArchiveAccessorMarker,
    fuchsia_component::client::connect_to_protocol,
    futures::{future::BoxFuture, FutureExt, StreamExt},
    log::info,
    rand::{rngs::SmallRng, Rng},
    stress_test_actor::{actor_loop, Action},
};

/// Stores all data needed by this Worker. This singleton gets passed into every action.
struct WorkerData {
    /// The active log subscription created with Archivist.
    /// Used by the reader action to get logs.
    pub subscription: Subscription<Logs>,
}

#[fuchsia::component]
async fn main() -> Result<()> {
    // Create a WorkerData singleton
    let accessor_proxy = connect_to_protocol::<ArchiveAccessorMarker>()
        .context("Could not connect to ArchiveAccessor protocol")?;
    let mut archive_reader = ArchiveReader::new();
    archive_reader.with_archive(accessor_proxy);
    let subscription =
        archive_reader.snapshot_then_subscribe::<Logs>().context("Could not subscribe to logs")?;
    let data = WorkerData { subscription };

    actor_loop(
        data,
        vec![
            Action { name: "reader", run: reader },
            Action { name: "simple_writer", run: simple_writer },
            Action { name: "length_writer", run: length_writer },
        ],
    )
    .await
}

fn reader<'a>(data: &'a mut WorkerData, _: SmallRng) -> BoxFuture<'a, Result<()>> {
    async move {
        let log: Data<Logs> =
            data.subscription.next().await.context("No next log")?.context("Error getting log")?;
        log.msg().context("Log doesn't have message")?;

        // TODO(82134): Add log verification

        Ok(())
    }
    .boxed()
}

fn simple_writer<'a>(_: &'a mut WorkerData, _: SmallRng) -> BoxFuture<'a, Result<()>> {
    async move {
        info!("This is a test log message");
        Ok(())
    }
    .boxed()
}

fn length_writer<'a>(_: &'a mut WorkerData, mut rng: SmallRng) -> BoxFuture<'a, Result<()>> {
    async move {
        let random_string_len = rng.gen_range(1, 1000);
        let mut random_string = String::new();

        for _ in 0..random_string_len {
            let c = rng.gen::<char>();
            random_string.push(c)
        }

        info!("{}", random_string);
        Ok(())
    }
    .boxed()
}
