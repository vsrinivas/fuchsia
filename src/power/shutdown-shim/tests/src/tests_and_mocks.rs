// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl::endpoints::{create_proxy, ServiceMarker},
    fidl_fuchsia_device_manager as fdevicemanager,
    fidl_fuchsia_hardware_power_statecontrol as fstatecontrol, fidl_fuchsia_io as fio,
    fidl_fuchsia_sys2 as fsys, fidl_fuchsia_test_shutdownshim as test_shutdown_shim,
    fuchsia_async as fasync,
    fuchsia_component::{client as fclient, server as fserver},
    fuchsia_syslog::{self as syslog, macros::*},
    fuchsia_zircon as zx,
    futures::{
        channel::mpsc, channel::oneshot, lock::Mutex, StreamExt, TryFutureExt, TryStreamExt,
    },
    std::sync::Arc,
};

const SHUTDOWN_SHIM_URL: &'static str =
    "fuchsia-pkg://fuchsia.com/shutdown-shim-integration-test#meta/shutdown-shim.cm";

#[derive(Debug, PartialEq)]
enum Admin {
    Reboot(fstatecontrol::RebootReason),
    RebootToBootloader,
    RebootToRecovery,
    Poweroff,
    Mexec,
    SuspendToRam,
}

#[derive(Debug, PartialEq)]
enum Signal {
    Statecontrol(Admin),
    DeviceManager(fstatecontrol::SystemPowerState),
    Sys2Shutdown,
}

async fn run_statecontrol_admin(
    send_signals: mpsc::UnboundedSender<Signal>,
    mut stream: fstatecontrol::AdminRequestStream,
) {
    fx_log_info!("new connection to {}", fstatecontrol::AdminMarker::NAME);
    async move {
        match stream.try_next().await? {
            Some(fstatecontrol::AdminRequest::PowerFullyOn { responder }) => {
                // PowerFullyOn is unsupported
                responder.send(&mut Err(zx::Status::NOT_SUPPORTED.into_raw()))?;
            }
            Some(fstatecontrol::AdminRequest::Reboot { reason, responder }) => {
                fx_log_info!("Reboot called");
                send_signals.unbounded_send(Signal::Statecontrol(Admin::Reboot(reason)))?;
                responder.send(&mut Ok(()))?;
            }
            Some(fstatecontrol::AdminRequest::RebootToBootloader { responder }) => {
                fx_log_info!("RebootToBootloader called");
                send_signals.unbounded_send(Signal::Statecontrol(Admin::RebootToBootloader))?;
                responder.send(&mut Ok(()))?;
            }
            Some(fstatecontrol::AdminRequest::RebootToRecovery { responder }) => {
                fx_log_info!("RebootToRecovery called");
                send_signals.unbounded_send(Signal::Statecontrol(Admin::RebootToRecovery))?;
                responder.send(&mut Ok(()))?;
            }
            Some(fstatecontrol::AdminRequest::Poweroff { responder }) => {
                fx_log_info!("Poweroff called");
                send_signals.unbounded_send(Signal::Statecontrol(Admin::Poweroff))?;
                responder.send(&mut Ok(()))?;
            }
            Some(fstatecontrol::AdminRequest::Mexec { responder }) => {
                fx_log_info!("Mexec called");
                send_signals.unbounded_send(Signal::Statecontrol(Admin::Mexec))?;
                responder.send(&mut Ok(()))?;
            }
            Some(fstatecontrol::AdminRequest::SuspendToRam { responder }) => {
                fx_log_info!("SuspendToRam called");
                send_signals.unbounded_send(Signal::Statecontrol(Admin::SuspendToRam))?;
                responder.send(&mut Ok(()))?;
            }
            _ => (),
        }
        Ok(())
    }
    .unwrap_or_else(|e: anyhow::Error| {
        // Note: the shim checks liveness by writing garbage data on its first connection and
        // observing PEER_CLOSED, so we're expecting this warning to happen once.
        fx_log_warn!("couldn't run {}: {:?}", fstatecontrol::AdminMarker::NAME, e);
    })
    .await
}

