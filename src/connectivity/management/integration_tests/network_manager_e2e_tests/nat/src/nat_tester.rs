// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Test a connection across a fuchsia router configured with NAT by network manager by passing
//! data between two devices.

use anyhow::{format_err, Context as _, Error};
use futures::{
    io::{AsyncReadExt, AsyncWriteExt},
    StreamExt,
};
use log::{error, info};

use fuchsia_async::{self as fasync, TimeoutExt};
use fuchsia_zircon::prelude::DurationNum;

use crate::device;
use crate::sync_manager;

/// Test runner.
pub struct NatTester {
    device: device::Type,
    sync: sync_manager::SyncManager,
    listener: Option<fasync::net::AcceptStream>,
}

impl NatTester {
    pub fn new(device: device::Type, sync: sync_manager::SyncManager) -> Self {
        NatTester { device, sync, listener: None }
    }

    /// Entry point into the test runner. Executes tests and publishes status (FINISHED or ERROR)
    /// of the test run to other devices.
    pub async fn run(&mut self) -> Result<(), Error> {
        match self.run_all().await {
            Ok(_) => {
                self.sync
                    .publish_status(device::STATUS_FINISHED)
                    .await
                    .context("error publishing finish status")?;
                self.sync
                    .wait_for_status_or_error(device::STATUS_FINISHED)
                    .await
                    .context("error waiting for finish")?;
                Ok(())
            }
            Err(e) => {
                error!("error running test: {:?}", e);
                self.sync
                    .publish_status(device::STATUS_ERROR)
                    .await
                    .context("error publishing error status")?;
                Err(e)
            }
        }
    }

    /// Runs all steps of the test:
    /// 1. Device specific setup
    /// 2. Mark ready status and wait for ready from other devices.
    /// 3. Run the actual test. Times out if not completed within 30s.
    async fn run_all(&mut self) -> Result<(), Error> {
        self.do_setup().await.context("error during setup")?;
        self.mark_ready().await.context("error waiting for ready status")?;
        self.run_test()
            .on_timeout(fasync::Time::after(30i64.seconds()), || {
                Err(format_err!("timeout waiting for run_test"))
            })
            .await?;
        Ok(())
    }

    /// Perform setup operations for current device. Currently only applicable to Wan device, which
    /// creates a listener socket.
    async fn do_setup(&mut self) -> Result<(), Error> {
        if self.device == device::Type::Wan {
            self.listener = Some(
                fasync::net::TcpListener::bind(&"0.0.0.0:8000".parse().expect("invalid listen ip"))
                    .context("error creating listener socket")?
                    .accept_stream(),
            );
        }
        Ok(())
    }

    /// Mark current device as Ready and wait for ready status from other devices.
    async fn mark_ready(&mut self) -> Result<(), Error> {
        self.sync
            .publish_status(device::STATUS_READY)
            .await
            .context("error publishing ready status")?;
        self.sync.wait_for_status_or_error(device::STATUS_READY).await
    }

    async fn run_test(&mut self) -> Result<(), Error> {
        match self.device {
            device::Type::Lan => self.do_lan().await,
            device::Type::Router => self.do_router().await,
            device::Type::Wan => self.do_wan().await,
        }
    }

    /// Connect to the wan device and send a 'ping'. Expect a 'pong' in response.
    async fn do_lan(&mut self) -> Result<(), Error> {
        let mut stream =
            fasync::net::TcpStream::connect("192.168.0.22:8000".parse().expect("bad ip"))
                .context("error creating socket")?
                .await
                .context("error connecting to wan")?;
        stream.write(b"ping").await.context("error writing to socket")?;
        stream.flush().await.context("error flushing")?;

        let mut response = String::new();
        stream.read_to_string(&mut response).await.context("error reading response")?;
        if response == "pong" {
            Ok(())
        } else {
            Err(format_err!("unexpected response: \"{:?}\"", response))
        }
    }

    /// Wait for a connection on the listener socket and expect a 'ping' string. Respond with a
    /// 'pong'.
    async fn do_wan(&mut self) -> Result<(), Error> {
        match self.listener.as_mut().expect("no tcp listener").next().await {
            Some(Ok((mut stream, addr))) => {
                info!("client {:?} connected", addr);

                let mut request = String::new();
                stream.read_to_string(&mut request).await?;
                if request != "ping" {
                    Err(format_err!("unexpected request: \"{:?}\"", request))
                } else {
                    stream.write(b"pong").await.context("error writing to socket")?;
                    stream.flush().await.context("error flushing")?;
                    Ok(())
                }
            }
            Some(Err(err)) => Err(format_err!("could not accept connection: {:?}", err)),
            None => Err(format_err!("empty accept")),
        }
    }

    async fn do_router(&mut self) -> Result<(), Error> {
        Ok(())
    }
}
