// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    errors::{ffx_bail, ffx_error},
    ffx_component::{connect_to_lifecycle_controller, verify_fuchsia_pkg_cm_url},
    ffx_component_run_args::RunComponentCommand,
    ffx_core::ffx_plugin,
    fidl_fuchsia_component as fcomponent, fidl_fuchsia_component_decl as fdecl,
    fidl_fuchsia_developer_remotecontrol as rc, fidl_fuchsia_sys2 as fsys,
    moniker::{AbsoluteMoniker, AbsoluteMonikerBase},
};

const COLLECTION_NAME: &'static str = "ffx-laboratory";

#[ffx_plugin]
pub async fn run(rcs_proxy: rc::RemoteControlProxy, cmd: RunComponentCommand) -> Result<()> {
    let lifecycle_controller = connect_to_lifecycle_controller(&rcs_proxy).await?;
    run_impl(lifecycle_controller, cmd.url, cmd.name, cmd.recreate, &mut std::io::stdout()).await
}

async fn run_impl<W: std::io::Write>(
    lifecycle_controller: fsys::LifecycleControllerProxy,
    url: String,
    name: Option<String>,
    recreate: bool,
    writer: &mut W,
) -> Result<()> {
    let manifest_name = verify_fuchsia_pkg_cm_url(url.as_str())?;

    let name = if let Some(name) = name {
        // Use a custom name provided in the command line
        name
    } else {
        // Attempt to use the manifest name as the instance name
        manifest_name
    };

    let moniker = format!("/core/{}:{}", COLLECTION_NAME, name);
    let moniker = AbsoluteMoniker::parse_str(&moniker)
        .map_err(|e| ffx_error!("Moniker could not be parsed: {}", e))?;

    writeln!(writer, "URL: {}", url)?;
    writeln!(writer, "Moniker: {}", moniker)?;
    writeln!(writer, "Creating component instance...")?;
    let mut collection = fdecl::CollectionRef { name: COLLECTION_NAME.to_string() };
    let decl = fdecl::Child {
        name: Some(name.clone()),
        url: Some(url.clone()),
        startup: Some(fdecl::StartupMode::Lazy),
        environment: None,
        ..fdecl::Child::EMPTY
    };
    let create_result = lifecycle_controller
        .create_child("./core", &mut collection, decl.clone(), fcomponent::CreateChildArgs::EMPTY)
        .await
        .map_err(|e| ffx_error!("FIDL error while creating component instance: {:?}", e))?;

    match create_result {
        Err(fcomponent::Error::InstanceAlreadyExists) => {
            if recreate {
                // This component already exists, but the user has asked it to be recreated.
                let mut child =
                    fdecl::ChildRef { name, collection: Some(COLLECTION_NAME.to_string()) };

                writeln!(writer, "Component instance already exists. Destroying...")?;
                let destroy_result =
                    lifecycle_controller.destroy_child("./core", &mut child).await.map_err(
                        |e| ffx_error!("FIDL error while destroying component instance: {:?}", e),
                    )?;

                if let Err(e) = destroy_result {
                    ffx_bail!("Lifecycle protocol could not destroy component instance: {:?}", e);
                }

                writeln!(writer, "Recreating component instance...")?;
                let create_result = lifecycle_controller
                    .create_child(
                        "./core",
                        &mut collection,
                        decl.clone(),
                        fcomponent::CreateChildArgs::EMPTY,
                    )
                    .await
                    .map_err(|e| {
                        ffx_error!("FIDL error while creating component instance: {:?}", e)
                    })?;

                if let Err(e) = create_result {
                    ffx_bail!("Lifecycle protocol could not recreate component instance: {:?}", e);
                }
            } else {
                ffx_bail!("Component instance already exists. Use --recreate to destroy and recreate a new instance, or --name to create a new instance with a different name.")
            }
        }
        Err(e) => {
            ffx_bail!("Lifecycle protocol could not create component instance: {:?}", e);
        }
        Ok(()) => {}
    }

    writeln!(writer, "Starting component instance...")?;

    // LifecycleController accepts RelativeMonikers only
    let moniker = format!(".{}", moniker.to_string());

    let start_result = lifecycle_controller
        .start(&moniker)
        .await
        .map_err(|e| ffx_error!("FIDL error while starting the component instance: {}", e))?;

    if let Err(e) = start_result {
        ffx_bail!("Lifecycle protocol could not start the component instance: {:?}", e);
    }

    Ok(())
}

////////////////////////////////////////////////////////////////////////////////
// tests

#[cfg(test)]
mod test {
    use {super::*, fidl::endpoints::create_proxy_and_stream, futures::TryStreamExt};

