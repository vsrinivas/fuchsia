// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    errors::{ffx_bail, ffx_error},
    ffx_component::{connect_to_lifecycle_controller, verify_fuchsia_pkg_cm_url},
    ffx_component_run_args::RunComponentCommand,
    ffx_core::ffx_plugin,
    ffx_log::{log_impl, LogOpts},
    ffx_log_args::LogCommand,
    fidl_fuchsia_developer_ffx::DiagnosticsProxy,
    fidl_fuchsia_developer_remotecontrol as rc, fidl_fuchsia_sys2 as fsys,
    moniker::{AbsoluteMoniker, AbsoluteMonikerBase},
};

const DEFAULT_COLLECTION: &'static str = "/core/ffx-laboratory";

fn get_deprecated_component_name(url: &String, name: &Option<String>) -> Result<String> {
    let manifest_name = verify_fuchsia_pkg_cm_url(url.as_str())?;

    let name = if let Some(name) = name {
        // Use a custom name provided in the command line
        name.clone()
    } else {
        // Attempt to use the manifest name as the instance name
        manifest_name
    };

    Ok(name)
}

fn get_url_moniker_from_args(cmd: &RunComponentCommand) -> Result<(&String, AbsoluteMoniker)> {
    // Check if the second positional arg (url) is None. If so, the user is
    // executing the command with the deprecated form:
    //   $ ffx component run <url>
    // instead of:
    //   $ ffx component run <moniker> <url>
    let (url, moniker) = match &cmd.url {
        Some(url) => {
            // Check if `--name` was supplied. If so, it'll be ignored.
            if cmd.name.is_some() {
                eprintln!("NOTE: --name is ignored when a moniker is specified.");
            }
            (url, cmd.moniker.clone())
        }
        None => {
            let url = &cmd.moniker;
            let moniker = format!(
                "{}:{}",
                DEFAULT_COLLECTION,
                get_deprecated_component_name(&cmd.moniker, &cmd.name)?
            );
            eprintln!(
                "WARNING: No component moniker specified. Using value '{}'.
The moniker arg will be required in the future. See fxbug.dev/104212",
                moniker
            );
            (url, moniker)
        }
    };
    let moniker = AbsoluteMoniker::parse_str(&moniker)
        .map_err(|e| ffx_error!("Moniker could not be parsed: {}", e))?;
    Ok((url, moniker))
}

#[ffx_plugin(DiagnosticsProxy = "daemon::protocol")]
pub async fn run(
    diagnostics_proxy: DiagnosticsProxy,
    rcs_proxy: rc::RemoteControlProxy,
    cmd: RunComponentCommand,
) -> Result<()> {
    let (url, moniker) = get_url_moniker_from_args(&cmd)?;

    let log_filter = moniker.to_string().strip_prefix("/").unwrap().to_string();
    // TODO(fxb/100844): Intgrate with Started event to get a start time for the logs.
    let log_cmd = LogCommand { filter: vec![log_filter], ..LogCommand::default() };

    let lifecycle_controller = connect_to_lifecycle_controller(&rcs_proxy).await?;

    run_impl(lifecycle_controller, &url, moniker, cmd.recreate, &mut std::io::stdout()).await?;

    if cmd.follow_logs {
        log_impl(
            diagnostics_proxy,
            Some(rcs_proxy),
            &None,
            log_cmd,
            &mut std::io::stdout(),
            LogOpts::default(),
        )
        .await?;
    }

    Ok(())
}

