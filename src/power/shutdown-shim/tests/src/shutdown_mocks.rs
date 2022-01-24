// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl::endpoints::{create_proxy, ProtocolMarker},
    fidl_fuchsia_device_manager as fdevicemanager,
    fidl_fuchsia_hardware_power_statecontrol as fstatecontrol, fidl_fuchsia_io as fio,
    fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync,
    fuchsia_component::server as fserver,
    fuchsia_component_test::new::LocalComponentHandles,
    fuchsia_syslog::macros::*,
    fuchsia_zircon as zx,
    futures::{channel::mpsc, future::BoxFuture, FutureExt, StreamExt, TryFutureExt, TryStreamExt},
};

pub fn new_mocks_provider() -> (
    impl Fn(LocalComponentHandles) -> BoxFuture<'static, Result<(), anyhow::Error>>
        + Sync
        + Send
        + 'static,
    mpsc::UnboundedReceiver<Signal>,
) {
    let (send_signals, recv_signals) = mpsc::unbounded();

    let mock =
        move |handles: LocalComponentHandles| run_mocks(send_signals.clone(), handles).boxed();

    (mock, recv_signals)
}

async fn run_mocks(
    send_signals: mpsc::UnboundedSender<Signal>,
    handles: LocalComponentHandles,
) -> Result<(), Error> {
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
    fs.dir("svc").add_fidl_service(move |stream| {
        fasync::Task::spawn(run_sys2_system_controller(send_sys2_signals.clone(), stream)).detach();
    });

    // The black_hole directory points to a channel we will never answer, so that capabilities
    // provided from this directory will behave similarly to as if they were from an unlaunched
    // component.
    let (proxy, _server_end) = create_proxy::<fio::DirectoryMarker>()?;
    fs.add_remote("black_hole", proxy);

    fs.serve_connection(handles.outgoing_dir.into_channel())?;
    fs.collect::<()>().await;

    Ok(())
}

#[derive(Debug, PartialEq)]
pub enum Admin {
    Reboot(fstatecontrol::RebootReason),
    RebootToBootloader,
    RebootToRecovery,
    Poweroff,
    Mexec,
    SuspendToRam,
}

#[derive(Debug)]
pub enum Signal {
    Statecontrol(Admin),
    DeviceManager(fstatecontrol::SystemPowerState),
    Sys2Shutdown(fsys::SystemControllerShutdownResponder),
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
            Some(fstatecontrol::AdminRequest::Mexec { responder, .. }) => {
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
            Some(fdevicemanager::SystemStateTransitionRequest::SetMexecZbis {
                responder, ..
            }) => {
                fx_log_info!("SetMexecZbis called");
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
) {
    fx_log_info!("new connection to {}", fsys::SystemControllerMarker::NAME);
    async move {
        match stream.try_next().await? {
            Some(fsys::SystemControllerRequest::Shutdown { responder }) => {
                fx_log_info!("Shutdown called");
                // Send the responder out with the signal.
                // The responder keeps the current request and the request stream alive.
                send_signals.unbounded_send(Signal::Sys2Shutdown(responder))?;
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
