// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    errors::{ffx_bail, ffx_error},
    ffx_component::connect_to_lifecycle_controller,
    ffx_component_bind_args::ComponentBindCommand,
    ffx_core::ffx_plugin,
    fidl_fuchsia_developer_remotecontrol as rc, fidl_fuchsia_sys2 as fsys,
    moniker::{AbsoluteMonikerBase, PartialAbsoluteMoniker},
};

#[ffx_plugin()]
pub async fn bind(rcs_proxy: rc::RemoteControlProxy, cmd: ComponentBindCommand) -> Result<()> {
    let lifecycle_controller = connect_to_lifecycle_controller(&rcs_proxy).await?;
    bind_impl(lifecycle_controller, cmd.moniker, &mut std::io::stdout()).await
}

async fn bind_impl<W: std::io::Write>(
    lifecycle_controller: fsys::LifecycleControllerProxy,
    moniker: String,
    writer: &mut W,
) -> Result<()> {
    let moniker = PartialAbsoluteMoniker::parse_string_without_instances(&moniker)
        .map_err(|e| ffx_error!("Moniker could not be parsed: {}", e))?;
    writeln!(writer, "Moniker: {}", moniker)?;
    writeln!(writer, "Binding to component instance...")?;

    // LifecycleController accepts PartialRelativeMonikers only
    let moniker = format!(".{}", moniker.to_string_without_instances());
    match lifecycle_controller.bind(&moniker).await {
        Ok(Ok(())) => Ok(()),
        Ok(Err(e)) => {
            ffx_bail!("Lifecycle protocol could not bind to the component instance: {:?}", e)
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
    ) -> fsys::LifecycleControllerProxy {
        let (lifecycle_controller, mut stream) =
            create_proxy_and_stream::<fsys::LifecycleControllerMarker>().unwrap();
        fuchsia_async::Task::local(async move {
            let req = stream.try_next().await.unwrap().unwrap();
            match req {
                fsys::LifecycleControllerRequest::Bind { moniker, responder, .. } => {
                    assert_eq!(expected_moniker, moniker);
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
        let lifecycle_controller = setup_fake_lifecycle_controller("./core/ffx-laboratory:test");
        let response =
            bind_impl(lifecycle_controller, "/core/ffx-laboratory:test".to_string(), &mut writer)
                .await;
        response.unwrap();
        Ok(())
    }
}
