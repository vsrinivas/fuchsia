// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! `diagnostics-kcounter` component publishes kcounter Inspect VMO.

use {
    anyhow::Error,
    argh::FromArgs,
    fidl_fuchsia_kernel::{CounterMarker, CounterProxy},
    fuchsia_component::{client::connect_to_protocol, server::ServiceFs},
    fuchsia_zircon as zx,
    futures::prelude::*,
    std::sync::Arc,
    tracing::*,
};

/// The name of the subcommand and the logs-tag.
pub const PROGRAM_NAME: &str = "kcounter";

/// Command line args
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "kcounter")]
pub struct CommandLine {}

async fn publish_kcounter_inspect(
    kcounter: CounterProxy,
    inspector: &fuchsia_inspect::Inspector,
) -> Result<(), Error> {
    let (state, inspect_vmo) = kcounter.get_inspect_vmo().await?;
    zx::ok(state)?;
    let vmo = Arc::new(inspect_vmo.vmo);
    // We are adding the VMO as lazy values to root; there won't be a node called "kcounter".
    inspector.root().record_lazy_values("kcounter", move || {
        let kcounter_clone = kcounter.clone();
        let vmo_clone = vmo.clone();
        async move {
            zx::ok(kcounter_clone.update_inspect_vmo().check()?.await?)?;
            Ok(fuchsia_inspect::Inspector::no_op_from_vmo(vmo_clone))
        }
        .boxed()
    });
    Ok(())
}

pub async fn main() -> Result<(), Error> {
    let mut fs = ServiceFs::new();
    fs.take_and_serve_directory_handle()?;
    let inspector = fuchsia_inspect::component::inspector();
    inspect_runtime::serve(inspector, &mut fs)?;
    publish_kcounter_inspect(connect_to_protocol::<CounterMarker>()?, inspector).await?;

    info!("Serving");
    fs.collect::<()>().await;
    info!("Exiting");
    Ok(())
}

#[cfg(test)]
mod tests {
    use {
        super::*, fidl_fuchsia_kernel::CounterRequest, fidl_fuchsia_mem::Buffer,
        fuchsia_async as fasync, std::sync::Mutex,
    };

    #[fasync::run_until_stalled(test)]
    async fn test_kcounter_proxy() -> Result<(), Error> {
        let (proxy, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<CounterMarker>().unwrap();
        let counter = Arc::new(Mutex::new((0, 0)));
        let counter_in_server = counter.clone();
        let _task = fasync::Task::local(async move {
            while let Some(req) = stream.next().await {
                let mut counter = counter_in_server.lock().unwrap();
                match req {
                    Ok(CounterRequest::GetInspectVmo { responder }) => {
                        (*counter).0 += 1;
                        let vmo = zx::Vmo::create(0).unwrap();
                        responder
                            .send(zx::Status::OK.into_raw(), &mut Buffer { vmo, size: 0 })
                            .expect("Failed to respond to GetInspectVmo");
                    }
                    Ok(CounterRequest::UpdateInspectVmo { responder }) => {
                        (*counter).1 += 1;
                        responder
                            .send(zx::Status::OK.into_raw())
                            .expect("Failed to respond to UpdateInspectVmo");
                    }
                    Err(e) => {
                        assert!(false, "Server error: {}", e);
                    }
                };
            }
        });

        // Use a local Inspector instead of the static singleton so fasync won't complain about
        // leaking resources.
        let inspector = fuchsia_inspect::Inspector::new();
        publish_kcounter_inspect(proxy, &inspector).await?;
        {
            let counter = counter.lock().unwrap();
            assert_eq!((*counter).0, 1, "Incorrect call(s) to GetInspectVmo");
            assert_eq!((*counter).1, 0, "Incorrect call(s) to UpdateInspectVmo");
        };
        fuchsia_inspect::reader::read(&inspector).await?;
        {
            let counter = counter.lock().unwrap();
            assert_eq!((*counter).0, 1, "Incorrect call(s) to GetInspectVmo");
            assert_eq!((*counter).1, 1, "Incorrect call(s) to UpdateInspectVmo");
        };
        fuchsia_inspect::reader::read(&inspector).await?;
        {
            let counter = counter.lock().unwrap();
            assert_eq!((*counter).0, 1, "Incorrect call(s) to GetInspectVmo");
            assert_eq!((*counter).1, 2, "Incorrect call(s) to UpdateInspectVmo");
        };
        Ok(())
    }
}
