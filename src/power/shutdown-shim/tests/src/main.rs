// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::shutdown_mocks::{new_mocks_provider, Admin, Signal},
    anyhow::Error,
    assert_matches::assert_matches,
    fidl_fuchsia_boot as fboot, fidl_fuchsia_device_manager as fdevicemanager,
    fidl_fuchsia_hardware_power_statecontrol as fstatecontrol, fidl_fuchsia_sys2 as fsys,
    fuchsia_async as fasync,
    fuchsia_component_test::new::{
        Capability, ChildOptions, LocalComponentHandles, RealmBuilder, RealmInstance, Ref, Route,
    },
    fuchsia_zircon as zx,
    futures::{channel::mpsc, future, StreamExt},
};

mod shutdown_mocks;

const SHUTDOWN_SHIM_URL: &'static str =
    "fuchsia-pkg://fuchsia.com/shutdown-shim-integration-tests#meta/shutdown-shim.cm";

#[derive(PartialEq)]
enum RealmVariant {
    // Power manager is present, running, and actively handling requests.
    PowerManagerPresent,

    // Power manager hasn't started yet for whatever reason, and FIDL connections to it are hanging
    // as they wait for it to start.
    PowerManagerIsntStartedYet,

    // Power manager isn't present on the system, and FIDL connections to it are closed
    // immediately.
    PowerManagerNotPresent,
}

// Makes a new realm containing a shutdown shim and a mocks server.
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
async fn new_realm(
    variant: RealmVariant,
) -> Result<(RealmInstance, mpsc::UnboundedReceiver<Signal>), Error> {
    let (mocks_provider, recv_signals) = new_mocks_provider();
    let builder = RealmBuilder::new().await?;
    let shutdown_shim =
        builder.add_child("shutdown-shim", SHUTDOWN_SHIM_URL, ChildOptions::new()).await?;
    let mocks_server =
        builder.add_local_child("mocks-server", mocks_provider, ChildOptions::new()).await?;
    // Give the shim logging
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol::<fboot::WriteOnlyLogMarker>())
                .from(Ref::parent())
                .to(&shutdown_shim),
        )
        .await?;
    // Expose the shim's statecontrol.Admin so test cases can access it
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol::<fstatecontrol::AdminMarker>())
                .from(&shutdown_shim)
                .to(Ref::parent()),
        )
        .await?;
    // Give the shim the driver_manager and component_manager mocks, as those are always available
    // to the shim in prod
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol::<fdevicemanager::SystemStateTransitionMarker>())
                .capability(Capability::protocol::<fsys::SystemControllerMarker>())
                .from(&mocks_server)
                .to(&shutdown_shim),
        )
        .await?;

    match variant {
        RealmVariant::PowerManagerPresent => {
            builder
                .add_route(
                    Route::new()
                        .capability(Capability::protocol::<fstatecontrol::AdminMarker>())
                        .from(&mocks_server)
                        .to(&shutdown_shim),
                )
                .await?;
        }
        RealmVariant::PowerManagerIsntStartedYet => {
            let black_hole = builder
                .add_local_child(
                    "black-hole",
                    move |handles: LocalComponentHandles| {
                        Box::pin(async move {
                            let _handles = handles;
                            // We want to hold the mock_handles for the lifetime of this mock component, but
                            // never do anything with them. This will cause FIDL requests to us to go
                            // unanswered, simulating the environment where a component is unable to launch due
                            // to pkgfs not coming online.
                            future::pending::<()>().await;
                            panic!("the black hole component should never return")
                        })
                    },
                    ChildOptions::new(),
                )
                .await?;
            builder
                .add_route(
                    Route::new()
                        .capability(Capability::protocol::<fstatecontrol::AdminMarker>())
                        .from(&black_hole)
                        .to(&shutdown_shim),
                )
                .await?;
        }
        RealmVariant::PowerManagerNotPresent => (),
    }

    Ok((builder.build().await?, recv_signals))
}

