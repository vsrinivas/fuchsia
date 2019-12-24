// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    fdio::{watch_directory, WatchEvent},
    fidl_fuchsia_netemul_environment::{ManagedEnvironmentMarker, VirtualDevice},
    fidl_fuchsia_netemul_network::{
        DeviceProxy_Marker, EndpointBacking, EndpointConfig, EndpointManagerMarker, EndpointProxy,
        NetworkContextMarker,
    },
    fuchsia_async as fasync,
    fuchsia_component::client,
    fuchsia_zircon as zx,
    std::fs::File,
    std::path::Path,
};

const ETHERNET_VDEV_PATH: &'static str = "/vdev/class/ethernet";

fn wait_for_device_present(name: &str) -> Result<(), Error> {
    let status = watch_directory(
        &File::open(Path::new(ETHERNET_VDEV_PATH))?,
        zx::sys::ZX_TIME_INFINITE,
        |e, p| match e {
            WatchEvent::AddFile if p == Path::new(name) => Err(zx::Status::STOP),
            _ => Ok(()),
        },
    );
    if status == zx::Status::STOP {
        Ok(())
    } else {
        Err(format_err!("Failed watching for device present {}: {:?}", name, status))
    }
}

fn wait_for_device_absent(name: &str) -> Result<(), Error> {
    let status = watch_directory(
        &File::open(Path::new(ETHERNET_VDEV_PATH))?,
        zx::sys::ZX_TIME_INFINITE,
        |e, _| match e {
            WatchEvent::Idle | WatchEvent::RemoveFile => {
                if device_present(name) {
                    Ok(())
                } else {
                    Err(zx::Status::STOP)
                }
            }
            _ => Ok(()),
        },
    );
    if status == zx::Status::STOP {
        Ok(())
    } else {
        Err(format_err!("Failed watching for device absent {}: {:?}", name, status))
    }
}

fn device_present(path: &str) -> bool {
    Path::new(&format!("{}/{}", ETHERNET_VDEV_PATH, path)).exists()
}

fn check_device_absent(path: &str) -> Result<(), Error> {
    if device_present(path) {
        Err(format_err!("Device {} present, expected it to be absent", path))
    } else {
        Ok(())
    }
}

fn check_device_present(path: &str) -> Result<(), Error> {
    if device_present(path) {
        Ok(())
    } else {
        Err(format_err!("Device {} not present, expected it to be present", path))
    }
}

fn remove_device(ep_name: &str) -> Result<(), Error> {
    let env = client::connect_to_service::<ManagedEnvironmentMarker>()?;
    env.remove_device(&format!("class/ethernet/{}", ep_name))?;
    Ok(())
}

async fn attach_device(ep_name: &str) -> Result<(), Error> {
    let netctx = client::connect_to_service::<NetworkContextMarker>()?;
    // get the endpoint manager
    let (epm, epm_server_end) = fidl::endpoints::create_proxy::<EndpointManagerMarker>()?;
    netctx.get_endpoint_manager(epm_server_end)?;

    // get endpoint by name
    let ep = epm.get_endpoint(ep_name).await?;

    let ep = match ep {
        Some(ep) => Ok(ep.into_proxy()?),
        None => Err(format_err!("Can't find endpoint {}", ep_name)),
    }?;

    let (dev_proxy_client_end, dev_proxy_server_end) =
        fidl::endpoints::create_endpoints::<DeviceProxy_Marker>()?;
    ep.get_proxy_(dev_proxy_server_end)?;

    let env = client::connect_to_service::<ManagedEnvironmentMarker>()?;
    env.add_device(&mut VirtualDevice {
        path: format!("class/ethernet/{}", ep_name),
        device: dev_proxy_client_end,
    })?;

    Ok(())
}

async fn create_endpoint(name: &str) -> Result<EndpointProxy, Error> {
    let netctx = client::connect_to_service::<NetworkContextMarker>()?;
    // get the endpoint manager
    let (epm, epm_server_end) = fidl::endpoints::create_proxy::<EndpointManagerMarker>()?;
    netctx.get_endpoint_manager(epm_server_end)?;
    let mut cfg = EndpointConfig {
        backing: EndpointBacking::Ethertap,
        mac: None, // let network service create a random mac for us
        mtu: 1500,
    };
    let (_, ep) = epm.create_endpoint(name, &mut cfg).await?;
    match ep {
        Some(ep) => Ok(ep.into_proxy()?),
        None => Err(format_err!("Failed to create device")),
    }
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    // check that only pre-attached endpoint is there:
    let () = check_device_present("ep1")?;
    let () = check_device_absent("ep2")?;
    let () = check_device_absent("ep3")?;
    // attach cmx-created endpoint ep2 and check that it shows up
    let () = attach_device("ep2").await?;
    let () = wait_for_device_present("ep2")?;
    // attach cmx-created endpoint ep3 and check that it shows up
    let () = attach_device("ep3").await?;
    let () = wait_for_device_present("ep3")?;

    // create a new endpoint and attach:
    let ep4 = create_endpoint("ep4").await?;
    let () = attach_device("ep4").await?;
    let () = wait_for_device_present("ep4")?;

    // remove ep2:
    let () = remove_device("ep2")?;
    let () = wait_for_device_absent("ep2")?;
    // other ep1 and ep3 should still be there:
    let () = check_device_present("ep1")?;
    let () = check_device_present("ep3")?;

    // if we drop ep4, ep4 should disappear from the folder as well:
    std::mem::drop(ep4);
    let () = wait_for_device_absent("ep4")?;

    // ensure we're allowed to remove ep1, which was added by the .cmx file:
    let () = remove_device("ep1")?;
    let () = wait_for_device_absent("ep1")?;

    Ok(())
}
