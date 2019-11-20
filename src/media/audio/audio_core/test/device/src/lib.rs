// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod gain;
mod virtual_devices;

mod prelude {
    pub use fidl;
    pub use fidl_fuchsia_virtualaudio::*;
    pub use fuchsia_async as fasync;
    pub type Result<T> = std::result::Result<T, failure::Error>;
    pub use crate::Environment;
    pub use fidl_fuchsia_media::*;
    pub use fuchsia_component::client;
    pub use fuchsia_zircon as zx;
    pub use futures::{self, future, FutureExt, SinkExt, StreamExt, TryStreamExt};
    pub use test_util::assert_matches;
    pub use zx::AsHandleRef;
}

use fidl::endpoints::{create_endpoints, DiscoverableService};
use fidl_fuchsia_io::DirectoryMarker;
use fidl_fuchsia_sys::LauncherProxy;
use fuchsia_component::client::App;
use fuchsia_component::server::*;
use lazy_static::lazy_static;
use maplit::hashmap;
use prelude::*;
use std::collections::HashMap;

type ServiceName = &'static str;
type ComponentUrl = &'static str;

struct ComponentLaunchInfo {
    services: Vec<ServiceName>,
    arguments: Option<Vec<String>>,
}

lazy_static! {
    static ref SERVICES: HashMap<ComponentUrl, ComponentLaunchInfo> = hashmap! {
        "fuchsia-pkg://fuchsia.com/audio_core#meta/audio_core_nodevfs.cmx" => ComponentLaunchInfo {
            services: vec![
                AudioCoreMarker::SERVICE_NAME,
                UsageReporterMarker::SERVICE_NAME,
                AudioDeviceEnumeratorMarker::SERVICE_NAME
            ],
            arguments: Some(vec![
                "--disable-device-settings-writeback".to_string()
            ])
        },
        "fuchsia-pkg://fuchsia.com/virtual_audio_service#meta/virtual_audio_service_nodevfs.cmx" => ComponentLaunchInfo {
            services: vec![
                InputMarker::SERVICE_NAME,
                OutputMarker::SERVICE_NAME,
            ],
            arguments: None
        },
    };
}

#[derive(Debug)]
struct ConnectRequest {
    service: ServiceName,
    component_url: ComponentUrl,
    channel: zx::Channel,
}

/// Registers all the statically enumerated services for the hermetic environment in the service
/// directory.
///
/// The service directory is then a stream of `ConnectRequest`s tagged with the appropriate service
/// and provider component.
fn register_services<'a>(fs: &mut ServiceFs<ServiceObj<'a, ConnectRequest>>) {
    for (component_url, info) in SERVICES.iter() {
        for service in info.services.iter().copied() {
            fs.add_service_at(service, move |channel| {
                Some(ConnectRequest { service, component_url, channel })
            });
        }
    }
}

/// Launches all the statically enumerated components for the hermetic environment.
///
/// Returns a map of component url to the app handle for the child component, which can be used to
/// connect to services offered by the component.
fn launch_components(launcher: &LauncherProxy) -> Result<HashMap<ComponentUrl, App>> {
    const TEST_DEV_MGR_URL: &str =
        "fuchsia-pkg://fuchsia.com/audio_test_devmgr#meta/audio_test_devmgr.cmx";
    const TEST_DEV_MGR_NAME: &str = "fuchsia.media.AudioTestDevmgr";
    let test_dev_mgr = client::AppBuilder::new(TEST_DEV_MGR_URL).spawn(launcher)?;

    let mut launched = SERVICES
        .iter()
        .map(|(url, launch_info)| {
            use zx::HandleBased;

            let test_dev_mgr_handle = {
                let (dev_enum, directory_request) = create_endpoints::<DirectoryMarker>()?;
                test_dev_mgr
                    .pass_to_named_service(TEST_DEV_MGR_NAME, directory_request.into_channel())?;
                dev_enum.into_channel().into_handle()
            };

            let builder = client::AppBuilder::new(*url)
                .add_handle_to_namespace("/dev".to_string(), test_dev_mgr_handle)
                .stdout(client::Stdio::Inherit)
                .stderr(client::Stdio::Inherit);
            let builder = if let Some(arguments) = launch_info.arguments.as_ref() {
                builder.args(arguments)
            } else {
                builder
            };

            let app = builder.spawn(launcher)?;

            Ok((*url, app))
        })
        .collect::<Result<HashMap<ComponentUrl, App>>>()?;

    launched.insert(TEST_DEV_MGR_URL, test_dev_mgr);
    Ok(launched)
}

pub struct Environment {
    env: NestedEnvironment,
}

impl Environment {
    pub fn new() -> Result<Self> {
        use fidl_fuchsia_logger::LogSinkMarker;

        let mut fs = ServiceFs::new();
        register_services(&mut fs);
        fs.add_proxy_service::<LogSinkMarker, ConnectRequest>();

        let env = fs.create_salted_nested_environment("environment")?;
        let launched_components = launch_components(env.launcher())?;

        fasync::spawn(fs.for_each_concurrent(None, move |request| {
            match launched_components.get(request.component_url) {
                Some(component) => {
                    component.pass_to_named_service(request.service, request.channel).expect(
                        &format!(
                            "Component {} does not serve {}",
                            request.component_url, request.service
                        ),
                    );
                }
                None => panic!("Unknown component: {:?}", request.component_url),
            }
            future::ready(())
        }));

        Ok(Self { env })
    }

    pub fn connect_to_service<S: DiscoverableService>(&self) -> Result<S::Proxy> {
        self.env.connect_to_service::<S>()
    }
}