async fn run_device_manager_system_state_transition(
    send_signals: mpsc::UnboundedSender<Signal>,
    mut stream: fdevicemanager::SystemStateTransitionRequestStream,
) {
    fx_log_info!("new connection to {}", fdevicemanager::SystemStateTransitionMarker::NAME);
    async move {
        match stream.try_next().await? {
            Some(fdevicemanager::SystemStateTransitionRequest::SetTerminationSystemState {
                state,
                responder,
            }) => {
                fx_log_info!("SetTerminationState called");
                send_signals.unbounded_send(Signal::DeviceManager(state))?;
                responder.send(&mut Ok(()))?;
            }
            _ => (),
        }
        Ok(())
    }
    .unwrap_or_else(|e: anyhow::Error| {
        panic!("couldn't run {}: {:?}", fdevicemanager::SystemStateTransitionMarker::NAME, e);
    })
    .await
}

async fn run_sys2_system_controller(
    send_signals: mpsc::UnboundedSender<Signal>,
    mut stream: fsys::SystemControllerRequestStream,
    ignored_responders: Arc<Mutex<Vec<fsys::SystemControllerShutdownResponder>>>,
) {
    fx_log_info!("new connection to {}", fsys::SystemControllerMarker::NAME);
    async move {
        match stream.try_next().await? {
            Some(fsys::SystemControllerRequest::Shutdown { responder }) => {
                fx_log_info!("Shutdown called");
                send_signals.unbounded_send(Signal::Sys2Shutdown)?;
                // The shim should never observe this call returning, as it only returns once all
                // components are shut down, which includes the shim.
                ignored_responders.lock().await.push(responder);
            }
            _ => (),
        }
        Ok(())
    }
    .unwrap_or_else(|e: anyhow::Error| {
        panic!("couldn't run {}: {:?}", fsys::SystemControllerMarker::NAME, e);
    })
    .await
}

async fn setup_shim(
    collection_name: &str,
) -> Result<(fclient::ScopedInstance, fstatecontrol::AdminProxy), Error> {
    let shutdown_shim =
        fclient::ScopedInstance::new(collection_name.to_string(), SHUTDOWN_SHIM_URL.to_string())
            .await?;
    let shim_statecontrol =
        shutdown_shim.connect_to_protocol_at_exposed_dir::<fstatecontrol::AdminMarker>()?;
    Ok((shutdown_shim, shim_statecontrol))
}

async fn run_power_manager_present_test(
    recv_signals: Arc<Mutex<mpsc::UnboundedReceiver<Signal>>>,
    responder: test_shutdown_shim::TestsPowerManagerPresentResponder,
) -> Result<(), Error> {
    let mut recv_signals = recv_signals.lock().await;
    fx_log_info!("running PowerManagerPresent");
    let (_shutdown_shim, shim_statecontrol) =
        setup_shim("shutdown-shim-statecontrol-present").await?;

    // We've created a child shutdown-shim and connected to its
    // statecontrol.Admin protocol. Send some calls, and confirm that the shim
    // forwarded the requests to our mock service.
    let reason = fstatecontrol::RebootReason::SystemUpdate;
    shim_statecontrol.reboot(reason).await?.unwrap();
    let res = recv_signals.next().await;
    assert_eq!(res, Some(Signal::Statecontrol(Admin::Reboot(reason))));

    let reason = fstatecontrol::RebootReason::SessionFailure;
    shim_statecontrol.reboot(reason).await?.unwrap();
    let res = recv_signals.next().await;
    assert_eq!(res, Some(Signal::Statecontrol(Admin::Reboot(reason))));

    shim_statecontrol.reboot_to_bootloader().await?.unwrap();
    let res = recv_signals.next().await;
    assert_eq!(res, Some(Signal::Statecontrol(Admin::RebootToBootloader)));

    shim_statecontrol.reboot_to_recovery().await?.unwrap();
    let res = recv_signals.next().await;
    assert_eq!(res, Some(Signal::Statecontrol(Admin::RebootToRecovery)));

    shim_statecontrol.poweroff().await?.unwrap();
    let res = recv_signals.next().await;
    assert_eq!(res, Some(Signal::Statecontrol(Admin::Poweroff)));

    shim_statecontrol.suspend_to_ram().await?.unwrap();
    let res = recv_signals.next().await;
    assert_eq!(res, Some(Signal::Statecontrol(Admin::SuspendToRam)));

    // Report that the test is finished
    responder.send()?;
    fx_log_info!("PowerManagerPresent succeeded");
    Ok(())
}

