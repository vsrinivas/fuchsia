// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    errors::{ffx_bail, ffx_error},
    ffx_component::connect_to_lifecycle_controller,
    ffx_component_stop_args::ComponentStopCommand,
    ffx_core::ffx_plugin,
    fidl_fuchsia_developer_remotecontrol as rc, fidl_fuchsia_sys2 as fsys,
    moniker::{AbsoluteMonikerBase, PartialAbsoluteMoniker},
};

#[ffx_plugin]
pub async fn stop(rcs_proxy: rc::RemoteControlProxy, cmd: ComponentStopCommand) -> Result<()> {
    let lifecycle_controller = connect_to_lifecycle_controller(&rcs_proxy).await?;
    stop_impl(lifecycle_controller, cmd.moniker, cmd.recursive, &mut std::io::stdout()).await
}

async fn stop_impl<W: std::io::Write>(
    lifecycle_controller: fsys::LifecycleControllerProxy,
    moniker: String,
    recursive: bool,
    writer: &mut W,
) -> Result<()> {
    let moniker = PartialAbsoluteMoniker::parse_string_without_instances(&moniker)
        .map_err(|e| ffx_error!("Moniker could not be parsed: {}", e))?;
    writeln!(writer, "Moniker: {}", moniker)?;

    if recursive {
        writeln!(writer, "Stopping component instances recursively...")?;
    } else {
        writeln!(writer, "Stopping component instance...")?;
    }

    // LifecycleController accepts PartialRelativeMonikers only
    let moniker = format!(".{}", moniker.to_string_without_instances());
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
        std::io::BufWriter,
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
        let mut output = String::new();
        let mut writer = unsafe { BufWriter::new(output.as_mut_vec()) };
        let lifecycle_controller =
            setup_fake_lifecycle_controller("./core/ffx-laboratory:test", true);
        let response = stop_impl(
            lifecycle_controller,
            "/core/ffx-laboratory:test".to_string(),
            true,
            &mut writer,
        )
        .await;
        response.unwrap();
        Ok(())
    }
}
