// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    cstr::cstr,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_hardware_pty::{DeviceMarker, DeviceProxy, WindowSize},
    fuchsia_component::client::connect_to_protocol,
    fuchsia_trace as ftrace,
    fuchsia_zircon::{self as zx, HandleBased as _, ProcessInfo, ProcessInfoFlags},
    std::{ffi::CStr, fs::File},
};

/// An object used for interacting with the shell.
#[derive(Clone)]
pub struct ServerPty {
    // The server side pty connection.
    proxy: DeviceProxy,
}

pub struct ShellProcess {
    pub pty: ServerPty,

    // The running shell process. This process will be closed when the
    // Pty goes out of scope so there is no need to explicitly close it.
    process: zx::Process,
}

impl ServerPty {
    /// Creates a new instance of the Pty which must later be spawned.
    pub fn new() -> Result<Self, Error> {
        ftrace::duration!("pty", "Pty:new");
        let proxy =
            connect_to_protocol::<DeviceMarker>().context("could not connect to pty service")?;
        Ok(Self { proxy })
    }

    /// Spawns the Pty.
    ///
    /// If no command is provided the default /boot/bin/sh will be used.
    ///
    /// After calling this method the user must call resize to give the process a
    /// valid window size before it will respond.
    ///
    /// The launched process will close when the Pty is dropped so you do not need to
    /// explicitly close it.
    pub async fn spawn(
        self,
        command: Option<&CStr>,
        environ: Option<&[&CStr]>,
    ) -> Result<ShellProcess, Error> {
        let command = command.unwrap_or(&cstr!("/boot/bin/sh"));
        self.spawn_with_argv(command, &[command], environ).await
    }

    pub async fn spawn_with_argv(
        self,
        command: &CStr,
        argv: &[&CStr],
        environ: Option<&[&CStr]>,
    ) -> Result<ShellProcess, Error> {
        ftrace::duration!("pty", "Pty:spawn");
        let client_pty = self.open_client_pty().await.context("unable to create client_pty")?;
        let process = match fdio::spawn_etc(
            &zx::Job::from_handle(zx::Handle::invalid()),
            fdio::SpawnOptions::CLONE_ALL - fdio::SpawnOptions::CLONE_STDIO,
            command,
            argv,
            environ,
            &mut [fdio::SpawnAction::transfer_fd(client_pty, fdio::SpawnAction::USE_FOR_STDIO)],
        ) {
            Ok(process) => process,
            Err((status, reason)) => {
                return Err(status).context(format!("failed to spawn shell: {}", reason));
            }
        };

        Ok(ShellProcess { pty: self, process })
    }

    /// Attempts to clone the server side of the file descriptor.
    pub fn try_clone_fd(&self) -> Result<File, Error> {
        use std::os::fd::AsRawFd as _;

        let Self { proxy } = self;
        let (client_end, server_end) = fidl::endpoints::create_endpoints()?;
        let () = proxy.clone(fidl_fuchsia_io::OpenFlags::CLONE_SAME_RIGHTS, server_end)?;
        let file = fdio::create_fd::<File>(client_end.into())
            .context("failed to create FD from server PTY")?;
        let fd = file.as_raw_fd();
        let previous = {
            let res = unsafe { libc::fcntl(fd, libc::F_GETFL) };
            if res == -1 {
                Err(std::io::Error::last_os_error()).context("failed to get file status flags")
            } else {
                Ok(res)
            }
        }?;
        let new = previous | libc::O_NONBLOCK;
        if new != previous {
            let res = unsafe { libc::fcntl(fd, libc::F_SETFL, new) };
            let () = if res == -1 {
                Err(std::io::Error::last_os_error()).context("failed to set file status flags")
            } else {
                Ok(())
            }?;
        }
        Ok(file)
    }

    /// Sends a message to the shell that the window has been resized.
    pub async fn resize(&self, mut window_size: WindowSize) -> Result<(), Error> {
        ftrace::duration!("pty", "Pty:resize");
        let Self { proxy } = self;
        let () = proxy
            .set_window_size(&mut window_size)
            .await
            .map(zx::Status::ok)
            .context("unable to call resize window")?
            .context("failed to resize window")?;
        Ok(())
    }

    /// Creates a File which is suitable to use as the client side of the Pty.
    async fn open_client_pty(&self) -> Result<File, Error> {
        ftrace::duration!("pty", "Pty:open_client_pty");
        let (client_end, server_end) = fidl::endpoints::create_endpoints()?;
        let () = self.open_client(server_end).await.context("failed to open client")?;
        fdio::create_fd(client_end.into()).context("failed to create FD from client PTY")
    }

    /// Open a client Pty device. `server_end` should be a handle
    /// to one endpoint of a channel that (on success) will become an open
    /// connection to the newly created device.
    pub async fn open_client(&self, server_end: ServerEnd<DeviceMarker>) -> Result<(), Error> {
        let Self { proxy } = self;
        ftrace::duration!("pty", "Pty:open_client");

        let () = proxy
            .open_client(0, server_end)
            .await
            .map(zx::Status::ok)
            .context("failed to interact with PTY device")?
            .context("failed to attach PTY to channel")?;

        Ok(())
    }
}

