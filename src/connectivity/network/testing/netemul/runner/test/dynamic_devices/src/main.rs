// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro, futures_api)]

use {
    failure::{format_err, Error},
    fidl_fuchsia_netemul_environment::{ManagedEnvironmentMarker, VirtualDevice},
    fidl_fuchsia_netemul_network::{
        DeviceProxy_Marker, EndpointBacking, EndpointConfig, EndpointManagerMarker, EndpointProxy,
        NetworkContextMarker,
    },
    fuchsia_async as fasync,
    fuchsia_component::client,
    std::path::Path,
};

// some of the device changes may take a short while to show up
// this helper function will sleep for a bit in case |test| returns an error.
// Mostly here to prevent flakiness on tests due to ordering of operations on different channels.
fn try_with_failure_tolerance<T>(test: T) -> Result<(), Error>
where
    T: Fn() -> Result<(), Error>,
{
    let mut tries = 3;
    loop {
        let res = test();
        match res {
            Ok(_) => break Ok(()),
            Err(err) => {
                tries -= 1;
                if tries == 0 {
                    break Err(err);
                } else {
                    std::thread::sleep(std::time::Duration::from_millis(20))
                }
            }
        }
    }
}

fn device_present(path: &str) -> bool {
    Path::new(&format!("/vdev/class/ethernet/{}", path)).exists()
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
    env.remove_device(&format!("/class/ethernet/{}", ep_name))?;
    Ok(())
}

async fn attach_device(ep_name: &str) -> Result<(), Error> {
    let netctx = client::connect_to_service::<NetworkContextMarker>()?;
    // get the endpoint manager
    let (epm, epm_server_end) = fidl::endpoints::create_proxy::<EndpointManagerMarker>()?;
    netctx.get_endpoint_manager(epm_server_end)?;

    // get endpoint by name
    let ep = await!(epm.get_endpoint(ep_name))?;

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
    let (_, ep) = await!(epm.create_endpoint(name, &mut cfg))?;
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
    let () = await!(attach_device("ep2"))?;
    let () = try_with_failure_tolerance(|| check_device_present("ep2"))?;
    // attach cmx-created endpoint ep3 and check that it shows up
    let () = await!(attach_device("ep3"))?;
    let () = try_with_failure_tolerance(|| check_device_present("ep3"))?;

    // create a new endpoint and attach:
    let ep4 = await!(create_endpoint("ep4"))?;
    let () = await!(attach_device("ep4"))?;
    let () = try_with_failure_tolerance(|| check_device_present("ep4"))?;

    // remove ep2:
    let () = remove_device("ep2")?;
    let () = try_with_failure_tolerance(|| check_device_absent("ep2"))?;
    // other ep1 and ep3 should still be there:
    let () = check_device_present("ep1")?;
    let () = check_device_present("ep3")?;

    // if we drop ep4, ep4 should disappear from the folder as well:
    std::mem::drop(ep4);
    let () = try_with_failure_tolerance(|| check_device_absent("ep4"))?;

    // ensure we're allowed to remove ep1, which was added by the .cmx file:
    let () = remove_device("ep1")?;
    let () = try_with_failure_tolerance(|| check_device_absent("ep1"))?;

    Ok(())
}
