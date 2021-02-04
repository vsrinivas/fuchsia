// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::shutdown_mocks::{new_mocks_provider, Admin, Signal},
    anyhow::{format_err, Error},
    fidl_fuchsia_hardware_power_statecontrol as fstatecontrol, fuchsia_async as fasync,
    futures::{channel::mpsc, future, StreamExt},
    topology_builder::{builder::*, mock},
};

mod shutdown_mocks;

const SHUTDOWN_SHIM_URL: &'static str =
    "fuchsia-pkg://fuchsia.com/shutdown-shim-integration-tests#meta/shutdown-shim.cm";

#[derive(PartialEq)]
enum TopologyVariant {
    // Power manager is present, running, and actively handling requests.
    PowerManagerPresent,

    // Power manager hasn't started yet for whatever reason, and FIDL connections to it are hanging
    // as they wait for it to start.
    PowerManagerIsntStartedYet,

    // Power manager isn't present on the system, and FIDL connections to it are closed
    // immediately.
    PowerManagerNotPresent,
}

// Makes a new topology containing a shutdown shim and a mocks server.
//
//              root
//             /    \
// shutdown-shim    mocks-server
//
// The mocks server is seeded with an mpsc::UnboundedSender, and whenever the shutdown-shim calls a
// function on the mocks server the mocks server will emit a shutdown_mocks::Signal over the
// channel.
//
// The shutdown-shim always receives logging from above the root, along with mock driver_manager
// and component_manager protocols from the mocks-server (because these are always present in
// prod). The `variant` field determines whether the shim receives a functional version of the
// power_manager mocks.
async fn new_topology(
    variant: TopologyVariant,
) -> Result<(topology_builder::Topology, mpsc::UnboundedReceiver<Signal>), Error> {
    let (mocks_provider, recv_signals) = new_mocks_provider();
    let mut builder = TopologyBuilder::new().await?;
    builder
        .add_component("shutdown-shim", ComponentSource::url(SHUTDOWN_SHIM_URL))
        .await?
        .add_component("mocks-server", mocks_provider)
        .await?
        // Give the shim logging
        .add_route(CapabilityRoute {
            capability: Capability::protocol("fuchsia.boot.WriteOnlyLog"),
            source: RouteEndpoint::AboveRoot,
            targets: vec![RouteEndpoint::component("shutdown-shim")],
        })?
        // Expose the shim's statecontrol.Admin so test cases can access it
        .add_route(CapabilityRoute {
            capability: Capability::protocol("fuchsia.hardware.power.statecontrol.Admin"),
            source: RouteEndpoint::component("shutdown-shim"),
            targets: vec![RouteEndpoint::AboveRoot],
        })?
        // Give the shim the driver_manager and component_manager mocks, as those are always
        // available to the shim in prod
        .add_route(CapabilityRoute {
            capability: Capability::protocol("fuchsia.device.manager.SystemStateTransition"),
            source: RouteEndpoint::component("mocks-server"),
            targets: vec![RouteEndpoint::component("shutdown-shim")],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::protocol("fuchsia.sys2.SystemController"),
            source: RouteEndpoint::component("mocks-server"),
            targets: vec![RouteEndpoint::component("shutdown-shim")],
        })?;

    match variant {
        TopologyVariant::PowerManagerPresent => {
            builder.add_route(CapabilityRoute {
                capability: Capability::protocol("fuchsia.hardware.power.statecontrol.Admin"),
                source: RouteEndpoint::component("mocks-server"),
                targets: vec![RouteEndpoint::component("shutdown-shim")],
            })?;
        }
        TopologyVariant::PowerManagerIsntStartedYet => {
            builder
                .add_component(
                    "black-hole",
                    ComponentSource::Mock(mock::Mock::new(
                        move |mock_handles: mock::MockHandles| {
                            Box::pin(async move {
                                let outgoing_dir_handle = mock_handles.outgoing_dir;
                                // We want to hold the mock_handles for the lifetime of this mock component, but
                                // never do anything with them. This will cause FIDL requests to us to go
                                // unanswered, simulating the environment where a component is unable to launch due
                                // to pkgfs not coming online.
                                future::pending::<()>().await;
                                println!(
                                    "look, I still have the outgoing dir: {:?}",
                                    outgoing_dir_handle
                                );
                                panic!("the black hole component should never return")
                            })
                        },
                    )),
                )
                .await?;
            builder.add_route(CapabilityRoute {
                capability: Capability::protocol("fuchsia.hardware.power.statecontrol.Admin"),
                source: RouteEndpoint::component("black-hole"),
                targets: vec![RouteEndpoint::component("shutdown-shim")],
            })?;
        }
        TopologyVariant::PowerManagerNotPresent => (),
    }

    Ok((builder.build(), recv_signals))
}

