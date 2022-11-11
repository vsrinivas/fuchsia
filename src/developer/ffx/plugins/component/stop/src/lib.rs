// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    errors::ffx_bail,
    ffx_component::{
        query::get_cml_moniker_from_query,
        rcs::{connect_to_lifecycle_controller, connect_to_realm_explorer},
    },
    ffx_component_stop_args::ComponentStopCommand,
    ffx_core::ffx_plugin,
    fidl_fuchsia_developer_remotecontrol as rc, fidl_fuchsia_sys2 as fsys,
    moniker::AbsoluteMoniker,
};

#[ffx_plugin]
pub async fn stop(rcs_proxy: rc::RemoteControlProxy, cmd: ComponentStopCommand) -> Result<()> {
    let lifecycle_controller = connect_to_lifecycle_controller(&rcs_proxy).await?;

    let realm_explorer = connect_to_realm_explorer(&rcs_proxy).await?;
    let moniker = get_cml_moniker_from_query(&cmd.query, &realm_explorer).await?;

    stop_impl(lifecycle_controller, moniker, cmd.recursive, &mut std::io::stdout()).await
}

async fn stop_impl<W: std::io::Write>(
    lifecycle_controller: fsys::LifecycleControllerProxy,
    moniker: AbsoluteMoniker,
    recursive: bool,
    writer: &mut W,
) -> Result<()> {
    writeln!(writer, "Moniker: {}", moniker)?;

    if recursive {
        writeln!(writer, "Stopping component instances recursively...")?;
    } else {
        writeln!(writer, "Stopping component instance...")?;
    }

    // LifecycleController accepts RelativeMonikers only
    let moniker = format!(".{}", moniker.to_string());
    match lifecycle_controller.stop(&moniker, recursive).await {
        Ok(Ok(())) => Ok(()),
        Ok(Err(e)) => {
            ffx_bail!("Lifecycle protocol could not stop the component instance: {:?}", e)
        }
        Err(e) => ffx_bail!("FIDL error: {:?}", e),
    }
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
        expected_is_recursive: bool,
    ) -> fsys::LifecycleControllerProxy {
        let (lifecycle_controller, mut stream) =
            create_proxy_and_stream::<fsys::LifecycleControllerMarker>().unwrap();
        fuchsia_async::Task::local(async move {
            let req = stream.try_next().await.unwrap().unwrap();
            match req {
                fsys::LifecycleControllerRequest::Stop {
                    moniker, is_recursive, responder, ..
                } => {
                    assert_eq!(expected_moniker, moniker);
                    assert_eq!(expected_is_recursive, is_recursive);
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
        let lifecycle_controller =
            setup_fake_lifecycle_controller("./core/ffx-laboratory:test", true);
        let response = stop_impl(
            lifecycle_controller,
            AbsoluteMoniker::parse_str("/core/ffx-laboratory:test").unwrap(),
            true,
            &mut output,
        )
        .await;
        response.unwrap();
        Ok(())
    }
}
