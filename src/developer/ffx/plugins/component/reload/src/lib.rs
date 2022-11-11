// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    errors::ffx_bail,
    ffx_component::{
        query::get_cml_moniker_from_query,
        rcs::{connect_to_lifecycle_controller, connect_to_realm_explorer},
    },
    ffx_component_reload_args::ReloadComponentCommand,
    ffx_core::ffx_plugin,
    fidl_fuchsia_developer_remotecontrol as rc, fidl_fuchsia_sys2 as fsys,
    moniker::AbsoluteMoniker,
};

#[ffx_plugin()]
pub async fn reload(rcs_proxy: rc::RemoteControlProxy, cmd: ReloadComponentCommand) -> Result<()> {
    let lifecycle_controller = connect_to_lifecycle_controller(&rcs_proxy).await?;

    let realm_explorer = connect_to_realm_explorer(&rcs_proxy).await?;
    let moniker = get_cml_moniker_from_query(&cmd.query, &realm_explorer).await?;

    println!("Moniker: {}", moniker);

    reload_impl(lifecycle_controller, moniker, &mut std::io::stdout()).await
}

async fn reload_impl<W: std::io::Write>(
    lifecycle_controller: fsys::LifecycleControllerProxy,
    moniker: AbsoluteMoniker,
    writer: &mut W,
) -> Result<()> {
    // LifecycleController accepts RelativeMonikers only.
    let moniker = format!(".{}", moniker.to_string());

    // First: Unresolve the component recursively, first shutting it down.
    writeln!(writer, "Unresolving the component...")?;
    match lifecycle_controller.unresolve(&moniker).await {
        Ok(Ok(())) => {}
        Ok(Err(e)) => {
            ffx_bail!("Lifecycle protocol could not unresolve the component instance: {:?}", e);
        }
        Err(e) => {
            ffx_bail!("FIDL error: {:?}", e);
        }
    };

    // Then restart the component.
    writeln!(writer, "Restarting the component...")?;
    match lifecycle_controller.start(&moniker).await {
        Ok(sr) => match sr {
            Ok(fsys::StartResult::Started) => {
                writeln!(writer, "Component started.")?;
                Ok(())
            }
            Ok(fsys::StartResult::AlreadyStarted) => {
                ffx_bail!("Lifecycle protocol could not start component: already-running")
            }
            Err(e) => {
                ffx_bail!("Lifecycle protocol could not start the component instance: {:?}", e)
            }
        },
        Err(e) => {
            ffx_bail!("FIDL error: {:?}", e)
        }
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
    ) -> fsys::LifecycleControllerProxy {
        let (lifecycle_controller, mut stream) =
            create_proxy_and_stream::<fsys::LifecycleControllerMarker>().unwrap();

        fuchsia_async::Task::local(async move {
            // Expect 2 requests: Unresolve, Start.
            match stream.try_next().await.unwrap().unwrap() {
                fsys::LifecycleControllerRequest::Unresolve { moniker, responder, .. } => {
                    assert_eq!(expected_moniker, moniker);
                    responder.send(&mut Ok(())).unwrap();
                }
                r => panic!(
                    "Unexpected Lifecycle Controller request when expecting Unresolve: {:?}",
                    r
                ),
            }
            match stream.try_next().await.unwrap().unwrap() {
                fsys::LifecycleControllerRequest::Start { moniker, responder, .. } => {
                    assert_eq!(expected_moniker, moniker);
                    responder.send(&mut Ok(fsys::StartResult::Started)).unwrap();
                }
                r => {
                    panic!("Unexpected Lifecycle Controller request when expecting Start: {:?}", r)
                }
            }
        })
        .detach();
        lifecycle_controller
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_success() -> Result<()> {
        let mut output = Vec::new();
        let lifecycle_controller = setup_fake_lifecycle_controller("./core/ffx-laboratory:test");
        let response = reload_impl(
            lifecycle_controller,
            AbsoluteMoniker::parse_str("/core/ffx-laboratory:test").unwrap(),
            &mut output,
        )
        .await;
        response.unwrap();
        Ok(())
    }
}