impl ShellProcess {
    /// Returns the shell process info, if available.
    pub fn process_info(&self) -> Result<ProcessInfo, Error> {
        let Self { pty: _, process } = self;
        process.info().context("failed to get process info")
    }

    /// Checks that the shell process has been started and has not exited.
    pub fn is_running(&self) -> bool {
        self.process_info()
            .map(|info| {
                let flags = ProcessInfoFlags::from_bits(info.flags).unwrap();
                flags.contains(zx::ProcessInfoFlags::STARTED)
                    && !flags.contains(ProcessInfoFlags::EXITED)
            })
            .unwrap_or_default()
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fuchsia_async as fasync,
        std::os::unix::io::AsRawFd as _,
        {futures::io::AsyncWriteExt as _, zx::AsHandleRef as _},
    };

    #[fasync::run_singlethreaded(test)]
    async fn can_create_pty() -> Result<(), Error> {
        let _ = ServerPty::new()?;
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn can_open_client_pty() -> Result<(), Error> {
        let server_pty = ServerPty::new()?;
        let client_pty = server_pty.open_client_pty().await?;
        assert!(client_pty.as_raw_fd() > 0);

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn can_spawn_shell_process() -> Result<(), Error> {
        let server_pty = ServerPty::new()?;
        let cmd = cstr!("/pkg/bin/sh");
        let process = server_pty.spawn_with_argv(&cmd, &[cmd], None).await?;

        let mut started = false;
        if let Ok(info) = process.process_info() {
            started = ProcessInfoFlags::from_bits(info.flags)
                .unwrap()
                .contains(zx::ProcessInfoFlags::STARTED);
        }

        assert_eq!(started, true);

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn shell_process_is_spawned() -> Result<(), Error> {
        let process = spawn_pty().await?;

        let info = process.process_info().unwrap();
        assert!(ProcessInfoFlags::from_bits(info.flags)
            .unwrap()
            .contains(zx::ProcessInfoFlags::STARTED));

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn spawned_shell_process_is_running() -> Result<(), Error> {
        let process = spawn_pty().await?;

        assert!(process.is_running());
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn exited_shell_process_is_not_running() -> Result<(), Error> {
        let window_size = WindowSize { width: 300 as u32, height: 300 as u32 };
        let pty = ServerPty::new().unwrap();

        // While argv[0] is usually the executable path, this particular program expects it to be
        // an integer which is then parsed and returned as the status code.
        let process = pty
            .spawn_with_argv(&cstr!("/pkg/bin/exit_with_code_util"), &[cstr!("42")], None)
            .await?;
        let () = process.pty.resize(window_size).await?;

        // Since these tests don't seem to timeout automatically, we must
        // specify a deadline and cannot simply rely on fasync::OnSignals.
        process
            .process
            .wait_handle(
                zx::Signals::PROCESS_TERMINATED,
                zx::Time::after(zx::Duration::from_seconds(60)),
            )
            .expect("shell process did not exit in time");

        assert!(!process.is_running());
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn can_write_to_shell() -> Result<(), Error> {
        let process = spawn_pty().await?;
        // EventedFd::new() is unsafe because it can't guarantee the lifetime of
        // the file descriptor passed to it exceeds the lifetime of the EventedFd.
        // Since we're cloning the file when passing it in, the EventedFd
        // effectively owns that file descriptor and thus controls it's lifetime.
        let mut evented_fd = unsafe { fasync::net::EventedFd::new(process.pty.try_clone_fd()?)? };

        evented_fd.write_all("a".as_bytes()).await?;

        Ok(())
    }

    #[ignore] // TODO(63868): until we figure out why this test is flaking.
    #[fasync::run_singlethreaded(test)]
    async fn shell_process_is_not_running_after_writing_exit() -> Result<(), Error> {
        let process = spawn_pty().await?;
        // EventedFd::new() is unsafe because it can't guarantee the lifetime of
        // the file descriptor passed to it exceeds the lifetime of the EventedFd.
        // Since we're cloning the file when passing it in, the EventedFd
        // effectively owns that file descriptor and thus controls it's lifetime.
        let mut evented_fd = unsafe { fasync::net::EventedFd::new(process.pty.try_clone_fd()?)? };

        evented_fd.write_all("exit\n".as_bytes()).await?;

        // Since these tests don't seem to timeout automatically, we must
        // specify a deadline and cannot simply rely on fasync::OnSignals.
        process
            .process
            .wait_handle(
                zx::Signals::PROCESS_TERMINATED,
                zx::Time::after(zx::Duration::from_seconds(60)),
            )
            .expect("shell process did not exit in time");

        assert!(!process.is_running());

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn can_resize_window() -> Result<(), Error> {
        let process = spawn_pty().await?;
        let () = process.pty.resize(WindowSize { width: 400, height: 400 }).await?;
        Ok(())
    }

    async fn spawn_pty() -> Result<ShellProcess, Error> {
        let window_size = WindowSize { width: 300 as u32, height: 300 as u32 };
        let pty = ServerPty::new()?;
        let process =
            pty.spawn(Some(&cstr!("/pkg/bin/sh")), None).await.context("failed to spawn")?;
        let () = process.pty.resize(window_size).await?;
        Ok(process)
    }
}
