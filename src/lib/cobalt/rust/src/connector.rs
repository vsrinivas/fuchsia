// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Contains the logic for connecting to the Cobalt FIDL service, as well as setting up a worker
//! thread to handle sending `CobaltEvent`s off of the main thread.

use {
    crate::sender::CobaltSender,
    failure::{bail, Error, ResultExt},
    fdio, fidl,
    fidl_fuchsia_cobalt::{
        CobaltEvent, LoggerFactoryMarker, LoggerProxy, ProjectProfile, ReleaseStage, Status,
    },
    fidl_fuchsia_mem as fuchsia_mem,
    fuchsia_component::client::connect_to_service,
    futures::{channel::mpsc, prelude::*, StreamExt},
    log::error,
    std::{borrow::Cow, fs::File, io::Seek},
};

/// Determines how to connect to the Cobalt FIDL service.
#[derive(Clone, Debug, Eq, PartialEq, Hash, Ord, PartialOrd)]
pub enum ConnectionType {
    /// Connecting with ProjectName relys on the Cobalt FIDL service's internal copy of the metrics
    /// registry.
    ProjectName(Cow<'static, str>),

    /// Connecting with ConfigPath provides the Cobalt FIDL service with your own copy of the
    /// metrics registry. This can be useful if your module will be updated out-of-band from the
    /// Cobalt FIDL service.
    ConfigPath(Cow<'static, str>),
}

impl ConnectionType {
    /// Constructs a `ConnectionType::ProjectName(_)`
    pub fn project_name<S: Into<Cow<'static, str>>>(s: S) -> Self {
        ConnectionType::ProjectName(s.into())
    }

    /// Constructs a `ConnectionType::ConfigPath(_)`
    pub fn config_path<S: Into<Cow<'static, str>>>(s: S) -> Self {
        ConnectionType::ConfigPath(s.into())
    }
}

/// Information required for connecting to the Cobalt FIDL service.
#[derive(Debug, Clone, Eq, PartialEq, Hash, Ord, PartialOrd)]
pub struct CobaltConnector {
    /// The size of the `mpsc::channel` to use when sending `CobaltEvent`s from the main thread to
    /// the worker thread.
    pub buffer_size: usize,

    /// The release stage of this client's software. Determines which metrics are collected by this
    /// instance of the Cobalt FIDL service. Defaults to `ReleaseStage::Ga` (the most restrictive)
    pub release_stage: ReleaseStage,
}

/// At the time of this writing, all clients are using a buffer size of 100, so we've set this as
/// the default value. It is unclear whether this is a good default since it was originally
/// chosen arbitrarily, so for now it may be considered a placeholder value.
///
/// This number determines how backed up the cobalt sender thread can get before the
/// `CobaltSender` is unable to queue up more `CobaltEvent`s.
pub const DEFAULT_BUFFER_SIZE: usize = 100;

impl Default for CobaltConnector {
    fn default() -> Self {
        Self { buffer_size: DEFAULT_BUFFER_SIZE, release_stage: ReleaseStage::Ga }
    }
}

impl CobaltConnector {
    /// Connects to the Cobalt FIDL service. And returns a tuple of type `(CobaltSender, impl
    /// Future<Output = ()>)`. The `CobaltSender` is used for sending cobalt events to the FIDL
    /// service, and the `impl Future<Output = ()>` represents the sending thread and can be joined
    /// against.
    ///
    /// # Arguments
    ///
    /// * `connection_type` - The `ConnectionType` desired.
    pub fn serve(
        self,
        connection_type: ConnectionType,
    ) -> (CobaltSender, impl Future<Output = ()>) {
        let (sender, receiver) = mpsc::channel(self.buffer_size);
        let sender = CobaltSender::new(sender);
        let fut = async move {
            let logger = match await!(self.get_cobalt_logger(connection_type)) {
                Ok(logger) => logger,
                Err(e) => {
                    error!("Error obtaining a Cobalt Logger: {}", e);
                    return;
                }
            };
            await!(Self::send_cobalt_events(logger, receiver))
        };
        (sender, fut)
    }

