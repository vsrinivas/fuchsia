// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::arguments,
    crate::services,
    anyhow::{anyhow, Error},
    fidl::endpoints::{create_proxy, Proxy},
    fidl_fuchsia_virtualization::{GuestManagerProxy, GuestMarker, GuestStatus},
    fuchsia_async::{self as fasync, TimeoutExt},
    fuchsia_zircon as zx,
};

pub async fn handle_stop(args: &arguments::StopArgs) -> Result<(), Error> {
    let manager = services::connect_to_manager(args.guest_type)?;
    let status =
        manager.get_guest_info().await?.guest_status.expect("guest status should always be set");
    if status != GuestStatus::Starting && status != GuestStatus::Running {
        println!("Nothing to do - the guest is not running");
        return Ok(());
    }

    if args.force {
        force_stop_guest(args.guest_type, manager).await
    } else {
        graceful_stop_guest(args.guest_type, manager).await
    }
}

fn get_graceful_stop_command(guest: arguments::GuestType) -> Option<Vec<u8>> {
    let arg_string = match guest {
        arguments::GuestType::Zircon => "dm shutdown\n".to_string(),
        arguments::GuestType::Debian => "shutdown now\n".to_string(),
        arguments::GuestType::Termina => {
            return None;
        }
    };

    Some(arg_string.into_bytes())
}

async fn graceful_stop_guest(
    guest: arguments::GuestType,
    manager: GuestManagerProxy,
) -> Result<(), Error> {
    let (guest_endpoint, guest_server_end) = create_proxy::<GuestMarker>()
        .map_err(|err| anyhow!("failed to create guest proxy: {}", err))?;
    manager
        .connect_to_guest(guest_server_end)
        .await
        .map_err(|err| anyhow!("failed to get a connect_to_guest response: {}", err))?
        .map_err(|err| anyhow!("connect_to_guest failed with: {:?}", err))?;

    // TODO(fxbug.dev/104989): Use a different console for sending the stop command.
    let socket = guest_endpoint
        .get_console()
        .await
        .map_err(|err| anyhow!("failed to get a get_console response: {}", err))?
        .map_err(|err| anyhow!("get_console failed with: {:?}", err))?;

    println!("Sending stop command to guest");
    let command = get_graceful_stop_command(guest);
    if command.is_none() {
        // TODO(fxbug.dev/104989): Figure out how to shut down Termina.
        println!("Graceful stop not supported, rerun this command with -f to force a shutdown");
        return Ok(());
    }

    let command = command.unwrap();
    let bytes_written = socket
        .write(&command)
        .map_err(|err| anyhow!("failed to write command to socket: {}", err))?;
    if bytes_written != command.len() {
        return Err(anyhow!(
            "attempted to send command '{}', but only managed to write '{}'",
            std::str::from_utf8(&command).expect("failed to parse as utf-8"),
            std::str::from_utf8(&command[0..bytes_written]).expect("failed to parse as utf-8")
        ));
    }

    let start = fasync::Time::now();
    println!("Waiting for guest to stop");

    let unresponsive_help_delay = fasync::Time::after(zx::Duration::from_seconds(2));
    let guest_closed = guest_endpoint
        .on_closed()
        .on_timeout(unresponsive_help_delay, || Err(zx::Status::TIMED_OUT));

    match guest_closed.await {
        Ok(_) => Ok(()),
        Err(zx::Status::TIMED_OUT) => {
            println!("If the guest is unresponsive, you may force stop it by passing -f");
            guest_endpoint.on_closed().await.map(|_| ())
        }
        Err(err) => Err(err),
    }
    .map_err(|err| anyhow!("failed to wait on guest stop signal: {}", err))?;

    println!("Guest finished stopping in {}μs", (fasync::Time::now() - start).into_micros());
    Ok(())
}

async fn force_stop_guest(
    guest: arguments::GuestType,
    manager: GuestManagerProxy,
) -> Result<(), Error> {
    println!("Forcing {} to stop", guest.to_string());
    let start = fasync::Time::now();
    manager.force_shutdown_guest().await?;
    println!("Guest finished stopping in {}μs", (fasync::Time::now() - start).into_micros());

    Ok(())
}

#[cfg(test)]
mod test {
    use {
        super::*, async_utils::PollExt, fidl::endpoints::create_proxy_and_stream,
        fidl_fuchsia_virtualization::GuestManagerMarker, futures::TryStreamExt,
    };

    #[test]
    fn graceful_stop_waits_for_shutdown() {
        let mut executor = fasync::TestExecutor::new_with_fake_time().unwrap();
        executor.set_fake_time(fuchsia_async::Time::now());

        let (manager_proxy, mut manager_stream) = create_proxy_and_stream::<GuestManagerMarker>()
            .expect("failed to create GuestManager request stream");

        let fut = graceful_stop_guest(arguments::GuestType::Debian, manager_proxy);
        futures::pin_mut!(fut);

        assert!(executor.run_until_stalled(&mut fut).is_pending());

        let (guest_server_end, responder) = executor
            .run_until_stalled(&mut manager_stream.try_next())
            .expect("future should be ready")
            .unwrap()
            .unwrap()
            .into_connect_to_guest()
            .expect("received unexpected request on stream");

        responder.send(&mut Ok(())).expect("failed to send response");
        let mut guest_stream = guest_server_end.into_stream().unwrap();

        assert!(executor.run_until_stalled(&mut fut).is_pending());

        let responder = executor
            .run_until_stalled(&mut guest_stream.try_next())
            .expect("future should be ready")
            .unwrap()
            .unwrap()
            .into_get_console()
            .expect("received unexpected request on stream");

        let (client, device) =
            zx::Socket::create(zx::SocketOpts::STREAM).expect("failed to create sockets");
        responder.send(&mut Ok(client)).expect("failed to send response");

        assert!(executor.run_until_stalled(&mut fut).is_pending());

        let expected_command = get_graceful_stop_command(arguments::GuestType::Debian).unwrap();
        let mut actual_command = vec![0u8; expected_command.len()];
        assert_eq!(device.read(actual_command.as_mut_slice()).unwrap(), expected_command.len());

        // One nano past the helpful message timeout.
        executor.set_fake_time(fasync::Time::after(
            zx::Duration::from_seconds(2) + zx::Duration::from_nanos(1),
        ));

        // Waiting for CHANNEL_PEER_CLOSED timed out (printing the helpful message), but then
        // a new indefinite wait began as the channel is still not closed.
        assert!(executor.wake_expired_timers());
        assert!(executor.run_until_stalled(&mut fut).is_pending());

        // Send a CHANNEL_PEER_CLOSED to the guest proxy.
        drop(guest_stream);

        executor.run_until_stalled(&mut fut).expect("future should be ready").unwrap();
    }

    #[test]
    fn force_stop_guest_calls_stop_endpoint() {
        let mut executor = fasync::TestExecutor::new().unwrap();
        let (proxy, mut stream) = create_proxy_and_stream::<GuestManagerMarker>()
            .expect("failed to create GuestManager request stream");

        let fut = force_stop_guest(arguments::GuestType::Debian, proxy);
        futures::pin_mut!(fut);

        assert!(executor.run_until_stalled(&mut fut).is_pending());

        let responder = executor
            .run_until_stalled(&mut stream.try_next())
            .expect("future should be ready")
            .unwrap()
            .unwrap()
            .into_force_shutdown_guest()
            .expect("received unexpected request on stream");
        responder.send().expect("failed to send response");

        executor.run_until_stalled(&mut fut).expect("future should be ready").unwrap();
    }
}