async fn run_power_manager_missing_test(
    recv_signals: Arc<Mutex<mpsc::UnboundedReceiver<Signal>>>,
    responder: test_shutdown_shim::TestsPowerManagerMissingResponder,
) -> Result<(), Error> {
    let mut recv_signals = recv_signals.lock().await;
    fx_log_info!("running PowerManagerMissing");

    {
        let (_shutdown_shim, shim_statecontrol) =
            setup_shim("shutdown-shim-statecontrol-missing").await?;

        fasync::Task::spawn(async move {
            shim_statecontrol.poweroff().await.expect_err("the shutdown shim should close the channel when manual shutdown driving is complete");
        }).detach();
        assert_eq!(
            recv_signals.by_ref().take(2).collect::<Vec<_>>().await,
            vec![
                Signal::DeviceManager(fstatecontrol::SystemPowerState::Poweroff),
                Signal::Sys2Shutdown
            ]
        );
    }

    {
        let (_shutdown_shim, shim_statecontrol) =
            setup_shim("shutdown-shim-statecontrol-missing").await?;

        fasync::Task::spawn(async move {
            shim_statecontrol
                .reboot(fstatecontrol::RebootReason::SystemUpdate)
                .await
                .expect_err("the shutdown shim should close the channel when manual shutdown driving is complete");
        }).detach();
        assert_eq!(
            recv_signals.by_ref().take(2).collect::<Vec<_>>().await,
            vec![
                Signal::DeviceManager(fstatecontrol::SystemPowerState::Reboot),
                Signal::Sys2Shutdown,
            ]
        );
    }

    {
        let (shutdown_shim, shim_statecontrol) =
            setup_shim("shutdown-shim-statecontrol-missing").await?;

        let (send_mexec_returned, recv_mexec_returned) = oneshot::channel();
        fasync::Task::spawn(async move {
            shim_statecontrol.mexec().await.unwrap().unwrap();
            send_mexec_returned.send(()).expect("failed to send that mexec returned");
        })
        .detach();
        assert_eq!(
            recv_signals.by_ref().take(2).collect::<Vec<_>>().await,
            vec![
                Signal::DeviceManager(fstatecontrol::SystemPowerState::Mexec),
                Signal::Sys2Shutdown,
            ]
        );

        // Dropping the shutdown_shim will cause the shim to be destroyed, which will trigger its
        // stop event, which the shim watches for to know when to return for an mexec
        drop(shutdown_shim);

        // Mexec should actually return the call when it's done
        let _ = recv_mexec_returned.await;
    }

    // Report that the test is finished
    responder.send()?;
    fx_log_info!("PowerManagerMissing succeeded");
    Ok(())
}

async fn run_power_manager_not_present_test(
    recv_signals: Arc<Mutex<mpsc::UnboundedReceiver<Signal>>>,
    responder: test_shutdown_shim::TestsPowerManagerNotPresentResponder,
) -> Result<(), Error> {
    let mut recv_signals = recv_signals.lock().await;
    fx_log_info!("running PowerManagerNotPresent");

    {
        let (_shutdown_shim, shim_statecontrol) =
            setup_shim("shutdown-shim-statecontrol-not-present").await?;

        fasync::Task::spawn(async move {
            shim_statecontrol.poweroff().await.expect_err("the shutdown shim should close the channel when manual shutdown driving is complete");
        }).detach();
        assert_eq!(
            recv_signals.by_ref().take(2).collect::<Vec<_>>().await,
            vec![
                Signal::DeviceManager(fstatecontrol::SystemPowerState::Poweroff),
                Signal::Sys2Shutdown
            ]
        );
    }

    {
        let (_shutdown_shim, shim_statecontrol) =
            setup_shim("shutdown-shim-statecontrol-not-present").await?;

        fasync::Task::spawn(async move {
            shim_statecontrol
                .reboot(fstatecontrol::RebootReason::SystemUpdate)
                .await
                .expect_err("the shutdown shim should close the channel when manual shutdown driving is complete");
        }).detach();
        assert_eq!(
            recv_signals.by_ref().take(2).collect::<Vec<_>>().await,
            vec![
                Signal::DeviceManager(fstatecontrol::SystemPowerState::Reboot),
                Signal::Sys2Shutdown,
            ]
        );
    }

    {
        let (shutdown_shim, shim_statecontrol) =
            setup_shim("shutdown-shim-statecontrol-not-present").await?;

        let (send_mexec_returned, recv_mexec_returned) = oneshot::channel();
        fasync::Task::spawn(async move {
            shim_statecontrol.mexec().await.unwrap().unwrap();
            send_mexec_returned.send(()).expect("failed to send that mexec returned");
        })
        .detach();
        assert_eq!(
            recv_signals.by_ref().take(2).collect::<Vec<_>>().await,
            vec![
                Signal::DeviceManager(fstatecontrol::SystemPowerState::Mexec),
                Signal::Sys2Shutdown,
            ]
        );

        // Dropping the shutdown_shim will cause the shim to be destroyed, which will trigger its
        // stop event, which the shim watches for to know when to return for an mexec
        drop(shutdown_shim);

        // Mexec should actually return the call when it's done
        let _ = recv_mexec_returned.await;
    }

    // Report that the test is finished
    responder.send()?;
    fx_log_info!("PowerManagerNotPresent succeeded");
    Ok(())
}

