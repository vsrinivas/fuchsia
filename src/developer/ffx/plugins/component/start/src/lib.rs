// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    component_debug::lifecycle::start_instance,
    ffx_component::{
        format_lifecycle_error,
        query::get_cml_moniker_from_query,
        rcs::{connect_to_lifecycle_controller, connect_to_realm_explorer},
    },
    ffx_component_start_args::ComponentStartCommand,
    ffx_core::ffx_plugin,
    fidl_fuchsia_developer_remotecontrol as rc, fidl_fuchsia_sys2 as fsys,
    moniker::{AbsoluteMoniker, AbsoluteMonikerBase, RelativeMoniker, RelativeMonikerBase},
};

#[ffx_plugin()]
pub async fn start(rcs_proxy: rc::RemoteControlProxy, cmd: ComponentStartCommand) -> Result<()> {
    let lifecycle_controller = connect_to_lifecycle_controller(&rcs_proxy).await?;

    let realm_explorer = connect_to_realm_explorer(&rcs_proxy).await?;
    let moniker = get_cml_moniker_from_query(&cmd.query, &realm_explorer).await?;

    start_impl(lifecycle_controller, moniker, &mut std::io::stdout()).await?;
    Ok(())
}

async fn start_impl<W: std::io::Write>(
    lifecycle_controller: fsys::LifecycleControllerProxy,
    moniker: AbsoluteMoniker,
    writer: &mut W,
) -> Result<fsys::StartResult> {
    writeln!(writer, "Moniker: {}", moniker)?;
    writeln!(writer, "Starting component instance...")?;

    // Convert the absolute moniker into a relative moniker w.r.t. root.
    // LifecycleController expects relative monikers only.
    let relative_moniker = RelativeMoniker::scope_down(&AbsoluteMoniker::root(), &moniker).unwrap();

    let result = start_instance(&lifecycle_controller, &relative_moniker)
        .await
        .map_err(format_lifecycle_error)?;

    match result {
        fsys::StartResult::Started => writeln!(writer, "Started component instance!")?,
        fsys::StartResult::AlreadyStarted => {
            writeln!(writer, "Component instance was already running!")?
        }
    }
    Ok(result)
}

////////////////////////////////////////////////////////////////////////////////
// tests

#[cfg(test)]
mod test {
    use {
        super::*, fidl::endpoints::create_proxy_and_stream, futures::TryStreamExt,
        moniker::AbsoluteMonikerBase,
    };

    fn setup_fake_lifecycle_controller(
        expected_moniker: &'static str,
        is_running: bool,
    ) -> fsys::LifecycleControllerProxy {
        let (lifecycle_controller, mut stream) =
            create_proxy_and_stream::<fsys::LifecycleControllerMarker>().unwrap();
        fuchsia_async::Task::local(async move {
            let req = stream.try_next().await.unwrap().unwrap();
            match req {
                fsys::LifecycleControllerRequest::Start { moniker, responder, .. } => {
                    assert_eq!(expected_moniker, moniker);
                    let sr = if is_running {
                        fsys::StartResult::AlreadyStarted
                    } else {
                        fsys::StartResult::Started
                    };
                    responder.send(&mut Ok(sr)).unwrap();
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
        let lifecycle_controller =
            setup_fake_lifecycle_controller("./core/ffx-laboratory:test", false);
        let response = start_impl(
            lifecycle_controller,
            AbsoluteMoniker::parse_str("/core/ffx-laboratory:test").unwrap(),
            &mut output,
        )
        .await;
        assert_eq!(response.unwrap(), fsys::StartResult::Started);
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_already_started() -> Result<()> {
        let mut output = Vec::new();
        let lifecycle_controller =
            setup_fake_lifecycle_controller("./core/ffx-laboratory:test", true);
        let response = start_impl(
            lifecycle_controller,
            AbsoluteMoniker::parse_str("/core/ffx-laboratory:test").unwrap(),
            &mut output,
        )
        .await;
        assert_eq!(response.unwrap(), fsys::StartResult::AlreadyStarted);
        Ok(())
    }
}
