// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{builtin::capability::BuiltinCapability, capability::InternalCapability},
    anyhow::{anyhow, Context, Error},
    async_trait::async_trait,
    cm_rust::{CapabilityNameOrPath, CapabilityPath},
    fidl_fuchsia_time as ftime,
    fuchsia_zircon::{Clock, ClockOpts, ClockUpdate, HandleBased, Rights, Time},
    futures::prelude::*,
    io_util::{file, OPEN_RIGHT_READABLE},
    lazy_static::lazy_static,
    std::{convert::TryInto, sync::Arc},
};

lazy_static! {
    static ref TIME_MAINTENANCE_CAPABILITY_PATH: CapabilityPath =
        "/svc/fuchsia.time.Maintenance".try_into().unwrap();
}

/// An implementation of the `fuchsia.time.Maintenance` protocol, which
/// maintains a UTC clock, vending out handles with write access.
/// Consumers of this protocol are meant to keep the clock synchronized
/// with external time sources.
pub struct UtcTimeMaintainer {
    utc_clock: Arc<Clock>,
}

impl UtcTimeMaintainer {
    pub fn new(utc_clock: Arc<Clock>) -> Self {
        UtcTimeMaintainer { utc_clock }
    }
}

#[async_trait]
impl BuiltinCapability for UtcTimeMaintainer {
    const NAME: &'static str = "TimeMaintenance";
    type Marker = ftime::MaintenanceMarker;

    async fn serve(
        self: Arc<Self>,
        mut stream: ftime::MaintenanceRequestStream,
    ) -> Result<(), Error> {
        while let Some(ftime::MaintenanceRequest::GetWritableUtcClock { responder }) =
            stream.try_next().await?
        {
            responder.send(self.utc_clock.duplicate_handle(Rights::SAME_RIGHTS)?)?;
        }
        Ok(())
    }

    fn matches_routed_capability(&self, capability: &InternalCapability) -> bool {
        matches!(
            capability,
            InternalCapability::Protocol(CapabilityNameOrPath::Path(path)) if *path == *TIME_MAINTENANCE_CAPABILITY_PATH
        )
    }
}

async fn read_utc_backstop(path: &str) -> Result<Time, Error> {
    let file_proxy = file::open_in_namespace(path, OPEN_RIGHT_READABLE)
        .context("failed to open backstop time file from disk")?;
    let file_contents = file::read_to_string(&file_proxy)
        .await
        .context("failed to read backstop time from disk")?;
    let parsed_time =
        file_contents.trim().parse::<i64>().context("failed to parse backstop time")?;
    Ok(Time::from_nanos(
        parsed_time
            .checked_mul(1_000_000_000)
            .ok_or_else(|| anyhow!("backstop time is too large"))?,
    ))
}

/// Creates a UTC kernel clock with a backstop time configured by /boot,
/// and immediately starts it, setting its time to the backstop, or zero
/// if no backstop exists.
pub async fn create_and_start_utc_clock() -> Result<Clock, Error> {
    let backstop = read_utc_backstop("/boot/config/build_info/minimum_utc_stamp").await?;
    let clock = Clock::create(ClockOpts::empty(), Some(backstop))
        .map_err(|s| anyhow!("failed to create UTC clock: {}", s))?;
    clock
        .update(ClockUpdate::new().value(backstop))
        .map_err(|s| anyhow!("failed to start UTC clock: {}", s))?;
    Ok(clock)
}