async fn run_tests(
    mut stream: test_shutdown_shim::TestsRequestStream,
    recv_signals: Arc<Mutex<mpsc::UnboundedReceiver<Signal>>>,
) {
    fx_log_info!("new connection to {}", test_shutdown_shim::TestsMarker::NAME);
    async move {
        match stream.try_next().await? {
            Some(test_shutdown_shim::TestsRequest::PowerManagerPresent { responder }) => {
                run_power_manager_present_test(recv_signals.clone(), responder).await?;
            }

            Some(test_shutdown_shim::TestsRequest::PowerManagerMissing { responder }) => {
                run_power_manager_missing_test(recv_signals.clone(), responder).await?;
            }
            Some(test_shutdown_shim::TestsRequest::PowerManagerNotPresent { responder }) => {
                run_power_manager_not_present_test(recv_signals.clone(), responder).await?;
            }
            r => panic!("unexpected request: {:?}", r),
        }
        Ok(())
    }
    .unwrap_or_else(|e: anyhow::Error| {
        panic!("couldn't run fuchsia.test.shutdownshim.Tests: {:?}", e);
    })
    .await
}

#[fasync::run_singlethreaded()]
async fn main() -> Result<(), Error> {
    syslog::init_with_tags(&["shutdown-shim-mock-services"])?;
    fx_log_info!("started");
    let (send_signals, recv_signals) = mpsc::unbounded();

    // Each test that can be run will lock this mutex before doing anything, guaranteeing that we
    // only have one child shutdown-shim at a time.
    let recv_signals = Arc::new(Mutex::new(recv_signals));

    let mut fs = fserver::ServiceFs::new();
    let send_admin_signals = send_signals.clone();
    fs.dir("svc").add_fidl_service(move |stream| {
        fasync::Task::spawn(run_statecontrol_admin(send_admin_signals.clone(), stream)).detach();
    });
    let send_system_state_transition_signals = send_signals.clone();
    fs.dir("svc").add_fidl_service(move |stream| {
        fasync::Task::spawn(run_device_manager_system_state_transition(
            send_system_state_transition_signals.clone(),
            stream,
        ))
        .detach();
    });
    let send_sys2_signals = send_signals.clone();
    let ignored_responders = Arc::new(Mutex::new(vec![]));
    fs.dir("svc").add_fidl_service(move |stream| {
        fasync::Task::spawn(run_sys2_system_controller(
            send_sys2_signals.clone(),
            stream,
            ignored_responders.clone(),
        ))
        .detach();
    });
    fs.dir("svc").add_fidl_service(move |stream| {
        fasync::Task::spawn(run_tests(stream, recv_signals.clone())).detach();
    });

    // The black_hole directory points to a channel we will never answer, so that capabilities
    // provided from this directory will behave similarly to as if they were from an unlaunched
    // component.
    let (proxy, _server_end) = create_proxy::<fio::DirectoryMarker>()?;
    fs.add_remote("black_hole", proxy);

    fs.take_and_serve_directory_handle()?;
    fs.collect::<()>().await;

    Ok(())
}
