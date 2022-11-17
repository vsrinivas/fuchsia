// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    component_debug::lifecycle::create_instance_in_collection,
    errors::ffx_error,
    ffx_component::{
        format_lifecycle_error, parse_component_url, rcs::connect_to_lifecycle_controller,
    },
    ffx_component_create_args::CreateComponentCommand,
    ffx_core::ffx_plugin,
    fidl_fuchsia_developer_remotecontrol as rc, fidl_fuchsia_sys2 as fsys,
    moniker::{AbsoluteMoniker, AbsoluteMonikerBase, RelativeMoniker, RelativeMonikerBase},
};

#[ffx_plugin]
pub async fn create(rcs_proxy: rc::RemoteControlProxy, cmd: CreateComponentCommand) -> Result<()> {
    let lifecycle_controller = connect_to_lifecycle_controller(&rcs_proxy).await?;
    create_impl(lifecycle_controller, cmd.moniker, cmd.url, &mut std::io::stdout()).await
}

async fn create_impl<W: std::io::Write>(
    lifecycle_controller: fsys::LifecycleControllerProxy,
    moniker: String,
    url: String,
    writer: &mut W,
) -> Result<()> {
    let url = parse_component_url(url.as_str())?;

    let moniker = AbsoluteMoniker::parse_str(&moniker)
        .map_err(|e| ffx_error!("Moniker could not be parsed: {}", e))?;

    writeln!(writer, "URL: {}", url)?;
    writeln!(writer, "Moniker: {}", moniker)?;
    writeln!(writer, "Creating component instance...")?;

    // Convert the absolute moniker into a relative moniker w.r.t. root.
    // LifecycleController expects relative monikers only.
    let moniker = RelativeMoniker::scope_down(&AbsoluteMoniker::root(), &moniker).unwrap();

    create_instance_in_collection(&lifecycle_controller, &moniker, &url)
        .await
        .map_err(format_lifecycle_error)?;

    writeln!(writer, "Created component instance!")?;
    Ok(())
}

////////////////////////////////////////////////////////////////////////////////
// tests

#[cfg(test)]
mod test {
    use {super::*, fidl::endpoints::create_proxy_and_stream, futures::TryStreamExt};

    fn setup_fake_lifecycle_controller(
        expected_moniker: &'static str,
        expected_collection: &'static str,
        expected_name: &'static str,
        expected_url: &'static str,
    ) -> fsys::LifecycleControllerProxy {
        let (lifecycle_controller, mut stream) =
            create_proxy_and_stream::<fsys::LifecycleControllerMarker>().unwrap();
        fuchsia_async::Task::local(async move {
            let req = stream.try_next().await.unwrap().unwrap();
            match req {
                fsys::LifecycleControllerRequest::CreateChild {
                    parent_moniker,
                    collection,
                    decl,
                    responder,
                    ..
                } => {
                    assert_eq!(expected_moniker, parent_moniker);
                    assert_eq!(expected_collection, collection.name);
                    assert_eq!(expected_name, decl.name.unwrap());
                    assert_eq!(expected_url, decl.url.unwrap());
                    responder.send(&mut Ok(())).unwrap();
                }
                _ => panic!("Unexpected Lifecycle Controller request"),
            }
        })
        .detach();
        lifecycle_controller
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_success() -> Result<()> {
        let mut output = Vec::new();
        let lifecycle_controller = setup_fake_lifecycle_controller(
            "./core",
            "ffx-laboratory",
            "test",
            "fuchsia-pkg://fuchsia.com/test#meta/test.cm",
        );
        let response = create_impl(
            lifecycle_controller,
            "/core/ffx-laboratory:test".to_string(),
            "fuchsia-pkg://fuchsia.com/test#meta/test.cm".to_string(),
            &mut output,
        )
        .await;
        response.unwrap();
        Ok(())
    }
}
