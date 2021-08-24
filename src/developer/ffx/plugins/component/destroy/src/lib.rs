// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Result},
    errors::ffx_error,
    ffx_component::destroy_component_instance,
    ffx_component_destroy_args::DestroyComponentCommand,
    ffx_core::ffx_plugin,
    fidl::endpoints::create_proxy,
    fidl_fuchsia_developer_remotecontrol as rc,
    fidl_fuchsia_sys2::RealmMarker,
    moniker::{AbsoluteMonikerBase, ChildMonikerBase, PartialAbsoluteMoniker},
};

static MONIKER_ERROR_HELP: &'static str = "Provide a moniker to a component instance in a \
collection. To learn more about collections, visit \
https://fuchsia.dev/fuchsia-src/concepts/components/v2/realms#collections";

#[ffx_plugin]
pub async fn destroy_component(
    rcs_proxy: rc::RemoteControlProxy,
    destroy: DestroyComponentCommand,
) -> Result<()> {
    destroy_component_cmd(rcs_proxy, destroy.moniker, &mut std::io::stdout()).await
}

async fn destroy_component_cmd<W: std::io::Write>(
    rcs_proxy: rc::RemoteControlProxy,
    moniker: String,
    writer: &mut W,
) -> Result<()> {
    let moniker = PartialAbsoluteMoniker::parse_string_without_instances(&moniker)
        .context("Parsing moniker")?;
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

    // This is a hack that takes the parent moniker and creates a selector for fuchsia.sys2.Realm
    // in the incoming namespace. The `destroy` command expects such a `use` declaration to exist
    // in the parent because the collection can only be modified if the parent uses that protocol.
    let mut parent = parent.to_string_without_instances();
    assert!(parent.starts_with("/"));
    parent.remove(0);
    let selector = format!("{}:in:fuchsia.sys2.Realm", parent);
    log::debug!("Attempting to connect to {}", selector);

    let selector =
        selectors::parse_selector(&selector).context("Parsing selector derived from moniker")?;
    let (realm_proxy, server) = create_proxy::<RealmMarker>()?;
    let server = server.into_channel();
    rcs_proxy
        .connect(selector, server)
        .await
        .context("Awaiting connect call")?
        .map_err(|e| ffx_error!("Connecting to selector: {:?}", e))?;

    writeln!(writer, "Destroying component instance: {}", name)?;

    destroy_component_instance(&realm_proxy, name.to_string(), collection.to_string()).await
}

////////////////////////////////////////////////////////////////////////////////
// tests

#[cfg(test)]
mod test {
    use {
        super::*,
        fidl::endpoints::RequestStream,
        fidl::handle::AsyncChannel,
        fidl_fuchsia_sys2::{RealmRequest, RealmRequestStream},
        futures::TryStreamExt,
        std::io::BufWriter,
    };

    fn setup_fake_realm_service(
        mut stream: RealmRequestStream,
        expected_collection: &'static str,
        expected_name: &'static str,
    ) {
        fuchsia_async::Task::local(async move {
            while let Ok(Some(req)) = stream.try_next().await {
                match req {
                    RealmRequest::DestroyChild { child, responder, .. } => {
                        assert_eq!(expected_collection, child.collection.unwrap());
                        assert_eq!(expected_name, child.name);
                        responder.send(&mut Ok(())).unwrap();
                    }
                    _ => assert!(false),
                }
                // We should only get one request per stream. We want subsequent calls to fail if more are
                // made.
                break;
            }
        })
        .detach();
    }

    fn setup_fake_remote_server(
        expected_selector: &'static str,
        expected_collection: &'static str,
        expected_name: &'static str,
    ) -> rc::RemoteControlProxy {
        setup_fake_rcs_proxy(move |req| match req {
            rc::RemoteControlRequest::Connect { selector, service_chan, responder } => {
                let selector = selectors::selector_to_string(selector).unwrap();
                assert_eq!(expected_selector, selector);

                setup_fake_realm_service(
                    RealmRequestStream::from_channel(
                        AsyncChannel::from_channel(service_chan).unwrap(),
                    ),
                    expected_collection,
                    expected_name,
                );

                let _ = responder
                    .send(&mut Ok(rc::ServiceMatch {
                        moniker: vec![String::from("core")],
                        subdir: String::from("in"),
                        service: String::from("fuchsia.sys2.Realm"),
                    }))
                    .unwrap();
            }
            _ => assert!(false, "got unexpected {:?}", req),
        })
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_success() -> Result<()> {
        let mut output = String::new();
        let mut writer = unsafe { BufWriter::new(output.as_mut_vec()) };
        let remote_proxy =
            setup_fake_remote_server("core:in:fuchsia.sys2.Realm", "ffx-laboratory", "test");
        let response = destroy_component_cmd(
            remote_proxy,
            "/core/ffx-laboratory:test".to_string(),
            &mut writer,
        )
        .await;
        response.unwrap();
        Ok(())
    }
}
