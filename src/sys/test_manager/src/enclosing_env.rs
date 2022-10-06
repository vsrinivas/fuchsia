// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::resolver,
    anyhow::{Context, Error},
    fidl::prelude::*,
    fidl_fuchsia_debugdata as fdebugdata, fidl_fuchsia_io as fio, fidl_fuchsia_sys as fv1sys,
    fuchsia_async as fasync,
    fuchsia_component::client::connect_to_protocol,
    fuchsia_component::server::ServiceFs,
    fuchsia_component_test::LocalComponentHandles,
    fuchsia_zircon as zx,
    futures::{prelude::*, StreamExt},
    lazy_static::lazy_static,
    std::sync::{
        atomic::{AtomicU64, Ordering},
        Arc,
    },
    tracing::{debug, warn},
};

lazy_static! {
    static ref ENCLOSING_ENV_ID: AtomicU64 = AtomicU64::new(1);
}

/// Represents a single CFv1 environment.
/// Consumer of this protocol have no access to system services.
/// The logger provided to clients comes from isolated archivist.
/// TODO(82072): Support collection of inspect by isolated archivist.
struct EnclosingEnvironment {
    svc_task: Option<fasync::Task<()>>,
    env_controller_proxy: Option<fv1sys::EnvironmentControllerProxy>,
    env_proxy: fv1sys::EnvironmentProxy,
    service_directory: fidl::endpoints::ClientEnd<fio::DirectoryMarker>,
}

impl Drop for EnclosingEnvironment {
    fn drop(&mut self) {
        let svc_task = self.svc_task.take();
        let env_controller_proxy = self.env_controller_proxy.take();
        fasync::Task::spawn(async move {
            if let Some(svc_task) = svc_task {
                svc_task.cancel().await;
            }
            if let Some(env_controller_proxy) = env_controller_proxy {
                let _ = env_controller_proxy.kill().await;
            }
        })
        .detach();
    }
}

impl EnclosingEnvironment {
    fn new(
        incoming_svc: fio::DirectoryProxy,
        hermetic_test_package_name: Arc<String>,
        other_allowed_packages: resolver::AllowedPackages,
    ) -> Result<Arc<Self>, Error> {
        let sys_env = connect_to_protocol::<fv1sys::EnvironmentMarker>()?;
        let (additional_svc_client, additional_svc_server) = fidl::endpoints::create_endpoints()?;
        let incoming_svc = Arc::new(incoming_svc);
        let incoming_svc_clone = incoming_svc.clone();
        let mut fs = ServiceFs::new();
        let mut loader_tasks = vec![];
        let loader_service = connect_to_protocol::<fv1sys::LoaderMarker>()?;

        fs.add_fidl_service(move |stream: fv1sys::LoaderRequestStream| {
            let hermetic_test_package_name = hermetic_test_package_name.clone();
            let other_allowed_packages = other_allowed_packages.clone();
            let loader_service = loader_service.clone();
            loader_tasks.push(fasync::Task::spawn(async move {
                resolver::serve_hermetic_loader(
                    stream,
                    hermetic_test_package_name,
                    other_allowed_packages,
                    loader_service.clone(),
                )
                .await;
            }));
        });

        fs.add_service_at(
            fdebugdata::PublisherMarker::PROTOCOL_NAME,
            move |chan: fuchsia_zircon::Channel| {
                if let Err(e) = fdio::service_connect_at(
                    incoming_svc_clone.as_channel().as_ref(),
                    fdebugdata::PublisherMarker::PROTOCOL_NAME,
                    chan,
                ) {
                    warn!("cannot connect to debug data Publisher: {}", e);
                }
                None
            },
        )
        .add_service_at("fuchsia.logger.LogSink", move |chan: fuchsia_zircon::Channel| {
            if let Err(e) = fdio::service_connect_at(
                incoming_svc.as_channel().as_ref(),
                "fuchsia.logger.LogSink",
                chan,
            ) {
                warn!("cannot connect to LogSink: {}", e);
            }
            None
        });

        fs.serve_connection(additional_svc_server)?;
        let svc_task = fasync::Task::spawn(async move {
            fs.collect::<()>().await;
        });

        let mut service_list = fv1sys::ServiceList {
            names: vec![
                fv1sys::LoaderMarker::PROTOCOL_NAME.to_string(),
                fdebugdata::PublisherMarker::PROTOCOL_NAME.to_string(),
                "fuchsia.logger.LogSink".into(),
            ],
            provider: None,
            host_directory: Some(additional_svc_client),
        };

        let mut opts = fv1sys::EnvironmentOptions {
            inherit_parent_services: false,
            use_parent_runners: false,
            kill_on_oom: true,
            delete_storage_on_death: true,
        };

        let (env_proxy, env_server_end) = fidl::endpoints::create_proxy()?;
        let (service_directory, directory_request) = fidl::endpoints::create_endpoints()?;

        let (env_controller_proxy, env_controller_server_end) = fidl::endpoints::create_proxy()?;
        let name = format!("env-{}", ENCLOSING_ENV_ID.fetch_add(1, Ordering::SeqCst));
        sys_env
            .create_nested_environment(
                env_server_end,
                env_controller_server_end,
                &name,
                Some(&mut service_list),
                &mut opts,
            )
            .context("Cannot create nested env")?;
        env_proxy.get_directory(directory_request).context("cannot get env directory")?;
        Ok(Self {
            svc_task: svc_task.into(),
            env_controller_proxy: env_controller_proxy.into(),
            env_proxy,
            service_directory,
        }
        .into())
    }