async fn run_impl<W: std::io::Write>(
    lifecycle_controller: fsys::LifecycleControllerProxy,
    url: &String,
    moniker: AbsoluteMoniker,
    recreate: bool,
    writer: &mut W,
) -> Result<()> {
    let if_exists = if recreate {
        ffx_component_create_lib::IfExists::Recreate
    } else {
        ffx_component_create_lib::IfExists::Error("Use --recreate to destroy and recreate a new instance, or --name to create a new instance with a different name.".to_string())
    };
    ffx_component_create_lib::create_component(
        &lifecycle_controller,
        &moniker,
        url,
        if_exists,
        writer,
    )
    .await?;

    writeln!(writer, "Starting component instance...")?;

    // LifecycleController accepts RelativeMonikers only
    let relative_moniker = format!(".{}", moniker.to_string());

    let start_result = lifecycle_controller
        .start(&relative_moniker)
        .await
        .map_err(|e| ffx_error!("FIDL error while starting the component instance: {}", e))?;

    match start_result {
        Ok(fsys::StartResult::Started) => {
            writeln!(writer, "Success! The component instance has been started.")?;
        }
        Ok(fsys::StartResult::AlreadyStarted) => {
            writeln!(writer, "The component instance was already started.")?;
        }
        Err(e) => {
            ffx_bail!(
                "Lifecycle protocol could not start the component instance: {:?}.\n{}",
                e,
                ffx_component_create_lib::LIFECYCLE_ERROR_HELP
            );
        }
    }

    Ok(())
}

////////////////////////////////////////////////////////////////////////////////
// tests

#[cfg(test)]
mod test {
    use {
        crate::get_deprecated_component_name,
        crate::get_url_moniker_from_args,
        crate::run_impl,
        anyhow::Result,
        ffx_component_run_args::RunComponentCommand,
        fidl::endpoints::create_proxy_and_stream,
        fidl_fuchsia_component as fcomponent, fidl_fuchsia_sys2 as fsys,
        futures::TryStreamExt,
        moniker::{AbsoluteMoniker, AbsoluteMonikerBase},
    };

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
    async fn test_get_url_moniker_from_args() -> Result<()> {
        let url: String = "fuchsia-pkg://fuchsia.com/test#meta/test.cm".to_string();
        let name = get_deprecated_component_name(&url, &None).unwrap();
        // Deprecated args, uses `name`.
        assert_eq!(
            get_url_moniker_from_args(&RunComponentCommand {
                moniker: url.clone(),
                url: None,
                name: Some(name.clone()),
                recreate: true,
                follow_logs: false,
            })?,
            (&url, AbsoluteMoniker::parse_str(&format!("/core/ffx-laboratory:{}", name))?)
        );
        // Modern usage. Ignores `name` and `url`.
        assert_eq!(
            get_url_moniker_from_args(&RunComponentCommand {
                moniker: "/some/place:foo".to_string(),
                url: Some(url.clone()),
                name: Some("bar".to_string()),
                recreate: true,
                follow_logs: false,
            })?,
            (&url, AbsoluteMoniker::parse_str("/some/place:foo")?)
        );
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_ok() -> Result<()> {
        let mut output = Vec::new();
        let lifecycle_controller = setup_fake_lifecycle_controller_ok(
            "./some",
            "collection",
            "name",
            "fuchsia-pkg://fuchsia.com/test#meta/test.cm",
            "./some/collection:name",
        );
        let moniker = AbsoluteMoniker::parse_str("/some/collection:name")?;
        let response = run_impl(
            lifecycle_controller,
            &"fuchsia-pkg://fuchsia.com/test#meta/test.cm".to_string(),
            moniker,
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
        let moniker = AbsoluteMoniker::parse_str("/core/ffx-laboratory:foobar")?;
        let response = run_impl(
            lifecycle_controller,
            &"fuchsia-pkg://fuchsia.com/test#meta/test.cm".to_string(),
            moniker,
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
        let moniker = AbsoluteMoniker::parse_str("/core/ffx-laboratory:test")?;
        let response = run_impl(
            lifecycle_controller,
            &"fuchsia-pkg://fuchsia.com/test#meta/test.cm".to_string(),
            moniker,
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
        let moniker = AbsoluteMoniker::parse_str("/core/ffx-laboratory:test")?;
        let response = run_impl(
            lifecycle_controller,
            &"fuchsia-pkg://fuchsia.com/test#meta/test.cm".to_string(),
            moniker,
            true,
            &mut output,
        )
        .await;
        response.unwrap();
        Ok(())
    }
}
