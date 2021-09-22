// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    errors::{ffx_bail, ffx_error},
    ffx_component::connect_to_lifecycle_controller,
    ffx_component_destroy_args::DestroyComponentCommand,
    ffx_core::ffx_plugin,
    fidl_fuchsia_component as fcomponent, fidl_fuchsia_developer_remotecontrol as rc,
    fidl_fuchsia_sys2 as fsys,
    moniker::{AbsoluteMonikerBase, ChildMonikerBase, PartialAbsoluteMoniker},
};

static MONIKER_ERROR_HELP: &'static str = "Provide a moniker to a component instance in a \
collection. To learn more about collections, visit \
https://fuchsia.dev/fuchsia-src/concepts/components/v2/realms#collections";

#[ffx_plugin]
pub async fn destroy(
    rcs_proxy: rc::RemoteControlProxy,
    cmd: DestroyComponentCommand,
) -> Result<()> {
    let lifecycle_controller = connect_to_lifecycle_controller(&rcs_proxy).await?;
    destroy_impl(lifecycle_controller, cmd.moniker, &mut std::io::stdout()).await
}

async fn destroy_impl<W: std::io::Write>(
    lifecycle_controller: fsys::LifecycleControllerProxy,
    moniker: String,
    writer: &mut W,
) -> Result<()> {
    let moniker = PartialAbsoluteMoniker::parse_string_without_instances(&moniker)
        .map_err(|e| ffx_error!("Moniker could not be parsed: {}", e))?;
    let parent = moniker
        .parent()
        .ok_or(ffx_error!("Component moniker cannot be the root. {}", MONIKER_ERROR_HELP))?;
    let leaf = moniker
        .leaf()
        .ok_or(ffx_error!("Component moniker cannot be the root. {}", MONIKER_ERROR_HELP))?;
    let collection = leaf
        .collection()
        .ok_or(ffx_error!("Moniker references a static component. {}", MONIKER_ERROR_HELP))?;
    let name = leaf.name();

    writeln!(writer, "Moniker: {}", moniker)?;
    writeln!(writer, "Destroying component instance...")?;

    let mut child =
        fsys::ChildRef { name: name.to_string(), collection: Some(collection.to_string()) };

    // LifecycleController accepts PartialRelativeMonikers only
    let parent_moniker = format!(".{}", parent.to_string_without_instances());

    let result = lifecycle_controller
        .destroy_child(&parent_moniker, &mut child)
        .await
        .map_err(|e| ffx_error!("FIDL error while destroying component instance: {:?}", e))?;

    match result {
        Err(fcomponent::Error::InstanceNotFound) => {
            ffx_bail!("Component instance was not found. Component instances can be created with the `ffx component create` or `ffx component run` commands.")
        }
        Err(e) => {
            ffx_bail!("Lifecycle protocol could not destroy component instance: {:?}", e);
        }
        Ok(()) => Ok(()),
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
        expected_collection: &'static str,
        expected_name: &'static str,
    ) -> fsys::LifecycleControllerProxy {
        let (lifecycle_controller, mut stream) =
            create_proxy_and_stream::<fsys::LifecycleControllerMarker>().unwrap();
        fuchsia_async::Task::local(async move {
            let req = stream.try_next().await.unwrap().unwrap();
            match req {
                fsys::LifecycleControllerRequest::DestroyChild {
                    parent_moniker,
                    child,
                    responder,
                    ..
                } => {
                    assert_eq!(expected_moniker, parent_moniker);
                    assert_eq!(expected_collection, child.collection.unwrap());
                    assert_eq!(expected_name, child.name);
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
            setup_fake_lifecycle_controller("./core", "ffx-laboratory", "test");
        let response = destroy_impl(
            lifecycle_controller,
            "/core/ffx-laboratory:test".to_string(),
            &mut writer,
        )
        .await;
        response.unwrap();
        Ok(())
    }
}