    fn get_launcher(&self, launcher: fidl::endpoints::ServerEnd<fv1sys::LauncherMarker>) {
        if let Err(e) = self.env_proxy.get_launcher(launcher) {
            warn!("GetLauncher failed: {}", e);
        }
    }

    fn connect_to_protocol(&self, protocol_name: &str, chan: zx::Channel) {
        if let Err(e) =
            fdio::service_connect_at(&self.service_directory.channel(), protocol_name, chan)
        {
            warn!("service_connect_at failed for {}: {}", protocol_name, e);
        }
    }

    async fn serve(&self, mut req_stream: fv1sys::EnvironmentRequestStream) {
        while let Some(req) = req_stream
            .try_next()
            .await
            .context("serving V1 stream failed")
            .map_err(|e| {
                warn!("{}", e);
            })
            .unwrap_or(None)
        {
            match req {
                fv1sys::EnvironmentRequest::GetLauncher { launcher, control_handle } => {
                    if let Err(e) = self.env_proxy.get_launcher(launcher) {
                        warn!("GetLauncher failed: {}", e);
                        control_handle.shutdown();
                    }
                }
                fv1sys::EnvironmentRequest::GetServices { services, control_handle } => {
                    if let Err(e) = self.env_proxy.get_services(services) {
                        warn!("GetServices failed: {}", e);

                        control_handle.shutdown();
                    }
                }
                fv1sys::EnvironmentRequest::GetDirectory { directory_request, control_handle } => {
                    if let Err(e) = self.env_proxy.get_directory(directory_request) {
                        warn!("GetDirectory failed: {}", e);
                        control_handle.shutdown();
                    }
                }
                fv1sys::EnvironmentRequest::CreateNestedEnvironment {
                    environment,
                    controller,
                    label,
                    mut additional_services,
                    mut options,
                    control_handle,
                } => {
                    let services = match &mut additional_services {
                        Some(s) => s.as_mut().into(),
                        None => None,
                    };
                    if let Err(e) = self.env_proxy.create_nested_environment(
                        environment,
                        controller,
                        &label,
                        services,
                        &mut options,
                    ) {
                        warn!("CreateNestedEnvironment failed: {}", e);
                        control_handle.shutdown();
                    }
                }
            }
        }
    }
}

/// Create a new and single enclosing env for every test. Each test only gets a single enclosing env
/// no matter how many times it connects to Environment service.
pub async fn gen_enclosing_env(
    handles: LocalComponentHandles,
    hermetic_test_package_name: Arc<String>,
    other_allowed_packages: resolver::AllowedPackages,
) -> Result<(), Error> {
    // This function should only be called when test tries to connect to Environment or Launcher.
    let mut fs = ServiceFs::new();
    let incoming_svc = handles.clone_from_namespace("svc")?;
    let enclosing_env =
        EnclosingEnvironment::new(incoming_svc, hermetic_test_package_name, other_allowed_packages)
            .context("Cannot create enclosing env")?;
    let enclosing_env_clone = enclosing_env.clone();
    let enclosing_env_clone2 = enclosing_env.clone();

    fs.dir("svc")
        .add_fidl_service(move |req_stream: fv1sys::EnvironmentRequestStream| {
            debug!("Received Env connection request");
            let enclosing_env = enclosing_env.clone();
            fasync::Task::spawn(async move {
                enclosing_env.serve(req_stream).await;
            })
            .detach();
        })
        .add_service_at(
            fv1sys::LauncherMarker::PROTOCOL_NAME,
            move |chan: fuchsia_zircon::Channel| {
                enclosing_env_clone.get_launcher(chan.into());
                None
            },
        )
        .add_service_at(
            fv1sys::LoaderMarker::PROTOCOL_NAME,
            move |chan: fuchsia_zircon::Channel| {
                enclosing_env_clone2.connect_to_protocol(fv1sys::LoaderMarker::PROTOCOL_NAME, chan);
                None
            },
        );

    fs.serve_connection(handles.outgoing_dir)?;
    fs.collect::<()>().await;

    // TODO(fxbug.dev/82021): kill and clean environment
    Ok(())
}
