// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        fuchsia_boot_resolver::{self, FuchsiaBootResolver},
        fuchsia_pkg_resolver::{self, FuchsiaPkgResolver},
        model::{error::ModelError, hub::Hub, ModelParams, ResolverRegistry},
        process_launcher::ProcessLauncherService,
    },
    failure::{format_err, Error, ResultExt},
    fidl::endpoints::{ServerEnd, ServiceMarker},
    fidl_fuchsia_io::{NodeMarker, OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE},
    fidl_fuchsia_pkg::{PackageResolverMarker, PackageResolverProxy},
    fuchsia_async as fasync,
    fuchsia_component::{client, server::ServiceFs},
    fuchsia_runtime::HandleType,
    fuchsia_vfs_pseudo_fs::directory::{self, entry::DirectoryEntry},
    fuchsia_zircon as zx,
    futures::prelude::*,
    std::{iter, path::PathBuf, sync::Arc},
};

/// Command line arguments that control component_manager's behavior. Use [Arguments::from_args()]
/// or [Arguments::new()] to create an instance.
// structopt would be nice to use here but the binary size impact from clap - which it depends on -
// is too large.
#[derive(Debug, Default, Clone, Eq, PartialEq)]
pub struct Arguments {
    /// If true, component_manager will serve an instance of fuchsia.process.Launcher and use this
    /// launcher for the built-in ELF component runner. The root component can additionally
    /// use and/or offer this service using '/builtin/fuchsia.process.Launcher' from realm.
    pub use_builtin_process_launcher: bool,

    /// URL of the root component to launch.
    pub root_component_url: String,
}

impl Arguments {
    /// Parse `Arguments` from the given String Iterator.
    ///
    /// This parser is relatively simple since component_manager is not a user-facing binary that
    /// requires or would benefit from more flexible UX. Recognized arguments are extracted from
    /// the given Iterator and used to create the returned struct. Unrecognized flags starting with
    /// "--" result in an error being returned. A single non-flag argument is expected for the root
    /// component URL.
    pub fn new<I>(iter: I) -> Result<Self, Error>
    where
        I: IntoIterator<Item = String>,
    {
        let mut iter = iter.into_iter();
        let mut args = Self::default();
        while let Some(arg) = iter.next() {
            if arg == "--use-builtin-process-launcher" {
                args.use_builtin_process_launcher = true;
            } else if arg.starts_with("--") {
                return Err(format_err!("Unrecognized flag: {}", arg));
            } else {
                if !args.root_component_url.is_empty() {
                    return Err(format_err!("Multiple non-flag arguments given"));
                }
                args.root_component_url = arg;
            }
        }

        if args.root_component_url.is_empty() {
            return Err(format_err!("No root component URL found"));
        }
        Ok(args)
    }

    /// Parse `Arguments` from [std::env::args()].
    ///
    /// See [Arguments::new()] for more details.
    pub fn from_args() -> Result<Self, Error> {
        // Ignore first argument with executable name, then delegate to generic iterator impl.
        Self::new(std::env::args().skip(1))
    }

    /// Returns a usage message for the supported arguments.
    pub fn usage() -> String {
        format!(
            "Usage: {} [options] <root-component-url>\n\
             Options:\n\
             --use-builtin-process-launcher   Provide and use a built-in implementation of\n\
             fuchsia.process.Launcher",
            std::env::args().next().unwrap_or("component_manager".to_string())
        )
    }
}

/// Returns a ResolverRegistry configured with the component resolvers available to the current
/// process.
pub fn available_resolvers() -> Result<ResolverRegistry, Error> {
    let mut resolver_registry = ResolverRegistry::new();
    resolver_registry
        .register(fuchsia_boot_resolver::SCHEME.to_string(), Box::new(FuchsiaBootResolver::new()));

    // Add the fuchsia-pkg resolver to the registry if it's available.
    if let Some(pkg_resolver) = connect_pkg_resolver()? {
        resolver_registry.register(
            fuchsia_pkg_resolver::SCHEME.to_string(),
            Box::new(FuchsiaPkgResolver::new(pkg_resolver)),
        );
    }

    Ok(resolver_registry)
}

/// Checks if a package resolver service is available through our namespace and connects to it if
/// so. If not availble, returns Ok(None).
fn connect_pkg_resolver() -> Result<Option<PackageResolverProxy>, Error> {
    let service_path = PathBuf::from(format!("/svc/{}", PackageResolverMarker::NAME));
    if !service_path.exists() {
        return Ok(None);
    }

    let pkg_resolver = client::connect_to_service::<PackageResolverMarker>()
        .context("error connecting to package resolver")?;
    return Ok(Some(pkg_resolver));
}

/// Installs a Hub if possible.
pub fn install_hub_if_possible(model_params: &mut ModelParams) -> Result<(), ModelError> {
    if let Some(out_dir_handle) =
        fuchsia_runtime::take_startup_handle(HandleType::DirectoryRequest.into())
    {
        let mut root_directory = directory::simple::empty();
        root_directory.open(
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            0,
            &mut iter::empty(),
            ServerEnd::<NodeMarker>::new(out_dir_handle.into()),
        );
        model_params
            .hooks
            .push(Arc::new(Hub::new(model_params.root_component_url.clone(), root_directory)?));
    };
    Ok(())
}

/// Serves services built into component_manager and provides methods for connecting to those
/// services.
///
/// The available built-in services depends on the configuration provided in Arguments:
///
/// * If [Arguments::use_builtin_process_launcher] is true, a fuchsia.process.Launcher service is
///   available.
pub struct BuiltinRootServices {
    services: zx::Channel,
}

