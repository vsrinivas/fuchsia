// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await)]

use failure::Error;
use fidl_fuchsia_ledger_cloud::CloudProviderRequestStream;
use fidl_fuchsia_ledger_cloud_test::CloudControllerFactoryRequestStream;
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use fuchsia_syslog;
use futures::prelude::*;

use cloud_provider_memory_diff_lib::{CloudControllerFactory, CloudFactory};

enum IncomingServices {
    CloudProvider(CloudProviderRequestStream),
    CloudControllerFactory(CloudControllerFactoryRequestStream),
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init()?;

    let cloud_factory = CloudFactory::new();
    let mut fs = ServiceFs::new_local();
    fs.dir("svc")
        .add_fidl_service(IncomingServices::CloudProvider)
        .add_fidl_service(IncomingServices::CloudControllerFactory);

    fs.take_and_serve_directory_handle()?;

    let fut = fs.for_each_concurrent(None, |req| match req {
        IncomingServices::CloudProvider(stream) => cloud_factory.spawn(stream),
        IncomingServices::CloudControllerFactory(stream) => {
            CloudControllerFactory::new(stream).run()
        }
    });

    fut.await;
    Ok(())
}