    fn setup_fake_lifecycle_controller_ok(
        expected_parent_moniker: &'static str,
        expected_collection: &'static str,
        expected_name: &'static str,
        expected_url: &'static str,
        expected_moniker: &'static str,
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
                    assert_eq!(expected_parent_moniker, parent_moniker);
                    assert_eq!(expected_collection, collection.name);
                    assert_eq!(expected_name, decl.name.unwrap());
                    assert_eq!(expected_url, decl.url.unwrap());
                    responder.send(&mut Ok(())).unwrap();
                }
                _ => panic!("Unexpected Lifecycle Controller request"),
            }

            let req = stream.try_next().await.unwrap().unwrap();
            match req {
                fsys::LifecycleControllerRequest::Start { moniker, responder, .. } => {
                    assert_eq!(expected_moniker, moniker);
                    responder.send(&mut Ok(fsys::StartResult::Started)).unwrap();
                }
                _ => panic!("Unexpected Lifecycle Controller request"),
            }
        })
        .detach();
        lifecycle_controller
    }

    fn setup_fake_lifecycle_controller_fail(
        expected_parent_moniker: &'static str,
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
                    assert_eq!(expected_parent_moniker, parent_moniker);
                    assert_eq!(expected_collection, collection.name);
                    assert_eq!(expected_name, decl.name.unwrap());
                    assert_eq!(expected_url, decl.url.unwrap());
                    responder.send(&mut Err(fcomponent::Error::InstanceAlreadyExists)).unwrap();
                }
                _ => panic!("Unexpected Lifecycle Controller request"),
            }
        })
        .detach();
        lifecycle_controller
    }

    fn setup_fake_lifecycle_controller_recreate(
        expected_parent_moniker: &'static str,
        expected_collection: &'static str,
        expected_name: &'static str,
        expected_url: &'static str,
        expected_moniker: &'static str,
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
                    assert_eq!(expected_parent_moniker, parent_moniker);
                    assert_eq!(expected_collection, collection.name);
                    assert_eq!(expected_name, decl.name.unwrap());
                    assert_eq!(expected_url, decl.url.unwrap());
                    responder.send(&mut Err(fcomponent::Error::InstanceAlreadyExists)).unwrap();
                }
                _ => panic!("Unexpected Lifecycle Controller request"),
            }

            let req = stream.try_next().await.unwrap().unwrap();
            match req {
                fsys::LifecycleControllerRequest::DestroyChild {
                    parent_moniker,
                    child,
                    responder,
                    ..
                } => {
                    assert_eq!(expected_parent_moniker, parent_moniker);
                    assert_eq!(expected_name, child.name);
                    assert_eq!(expected_collection, child.collection.unwrap());
                    responder.send(&mut Ok(())).unwrap();
                }
                _ => panic!("Unexpected Lifecycle Controller request"),
            }

            let req = stream.try_next().await.unwrap().unwrap();
            match req {
                fsys::LifecycleControllerRequest::CreateChild {
                    parent_moniker,
                    collection,
                    decl,
                    responder,
                    ..
                } => {
                    assert_eq!(expected_parent_moniker, parent_moniker);
                    assert_eq!(expected_collection, collection.name);
                    assert_eq!(expected_name, decl.name.unwrap());
                    assert_eq!(expected_url, decl.url.unwrap());
                    responder.send(&mut Ok(())).unwrap();
                }
                _ => panic!("Unexpected Lifecycle Controller request"),
            }

            let req = stream.try_next().await.unwrap().unwrap();
            match req {
                fsys::LifecycleControllerRequest::Start { moniker, responder, .. } => {
                    assert_eq!(expected_moniker, moniker);
                    responder.send(&mut Ok(fsys::StartResult::Started)).unwrap();
                }
                _ => panic!("Unexpected Lifecycle Controller request"),
            }
        })
        .detach();
        lifecycle_controller
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_ok() -> Result<()> {
        let mut output = Vec::new();
        let lifecycle_controller = setup_fake_lifecycle_controller_ok(
            "./core",
            "ffx-laboratory",
            "test",
            "fuchsia-pkg://fuchsia.com/test#meta/test.cm",
            "./core/ffx-laboratory:test",
        );
        let response = run_impl(
            lifecycle_controller,
            "fuchsia-pkg://fuchsia.com/test#meta/test.cm".to_string(),
            None,
            false,
            &mut output,
        )
        .await;
        response.unwrap();
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_name() -> Result<()> {
        let mut output = Vec::new();
        let lifecycle_controller = setup_fake_lifecycle_controller_ok(
            "./core",
            "ffx-laboratory",
            "foobar",
            "fuchsia-pkg://fuchsia.com/test#meta/test.cm",
            "./core/ffx-laboratory:foobar",
        );
        let response = run_impl(
            lifecycle_controller,
            "fuchsia-pkg://fuchsia.com/test#meta/test.cm".to_string(),
            Some("foobar".to_string()),
            false,
            &mut output,
        )
        .await;
        response.unwrap();
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_fail() -> Result<()> {
        let mut output = Vec::new();
        let lifecycle_controller = setup_fake_lifecycle_controller_fail(
            "./core",
            "ffx-laboratory",
            "test",
            "fuchsia-pkg://fuchsia.com/test#meta/test.cm",
        );
        let response = run_impl(
            lifecycle_controller,
            "fuchsia-pkg://fuchsia.com/test#meta/test.cm".to_string(),
            None,
            false,
            &mut output,
        )
        .await;
        response.unwrap_err();
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_recreate() -> Result<()> {
        let mut output = Vec::new();
        let lifecycle_controller = setup_fake_lifecycle_controller_recreate(
            "./core",
            "ffx-laboratory",
            "test",
            "fuchsia-pkg://fuchsia.com/test#meta/test.cm",
            "./core/ffx-laboratory:test",
        );
        let response = run_impl(
            lifecycle_controller,
            "fuchsia-pkg://fuchsia.com/test#meta/test.cm".to_string(),
            None,
            true,
            &mut output,
        )
        .await;
        response.unwrap();
        Ok(())
    }
}
