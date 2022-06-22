// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! LoWPAN Service for Fuchsia

pub mod inspect;
pub mod service;

use anyhow::{format_err, Context as _, Error};
use fidl_fuchsia_factory_lowpan::{FactoryLookupRequestStream, FactoryRegisterRequestStream};
use fidl_fuchsia_lowpan_driver::RegisterRequestStream;
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use fuchsia_inspect::Inspector;
use fuchsia_syslog::macros::*;
use futures::prelude::*;
use futures::task::{FutureObj, Spawn, SpawnError};
use lowpan_driver_common::lowpan_fidl::*;
use lowpan_driver_common::ServeTo;
use service::*;
use std::default::Default;
use std::sync::Arc;

enum IncomingService {
    DeviceWatcher(DeviceWatcherRequestStream),
    Register(RegisterRequestStream),
    FactoryLookup(FactoryLookupRequestStream),
    FactoryRegister(FactoryRegisterRequestStream),
    DeviceConnector(DeviceConnectorRequestStream),
    DeviceExtraConnector(DeviceExtraConnectorRequestStream),
    DeviceRouteConnector(DeviceRouteConnectorRequestStream),
    DeviceRouteExtraConnector(DeviceRouteExtraConnectorRequestStream),
    CountersConnector(CountersConnectorRequestStream),
    DeviceTestConnector(DeviceTestConnectorRequestStream),
    LegacyJoiningConnector(LegacyJoiningConnectorRequestStream),
    DatasetConnector(DatasetConnectorRequestStream),
    MeshcopConnector(MeshcopConnectorRequestStream),
    EnergyScanConnector(EnergyScanConnectorRequestStream),
    ExperimentalDeviceConnector(ExperimentalDeviceConnectorRequestStream),
    ExperimentalDeviceExtraConnector(ExperimentalDeviceExtraConnectorRequestStream),
    TelemetryProviderConnector(TelemetryProviderConnectorRequestStream),
}

const MAX_CONCURRENT: usize = 100;

/// Type that implements futures::task::Spawn and uses
/// Fuchsia's port-based global executor.
pub struct FuchsiaGlobalExecutor;
impl Spawn for FuchsiaGlobalExecutor {
    fn spawn_obj(&self, future: FutureObj<'static, ()>) -> Result<(), SpawnError> {
        fasync::Task::spawn(future).detach();
        Ok(())
    }
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["lowpan"]).context("initialize logging")?;
    fuchsia_syslog::set_severity(fuchsia_syslog::levels::INFO);

    fx_log_info!("LoWPAN Service starting up");

    let service = LowpanService::with_spawner(FuchsiaGlobalExecutor);

    let mut fs = ServiceFs::new_local();

    // Creates a new inspector object. This will create the "root" node in the
    // inspect tree to which further children objects can be added.
    let inspector = fuchsia_inspect::Inspector::new();
    inspect_runtime::serve(&inspector, &mut fs)?;
    let inspect_tree = Arc::new(inspect::LowpanServiceTree::new(inspector));
    let inspect_fut = inspect::start_inspect_process(inspect_tree).map(|ret| {
        fx_log_err!("Inspect process terminated: {:?}", ret);
    });

    fs.dir("svc")
        .add_fidl_service(IncomingService::DeviceWatcher)
        .add_fidl_service(IncomingService::Register)
        .add_fidl_service(IncomingService::FactoryLookup)
        .add_fidl_service(IncomingService::FactoryRegister)
        .add_fidl_service(IncomingService::DeviceConnector)
        .add_fidl_service(IncomingService::DeviceExtraConnector)
        .add_fidl_service(IncomingService::DeviceRouteConnector)
        .add_fidl_service(IncomingService::DeviceRouteExtraConnector)
        .add_fidl_service(IncomingService::CountersConnector)
        .add_fidl_service(IncomingService::DeviceTestConnector)
        .add_fidl_service(IncomingService::LegacyJoiningConnector)
        .add_fidl_service(IncomingService::DatasetConnector)
        .add_fidl_service(IncomingService::ExperimentalDeviceConnector)
        .add_fidl_service(IncomingService::ExperimentalDeviceExtraConnector)
        .add_fidl_service(IncomingService::TelemetryProviderConnector)
        .add_fidl_service(IncomingService::MeshcopConnector)
        .add_fidl_service(IncomingService::EnergyScanConnector);

    fs.take_and_serve_directory_handle()?;

    let fut = fs.for_each_concurrent(MAX_CONCURRENT, |request| async {
        if let Err(err) = match request {
            IncomingService::DeviceWatcher(stream) => service.serve_to(stream).await,
            IncomingService::Register(stream) => service.serve_to(stream).await,
            IncomingService::FactoryLookup(stream) => service.serve_to(stream).await,
            IncomingService::FactoryRegister(stream) => service.serve_to(stream).await,
            IncomingService::DeviceConnector(stream) => service.serve_to(stream).await,
            IncomingService::DeviceExtraConnector(stream) => service.serve_to(stream).await,
            IncomingService::DeviceRouteConnector(stream) => service.serve_to(stream).await,
            IncomingService::DeviceRouteExtraConnector(stream) => service.serve_to(stream).await,
            IncomingService::CountersConnector(stream) => service.serve_to(stream).await,
            IncomingService::DeviceTestConnector(stream) => service.serve_to(stream).await,
            IncomingService::LegacyJoiningConnector(stream) => service.serve_to(stream).await,
            IncomingService::DatasetConnector(stream) => service.serve_to(stream).await,
            IncomingService::MeshcopConnector(stream) => service.serve_to(stream).await,
            IncomingService::EnergyScanConnector(stream) => service.serve_to(stream).await,
            IncomingService::ExperimentalDeviceConnector(stream) => service.serve_to(stream).await,
            IncomingService::ExperimentalDeviceExtraConnector(stream) => {
                service.serve_to(stream).await
            }
            IncomingService::TelemetryProviderConnector(stream) => service.serve_to(stream).await,
        } {
            fx_log_err!("{:?}", err);
        }
    });

    futures::future::select(fut.boxed_local(), inspect_fut.boxed_local()).await;

    fx_log_info!("LoWPAN Service shut down");

    Ok(())
}