#[fasync::run_singlethreaded(test)]
async fn power_manager_present_reboot_system_update() -> Result<(), Error> {
    let (topology, mut recv_signals) = new_topology(TopologyVariant::PowerManagerPresent).await?;
    let topology_instance = topology.create().await?;
    let shim_statecontrol = topology_instance
        .root
        .connect_to_protocol_at_exposed_dir::<fstatecontrol::AdminMarker>()?;

    let reason = fstatecontrol::RebootReason::SystemUpdate;
    shim_statecontrol.reboot(reason).await?.unwrap();
    let res = recv_signals.next().await;
    assert_eq!(res, Some(Signal::Statecontrol(Admin::Reboot(reason))));
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn power_manager_present_reboot_session_failure() -> Result<(), Error> {
    let (topology, mut recv_signals) = new_topology(TopologyVariant::PowerManagerPresent).await?;
    let topology_instance = topology.create().await?;
    let shim_statecontrol = topology_instance
        .root
        .connect_to_protocol_at_exposed_dir::<fstatecontrol::AdminMarker>()?;

    let reason = fstatecontrol::RebootReason::SessionFailure;
    shim_statecontrol.reboot(reason).await?.unwrap();
    let res = recv_signals.next().await;
    assert_eq!(res, Some(Signal::Statecontrol(Admin::Reboot(reason))));
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn power_manager_present_reboot_to_bootloader() -> Result<(), Error> {
    let (topology, mut recv_signals) = new_topology(TopologyVariant::PowerManagerPresent).await?;
    let topology_instance = topology.create().await?;
    let shim_statecontrol = topology_instance
        .root
        .connect_to_protocol_at_exposed_dir::<fstatecontrol::AdminMarker>()?;

    shim_statecontrol.reboot_to_bootloader().await?.unwrap();
    let res = recv_signals.next().await;
    assert_eq!(res, Some(Signal::Statecontrol(Admin::RebootToBootloader)));
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn power_manager_present_reboot_to_recovery() -> Result<(), Error> {
    let (topology, mut recv_signals) = new_topology(TopologyVariant::PowerManagerPresent).await?;
    let topology_instance = topology.create().await?;
    let shim_statecontrol = topology_instance
        .root
        .connect_to_protocol_at_exposed_dir::<fstatecontrol::AdminMarker>()?;

    shim_statecontrol.reboot_to_recovery().await?.unwrap();
    let res = recv_signals.next().await;
    assert_eq!(res, Some(Signal::Statecontrol(Admin::RebootToRecovery)));
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn power_manager_present_poweroff() -> Result<(), Error> {
    let (topology, mut recv_signals) = new_topology(TopologyVariant::PowerManagerPresent).await?;
    let topology_instance = topology.create().await?;
    let shim_statecontrol = topology_instance
        .root
        .connect_to_protocol_at_exposed_dir::<fstatecontrol::AdminMarker>()?;

    shim_statecontrol.poweroff().await?.unwrap();
    let res = recv_signals.next().await;
    assert_eq!(res, Some(Signal::Statecontrol(Admin::Poweroff)));
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn power_manager_present_suspend_to_ram() -> Result<(), Error> {
    let (topology, mut recv_signals) = new_topology(TopologyVariant::PowerManagerPresent).await?;
    let topology_instance = topology.create().await?;
    let shim_statecontrol = topology_instance
        .root
        .connect_to_protocol_at_exposed_dir::<fstatecontrol::AdminMarker>()?;

    shim_statecontrol.suspend_to_ram().await?.unwrap();
    let res = recv_signals.next().await;
    assert_eq!(res, Some(Signal::Statecontrol(Admin::SuspendToRam)));
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn power_manager_missing_poweroff() -> Result<(), Error> {
    let (topology, mut recv_signals) =
        new_topology(TopologyVariant::PowerManagerIsntStartedYet).await?;
    let topology_instance = topology.create().await?;
    let shim_statecontrol = topology_instance
        .root
        .connect_to_protocol_at_exposed_dir::<fstatecontrol::AdminMarker>()?;

    // We don't expect this task to ever complete, as the shutdown shim isn't actually killed (the
    // shutdown methods it calls are mocks after all).
    let _task = fasync::Task::spawn(async move {
        shim_statecontrol.poweroff().await.expect_err(
            "the shutdown shim should close the channel when manual shutdown driving is complete",
        );
    });
    assert_eq!(
        recv_signals.by_ref().take(2).collect::<Vec<_>>().await,
        vec![
            Signal::DeviceManager(fstatecontrol::SystemPowerState::Poweroff),
            Signal::Sys2Shutdown
        ]
    );
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn power_manager_missing_reboot_system_update() -> Result<(), Error> {
    let (topology, mut recv_signals) =
        new_topology(TopologyVariant::PowerManagerIsntStartedYet).await?;
    let topology_instance = topology.create().await?;
    let shim_statecontrol = topology_instance
        .root
        .connect_to_protocol_at_exposed_dir::<fstatecontrol::AdminMarker>()?;

    // We don't expect this task to ever complete, as the shutdown shim isn't actually killed (the
    // shutdown methods it calls are mocks after all).
    let _task = fasync::Task::spawn(async move {
        shim_statecontrol.reboot(fstatecontrol::RebootReason::SystemUpdate).await.expect_err(
            "the shutdown shim should close the channel when manual shutdown driving is complete",
        );
    });
    assert_eq!(
        recv_signals.by_ref().take(2).collect::<Vec<_>>().await,
        vec![Signal::DeviceManager(fstatecontrol::SystemPowerState::Reboot), Signal::Sys2Shutdown,]
    );
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn power_manager_missing_mexec() -> Result<(), Error> {
    let (topology, mut recv_signals) =
        new_topology(TopologyVariant::PowerManagerIsntStartedYet).await?;
    let topology_instance = topology.create().await?;
    let shim_statecontrol = topology_instance
        .root
        .connect_to_protocol_at_exposed_dir::<fstatecontrol::AdminMarker>()?;

    let mexec_task = fasync::Task::spawn(async move { shim_statecontrol.mexec().await });
    assert_eq!(
        recv_signals.by_ref().take(2).collect::<Vec<_>>().await,
        vec![Signal::DeviceManager(fstatecontrol::SystemPowerState::Mexec), Signal::Sys2Shutdown,]
    );

    // Dropping the shutdown_shim will cause the shim to be destroyed, which will trigger its
    // stop event, which the shim watches for to know when to return for an mexec
    drop(topology_instance);

    // Mexec should actually return the call when it's done
    mexec_task.await?.map_err(|e| format_err!("mexec call failed: {:?}", e))?;
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn power_manager_not_present_poweroff() -> Result<(), Error> {
    let (topology, mut recv_signals) =
        new_topology(TopologyVariant::PowerManagerNotPresent).await?;
    let topology_instance = topology.create().await?;
    let shim_statecontrol = topology_instance
        .root
        .connect_to_protocol_at_exposed_dir::<fstatecontrol::AdminMarker>()?;

    fasync::Task::spawn(async move {
        shim_statecontrol.poweroff().await.expect_err(
            "the shutdown shim should close the channel when manual shutdown driving is complete",
        );
    })
    .detach();
    assert_eq!(
        recv_signals.by_ref().take(2).collect::<Vec<_>>().await,
        vec![
            Signal::DeviceManager(fstatecontrol::SystemPowerState::Poweroff),
            Signal::Sys2Shutdown
        ]
    );
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn power_manager_not_present_reboot() -> Result<(), Error> {
    let (topology, mut recv_signals) =
        new_topology(TopologyVariant::PowerManagerNotPresent).await?;
    let topology_instance = topology.create().await?;
    let shim_statecontrol = topology_instance
        .root
        .connect_to_protocol_at_exposed_dir::<fstatecontrol::AdminMarker>()?;

    fasync::Task::spawn(async move {
        shim_statecontrol.reboot(fstatecontrol::RebootReason::SystemUpdate).await.expect_err(
            "the shutdown shim should close the channel when manual shutdown driving is complete",
        );
    })
    .detach();
    assert_eq!(
        recv_signals.by_ref().take(2).collect::<Vec<_>>().await,
        vec![Signal::DeviceManager(fstatecontrol::SystemPowerState::Reboot), Signal::Sys2Shutdown,]
    );
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn power_manager_not_present_mexec() -> Result<(), Error> {
    let (topology, mut recv_signals) =
        new_topology(TopologyVariant::PowerManagerNotPresent).await?;
    let topology_instance = topology.create().await?;
    let shim_statecontrol = topology_instance
        .root
        .connect_to_protocol_at_exposed_dir::<fstatecontrol::AdminMarker>()?;

    let mexec_task = fasync::Task::spawn(shim_statecontrol.mexec());
    assert_eq!(
        recv_signals.by_ref().take(2).collect::<Vec<_>>().await,
        vec![Signal::DeviceManager(fstatecontrol::SystemPowerState::Mexec), Signal::Sys2Shutdown,]
    );

    // Dropping the shutdown_shim will cause the shim to be destroyed, which will trigger its
    // stop event, which the shim watches for to know when to return for an mexec
    drop(topology_instance);

    // Mexec should actually return the call when it's done
    mexec_task.await?.map_err(|e| format_err!("error returned on mexec call: {:?}", e))?;
    Ok(())
}