impl BuiltinRootServices {
    pub fn new(args: &Arguments) -> Result<Self, Error> {
        let services = Self::serve(args)?;
        Ok(Self { services })
    }

    /// Connect to a built-in FIDL service.
    pub fn connect_to_service<S: ServiceMarker>(&self) -> Result<S::Proxy, Error> {
        let (proxy, server) =
            fidl::endpoints::create_proxy::<S>().context("Failed to create proxy")?;
        fdio::service_connect_at(&self.services, S::NAME, server.into_channel())
            .context("Failed to connect built-in service")?;
        Ok(proxy)
    }

    fn serve(args: &Arguments) -> Result<zx::Channel, Error> {
        let (client, server) = zx::Channel::create().context("Failed to create channel")?;
        let mut fs = ServiceFs::new();

        if args.use_builtin_process_launcher {
            fs.add_fidl_service(move |stream| {
                fasync::spawn(
                    ProcessLauncherService::serve(stream)
                        .unwrap_or_else(|e| panic!("Error while serving process launcher: {}", e)),
                )
            });
        }

        fs.serve_connection(server).context("Failed to serve builtin services")?;
        fasync::spawn(fs.collect::<()>());
        Ok(client)
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl_fidl_examples_echo::EchoMarker,
        fidl_fuchsia_process::{LaunchInfo, LauncherMarker},
    };

    #[fasync::run_singlethreaded(test)]
    async fn parse_arguments() -> Result<(), Error> {
        let dummy_url = || "fuchsia-pkg://fuchsia.com/pkg#meta/component.cm".to_string();
        let dummy_url2 = || "fuchsia-pkg://fuchsia.com/pkg#meta/component2.cm".to_string();
        let unknown_flag = || "--unknown".to_string();
        let use_builtin_launcher = || "--use-builtin-process-launcher".to_string();

        // Zero or multiple positional arguments is an error; must be exactly one URL.
        assert!(Arguments::new(vec![]).is_err());
        assert!(Arguments::new(vec![use_builtin_launcher()]).is_err());
        assert!(Arguments::new(vec![dummy_url(), dummy_url2()]).is_err());
        assert!(Arguments::new(vec![dummy_url(), use_builtin_launcher(), dummy_url2()]).is_err());

        // An unknown option is an error.
        assert!(Arguments::new(vec![unknown_flag()]).is_err());
        assert!(Arguments::new(vec![unknown_flag(), dummy_url()]).is_err());
        assert!(Arguments::new(vec![dummy_url(), unknown_flag()]).is_err());

        // Single positional argument with no options is parsed correctly
        assert_eq!(
            Arguments::new(vec![dummy_url()]).expect("Unexpected error with just URL"),
            Arguments { root_component_url: dummy_url(), ..Default::default() }
        );
        assert_eq!(
            Arguments::new(vec![dummy_url2()]).expect("Unexpected error with just URL"),
            Arguments { root_component_url: dummy_url2(), ..Default::default() }
        );

        // Options are parsed correctly and do not depend on order.
        assert_eq!(
            Arguments::new(vec![use_builtin_launcher(), dummy_url()])
                .expect("Unexpected error with option"),
            Arguments { use_builtin_process_launcher: true, root_component_url: dummy_url() }
        );
        assert_eq!(
            Arguments::new(vec![dummy_url(), use_builtin_launcher()])
                .expect("Unexpected error with option"),
            Arguments { use_builtin_process_launcher: true, root_component_url: dummy_url() }
        );

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn connect_to_builtin_service() -> Result<(), Error> {
        // The built-in process launcher won't work properly in all environments where we run this
        // test due to zx_process_create being disallowed, but this test doesn't actually need to
        // launch a process, just check that the launcher is served and we can connect to and
        // communicate with it.
        let args = Arguments { use_builtin_process_launcher: true, ..Default::default() };
        let builtin = BuiltinRootServices::new(&args)?;

        // Try to launch a process with an invalid process name. This will cause a predictable
        // failure from the launcher, confirming that we can communicate with it.
        let launcher = builtin.connect_to_service::<LauncherMarker>()?;
        let job = fuchsia_runtime::job_default().duplicate(zx::Rights::SAME_RIGHTS)?;
        let mut launch_info =
            LaunchInfo { name: "ab\0cd".into(), executable: zx::Vmo::create(1)?, job };
        let (status, process) =
            await!(launcher.launch(&mut launch_info)).expect("FIDL call to launcher failed");
        assert_eq!(zx::Status::from_raw(status), zx::Status::INVALID_ARGS);
        assert_eq!(process, None);
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn connect_to_nonexistent_builtin_service() -> Result<(), Error> {
        let args = Arguments { use_builtin_process_launcher: false, ..Default::default() };
        let builtin = BuiltinRootServices::new(&args)?;

        // connect_to_service should succeed; it doesn't check that the service actually exists.
        let proxy = builtin.connect_to_service::<EchoMarker>()?;

        // But calls to the service should fail, since it doesn't exist.
        let res = await!(proxy.echo_string(Some("hippos")));
        let err = res.expect_err("echo_string unexpected succeeded");
        match err {
            fidl::Error::ClientRead(zx::Status::PEER_CLOSED)
            | fidl::Error::ClientWrite(zx::Status::PEER_CLOSED) => {}
            _ => panic!("Unexpected error: {:?}", err),
        };
        Ok(())
    }
}