#[fasync::run_singlethreaded(test)]
async fn power_manager_present_reboot_system_update() -> Result<(), Error> {
    let (realm_instance, mut recv_signals) = new_realm(RealmVariant::PowerManagerPresent).await?;
    let shim_statecontrol =
        realm_instance.root.connect_to_protocol_at_exposed_dir::<fstatecontrol::AdminMarker>()?;

    shim_statecontrol.reboot(fstatecontrol::RebootReason::SystemUpdate).await?.unwrap();
    let res = recv_signals.next().await;
    assert_matches!(
        res,
        Some(Signal::Statecontrol(Admin::Reboot(fstatecontrol::RebootReason::SystemUpdate)))
    );
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn power_manager_present_reboot_session_failure() -> Result<(), Error> {
    let (realm_instance, mut recv_signals) = new_realm(RealmVariant::PowerManagerPresent).await?;
    let shim_statecontrol =
        realm_instance.root.connect_to_protocol_at_exposed_dir::<fstatecontrol::AdminMarker>()?;

    shim_statecontrol.reboot(fstatecontrol::RebootReason::SessionFailure).await?.unwrap();
    let res = recv_signals.next().await;
    assert_matches!(
        res,
        Some(Signal::Statecontrol(Admin::Reboot(fstatecontrol::RebootReason::SessionFailure)))
    );
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn power_manager_present_reboot_to_bootloader() -> Result<(), Error> {
    let (realm_instance, mut recv_signals) = new_realm(RealmVariant::PowerManagerPresent).await?;
    let shim_statecontrol =
        realm_instance.root.connect_to_protocol_at_exposed_dir::<fstatecontrol::AdminMarker>()?;

    shim_statecontrol.reboot_to_bootloader().await?.unwrap();
    let res = recv_signals.next().await;
    assert_matches!(res, Some(Signal::Statecontrol(Admin::RebootToBootloader)));
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn power_manager_present_reboot_to_recovery() -> Result<(), Error> {
    let (realm_instance, mut recv_signals) = new_realm(RealmVariant::PowerManagerPresent).await?;
    let shim_statecontrol =
        realm_instance.root.connect_to_protocol_at_exposed_dir::<fstatecontrol::AdminMarker>()?;

    shim_statecontrol.reboot_to_recovery().await?.unwrap();
    let res = recv_signals.next().await;
    assert_matches!(res, Some(Signal::Statecontrol(Admin::RebootToRecovery)));
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn power_manager_present_poweroff() -> Result<(), Error> {
    let (realm_instance, mut recv_signals) = new_realm(RealmVariant::PowerManagerPresent).await?;
    let shim_statecontrol =
        realm_instance.root.connect_to_protocol_at_exposed_dir::<fstatecontrol::AdminMarker>()?;

    shim_statecontrol.poweroff().await?.unwrap();
    let res = recv_signals.next().await;
    assert_matches!(res, Some(Signal::Statecontrol(Admin::Poweroff)));
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn power_manager_present_mexec() -> Result<(), Error> {
    let (realm_instance, mut recv_signals) = new_realm(RealmVariant::PowerManagerPresent).await?;
    let shim_statecontrol =
        realm_instance.root.connect_to_protocol_at_exposed_dir::<fstatecontrol::AdminMarker>()?;

    let kernel_zbi = zx::Vmo::create(0).unwrap();
    let data_zbi = zx::Vmo::create(0).unwrap();
    shim_statecontrol.mexec(kernel_zbi, data_zbi).await?.unwrap();
    let res = recv_signals.next().await;
    assert_matches!(res, Some(Signal::Statecontrol(Admin::Mexec)));
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn power_manager_present_suspend_to_ram() -> Result<(), Error> {
    let (realm_instance, mut recv_signals) = new_realm(RealmVariant::PowerManagerPresent).await?;
    let shim_statecontrol =
        realm_instance.root.connect_to_protocol_at_exposed_dir::<fstatecontrol::AdminMarker>()?;

    shim_statecontrol.suspend_to_ram().await?.unwrap();
    let res = recv_signals.next().await;
    assert_matches!(res, Some(Signal::Statecontrol(Admin::SuspendToRam)));
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn power_manager_missing_poweroff() -> Result<(), Error> {
    let (realm_instance, mut recv_signals) =
        new_realm(RealmVariant::PowerManagerIsntStartedYet).await?;
    let shim_statecontrol =
        realm_instance.root.connect_to_protocol_at_exposed_dir::<fstatecontrol::AdminMarker>()?;

    // We don't expect this task to ever complete, as the shutdown shim isn't actually killed (the
    // shutdown methods it calls are mocks after all).
    let _task = fasync::Task::spawn(async move {
        shim_statecontrol.poweroff().await.expect_err(
            "the shutdown shim should close the channel when manual shutdown driving is complete",
        );
    });
    assert_matches!(
        recv_signals.next().await,
        Some(Signal::DeviceManager(fstatecontrol::SystemPowerState::Poweroff))
    );
    assert_matches!(recv_signals.next().await, Some(Signal::Sys2Shutdown(_)));
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn power_manager_missing_reboot_system_update() -> Result<(), Error> {
    let (realm_instance, mut recv_signals) =
        new_realm(RealmVariant::PowerManagerIsntStartedYet).await?;
    let shim_statecontrol =
        realm_instance.root.connect_to_protocol_at_exposed_dir::<fstatecontrol::AdminMarker>()?;

    // We don't expect this task to ever complete, as the shutdown shim isn't actually killed (the
    // shutdown methods it calls are mocks after all).
    let _task = fasync::Task::spawn(async move {
        shim_statecontrol.reboot(fstatecontrol::RebootReason::SystemUpdate).await.expect_err(
            "the shutdown shim should close the channel when manual shutdown driving is complete",
        );
    });

    assert_matches!(
        recv_signals.next().await,
        Some(Signal::DeviceManager(fstatecontrol::SystemPowerState::Reboot))
    );
    assert_matches!(recv_signals.next().await, Some(Signal::Sys2Shutdown(_)));
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn power_manager_missing_mexec() -> Result<(), Error> {
    let (realm_instance, mut recv_signals) =
        new_realm(RealmVariant::PowerManagerIsntStartedYet).await?;
    let shim_statecontrol =
        realm_instance.root.connect_to_protocol_at_exposed_dir::<fstatecontrol::AdminMarker>()?;

    // We don't expect this task to ever complete, as the shutdown shim isn't actually killed (the
    // shutdown methods it calls are mocks after all).
    let _task = fasync::Task::spawn(async move {
        let kernel_zbi = zx::Vmo::create(0).unwrap();
        let data_zbi = zx::Vmo::create(0).unwrap();
        shim_statecontrol.mexec(kernel_zbi, data_zbi).await.expect_err(
            "the shutdown shim should close the channel when manual shutdown driving is complete",
        );
    });

    assert_matches!(
        recv_signals.next().await,
        Some(Signal::DeviceManager(fstatecontrol::SystemPowerState::Mexec))
    );
    assert_matches!(recv_signals.next().await, Some(Signal::Sys2Shutdown(_)));
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn power_manager_not_present_poweroff() -> Result<(), Error> {
    let (realm_instance, mut recv_signals) =
        new_realm(RealmVariant::PowerManagerNotPresent).await?;
    let shim_statecontrol =
        realm_instance.root.connect_to_protocol_at_exposed_dir::<fstatecontrol::AdminMarker>()?;

    fasync::Task::spawn(async move {
        shim_statecontrol.poweroff().await.expect_err(
            "the shutdown shim should close the channel when manual shutdown driving is complete",
        );
    })
    .detach();

    assert_matches!(
        recv_signals.next().await,
        Some(Signal::DeviceManager(fstatecontrol::SystemPowerState::Poweroff))
    );
    assert_matches!(recv_signals.next().await, Some(Signal::Sys2Shutdown(_)));
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn power_manager_not_present_reboot() -> Result<(), Error> {
    let (realm_instance, mut recv_signals) =
        new_realm(RealmVariant::PowerManagerNotPresent).await?;
    let shim_statecontrol =
        realm_instance.root.connect_to_protocol_at_exposed_dir::<fstatecontrol::AdminMarker>()?;

    fasync::Task::spawn(async move {
        shim_statecontrol.reboot(fstatecontrol::RebootReason::SystemUpdate).await.expect_err(
            "the shutdown shim should close the channel when manual shutdown driving is complete",
        );
    })
    .detach();

    assert_matches!(
        recv_signals.next().await,
        Some(Signal::DeviceManager(fstatecontrol::SystemPowerState::Reboot))
    );
    assert_matches!(recv_signals.next().await, Some(Signal::Sys2Shutdown(_)));
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn power_manager_not_present_mexec() -> Result<(), Error> {
    let (realm_instance, mut recv_signals) =
        new_realm(RealmVariant::PowerManagerNotPresent).await?;
    let shim_statecontrol =
        realm_instance.root.connect_to_protocol_at_exposed_dir::<fstatecontrol::AdminMarker>()?;

    fasync::Task::spawn(async move {
        let kernel_zbi = zx::Vmo::create(0).unwrap();
        let data_zbi = zx::Vmo::create(0).unwrap();
        shim_statecontrol.mexec(kernel_zbi, data_zbi).await.expect_err(
            "the shutdown shim should close the channel when manual shutdown driving is complete",
        );
    })
    .detach();

    assert_matches!(
        recv_signals.next().await,
        Some(Signal::DeviceManager(fstatecontrol::SystemPowerState::Mexec))
    );
    assert_matches!(recv_signals.next().await, Some(Signal::Sys2Shutdown(_)));
    Ok(())
}