    async fn get_cobalt_logger(
        self,
        connection_type: ConnectionType,
    ) -> Result<LoggerProxy, Error> {
        let (logger_proxy, server_end) =
            fidl::endpoints::create_proxy().context("Failed to create endpoints")?;
        let logger_factory = connect_to_service::<LoggerFactoryMarker>()
            .context("Failed to connect to the Cobalt LoggerFactory")?;

        let res = match connection_type {
            ConnectionType::ProjectName(project_name) => await!(logger_factory
                .create_logger_from_project_name(&project_name, self.release_stage, server_end)),

            ConnectionType::ConfigPath(config_path) => {
                let mut cobalt_config = File::open(config_path.as_ref())?;
                let vmo = fdio::get_vmo_copy_from_file(&cobalt_config)?;
                let size = cobalt_config.seek(std::io::SeekFrom::End(0))?;

                let config = fuchsia_mem::Buffer { vmo, size };

                await!(logger_factory.create_logger(
                    &mut ProjectProfile { config, release_stage: self.release_stage },
                    server_end,
                ))
            }
        };

        Self::handle_cobalt_factory_result(res, "failed to obtain logger")?;
        Ok(logger_proxy)
    }

    fn handle_cobalt_factory_result(
        r: Result<Status, fidl::Error>,
        context: &str,
    ) -> Result<(), failure::Error> {
        match r {
            Ok(Status::Ok) => Ok(()),
            Ok(other) => bail!("{}: {:?}", context, other),
            Err(e) => bail!("{}: {}", context, e),
        }
    }

    async fn send_cobalt_events(logger: LoggerProxy, mut receiver: mpsc::Receiver<CobaltEvent>) {
        let mut is_full = false;
        while let Some(mut event) = await!(receiver.next()) {
            let resp = await!(logger.log_cobalt_event(&mut event));
            if let Err(e) = Self::throttle_cobalt_error(resp, event.metric_id, &mut is_full) {
                error!("{}", e);
            }
        }
    }

    fn throttle_cobalt_error(
        resp: Result<Status, fidl::Error>,
        metric_id: u32,
        is_full: &mut bool,
    ) -> Result<(), failure::Error> {
        let was_full = *is_full;
        *is_full = resp.as_ref().ok() == Some(&Status::BufferFull);
        match resp {
            Ok(Status::BufferFull) => {
                if !was_full {
                    bail!("Cobalt buffer became full. Cannot report the stats")
                }
                Ok(())
            }
            Ok(Status::Ok) => Ok(()),
            Ok(other) => bail!("Cobalt returned an error for metric {}: {:?}", metric_id, other),
            Err(e) => bail!("Failed to send event to Cobalt for metric {}: {}", metric_id, e),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl_fuchsia_cobalt::Status;

    #[test]
    fn throttle_errors() {
        let mut is_full = false;

        let cobalt_resp = Ok(Status::Ok);
        assert!(CobaltConnector::throttle_cobalt_error(cobalt_resp, 1, &mut is_full).is_ok());
        assert_eq!(is_full, false);

        let cobalt_resp = Ok(Status::InvalidArguments);
        assert!(CobaltConnector::throttle_cobalt_error(cobalt_resp, 1, &mut is_full).is_err());
        assert_eq!(is_full, false);

        let cobalt_resp = Ok(Status::BufferFull);
        assert!(CobaltConnector::throttle_cobalt_error(cobalt_resp, 1, &mut is_full).is_err());
        assert_eq!(is_full, true);

        let cobalt_resp = Ok(Status::BufferFull);
        assert!(CobaltConnector::throttle_cobalt_error(cobalt_resp, 1, &mut is_full).is_ok());
        assert_eq!(is_full, true);

        let cobalt_resp = Ok(Status::Ok);
        assert!(CobaltConnector::throttle_cobalt_error(cobalt_resp, 1, &mut is_full).is_ok());
        assert_eq!(is_full, false);

        let cobalt_resp = Err(fidl::Error::ClientWrite(fuchsia_zircon::Status::PEER_CLOSED));
        assert!(CobaltConnector::throttle_cobalt_error(cobalt_resp, 1, &mut is_full).is_err());
        assert_eq!(is_full, false);
    }
}
