// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context as _, Error},
    cstr::cstr,
    fidl::endpoints::{DiscoverableProtocolMarker as _, ServerEnd},
    fidl_fuchsia_hardware_pty::{DeviceMarker, DeviceProxy, WindowSize},
    fuchsia_async as fasync, fuchsia_trace as ftrace,
    fuchsia_zircon::{self as zx, HandleBased, ProcessInfo, ProcessInfoFlags},
    std::{ffi::CStr, fs::File, os::unix::io::AsRawFd},
};

/// An object used for interacting with the shell
pub struct Pty {
    // The server side file descriptor. This file is safe to clone.
    server_pty: File,

    // The running shell process. This process will be closed when the
    // Pty goes out of scope so there is no need to explicitly close it.
    shell_process: Option<zx::Process>,
}

impl Pty {
    /// Creates a new instance of the Pty which must later be spawned.
    pub fn new() -> Result<Self, Error> {
        let server_pty = Pty::open_server_pty()?;
        let shell_process = None;
        Ok(Pty { server_pty, shell_process })
    }

    /// Creates a new instance of the Pty using an existing server end.
    ///
    /// The user must call resize to give the client a valid window size
    /// before it will respond.
    pub async fn with_server_end(server_end: ServerEnd<DeviceMarker>) -> Result<Self, Error> {
        let server_pty = Pty::open_server_pty()?;
        let shell_process = None;
        Pty::open_client(&server_pty, server_end).await?;
        Ok(Pty { server_pty, shell_process })
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
        &mut self,
        command: Option<&CStr>,
        environ: Option<&[&CStr]>,
    ) -> Result<(), Error> {
        let command = command.unwrap_or(&cstr!("/boot/bin/sh"));
        self.spawn_with_argv(command, &[command], environ).await
    }

    pub async fn spawn_with_argv(
        &mut self,
        command: &CStr,
        argv: &[&CStr],
        environ: Option<&[&CStr]>,
    ) -> Result<(), Error> {
        if self.shell_process.is_some() {
            return Ok(());
        }

        ftrace::duration!("pty", "Pty:spawn");

        self.shell_process = Some(
            Pty::launch_shell(&self.server_pty, command, argv, environ)
                .await
                .context("launch shell process")?,
        );
        Ok(())
    }

    /// Returns the shell process info, if available.
    pub fn shell_process_info(&self) -> Option<ProcessInfo> {
        self.shell_process.as_ref().and_then(|p| match p.info() {
            Ok(info) => Some(info),
            Err(status) => {
                eprintln!("Failed to check shell process info, got status {:?}.", status);
                None
            }
        })
    }

    /// Checks that the shell process has been started and has not exited.
    pub fn is_shell_process_running(&self) -> bool {
        self.shell_process_info()
            .map(|info| {
                let flags = ProcessInfoFlags::from_bits(info.flags).unwrap();
                flags.contains(zx::ProcessInfoFlags::STARTED)
                    && !flags.contains(ProcessInfoFlags::EXITED)
            })
            .unwrap_or_default()
    }

    /// Attempts to clone the server side of the file descriptor.
    pub fn try_clone_fd(&self) -> Result<File, Error> {
        let fd = self.server_pty.try_clone()?;
        Ok(fd)
    }

    /// Sends a message to the shell that the window has been resized.
    pub async fn resize(&self, window_size: WindowSize) -> Result<(), Error> {
        Pty::set_window_size(&self.server_pty, window_size).await?;
        Ok(())
    }

    /// Opens the initial server side of the pty.
    fn open_server_pty() -> Result<File, Error> {
        ftrace::duration!("pty", "Pty:open_server_pty");
        let server_pty = std::fs::File::options()
            .read(true)
            .write(true)
            .open("/svc/".to_string() + DeviceMarker::PROTOCOL_NAME)
            .context("failed to open pty service")?;
        let fd = server_pty.as_raw_fd();
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
        Ok(server_pty)
    }

    /// Launches the shell process by creating the client side of the pty and then spawning the
    /// shell.
    async fn launch_shell(
        server_pty: &File,
        command: &CStr,
        argv: &[&CStr],
        environ: Option<&[&CStr]>,
    ) -> Result<zx::Process, Error> {
        ftrace::duration!("pty", "Pty:launch_shell");
        let client_pty =
            Pty::open_client_pty(server_pty).await.context("unable to create client_pty")?;
        let process = Pty::spawn_shell_process(client_pty, command, argv, environ)
            .context("unable to spawn shell process")?;

        Ok(process)
    }

    /// Creates a File which is suitable to use as the client side of the Pty.
    async fn open_client_pty(server_pty: &File) -> Result<File, Error> {
        ftrace::duration!("pty", "Pty:open_client_pty");
        let (client_end, server_end) = fidl::endpoints::create_endpoints()?;

        // Open a client Pty device.
        Self::open_client(server_pty, server_end).await?;

        // convert the client side into a file descriptor. This must be called
        // after the server side has been established.
        let client_pty =
            fdio::create_fd(client_end.into()).context("failed to create FD from client PTY")?;

        Ok(client_pty)
    }

    /// Open a client Pty device. `server_end` should be a handle
    /// to one endpoint of a channel that (on success) will become an open
    /// connection to the newly created device.
    async fn open_client(
        server_pty: &File,
        server_end: ServerEnd<DeviceMarker>,
    ) -> Result<(), Error> {
        ftrace::duration!("pty", "Pty:open_client");

        let server_pty_channel = fdio::clone_channel(server_pty)
            .context("failed to clone channel from server PTY FD")?;
        let server_pty_fidl_channel = fasync::Channel::from_channel(server_pty_channel)
            .context("failed to create FIDL channel from zircon channel")?;

        let device_proxy = DeviceProxy::new(server_pty_fidl_channel);
        let () = device_proxy
            .open_client(0, server_end)
            .await
            .map(zx::Status::ok)
            .context("failed to interact with PTY device")?
            .context("failed to attach PTY to channel")?;

        Ok(())
    }

    /// spawns the shell and transfers the client pty to the process.
    fn spawn_shell_process(
        client_pty: File,
        command: &CStr,
        argv: &[&CStr],
        environ: Option<&[&CStr]>,
    ) -> Result<zx::Process, Error> {
        ftrace::duration!("pty", "Pty:spawn_shell_process");
        let process = fdio::spawn_etc(
            &zx::Job::from_handle(zx::Handle::invalid()),
            fdio::SpawnOptions::CLONE_ALL - fdio::SpawnOptions::CLONE_STDIO,
            command,
            argv,
            environ,
            &mut [fdio::SpawnAction::transfer_fd(client_pty, fdio::SpawnAction::USE_FOR_STDIO)],
        )
        .map_err(|e| format_err!("failed to spawn shell: {:?}", e))?;

        Ok(process)
    }

    pub async fn set_window_size(
        server_pty: &File,
        mut window_size: WindowSize,
    ) -> Result<(), Error> {
        ftrace::duration!("pty", "Pty:set_window_size");
        let server_pty_channel = fdio::clone_channel(server_pty)
            .context("failed to clone channel from server PTY FD")?;
        let server_pty_fidl_channel = fasync::Channel::from_channel(server_pty_channel)
            .context("failed to create FIDL channel from zircon channel")?;
        let device_proxy = DeviceProxy::new(server_pty_fidl_channel);

        device_proxy.set_window_size(&mut window_size).await.context("Unable to resize window")?;
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use {futures::io::AsyncWriteExt, zx::AsHandleRef};

    #[fasync::run_singlethreaded(test)]
    async fn can_create_pty() -> Result<(), Error> {
        let _ = Pty::new()?;
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn can_create_pty_with_server_end() -> Result<(), Error> {
        let (_, server_end) = fidl::endpoints::create_endpoints()?;
        let _ = Pty::with_server_end(server_end).await?;
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn can_open_client_pty() -> Result<(), Error> {
        let server_pty = Pty::open_server_pty()?;
        let client_pty = Pty::open_client_pty(&server_pty).await?;
        assert!(client_pty.as_raw_fd() > 0);

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn can_open_server_pty() -> Result<(), Error> {
        let server_pty = Pty::open_server_pty()?;
        assert!(server_pty.as_raw_fd() > 0);

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn can_spawn_shell_process() -> Result<(), Error> {
        let server_pty = Pty::open_server_pty()?;
        let cmd = cstr!("/pkg/bin/sh");
        let process = Pty::launch_shell(&server_pty, &cmd, &[&cmd], None).await?;

        let mut started = false;
        if let Ok(info) = process.info() {
            started = ProcessInfoFlags::from_bits(info.flags)
                .unwrap()
                .contains(zx::ProcessInfoFlags::STARTED);
        }

        assert_eq!(started, true);

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn shell_process_is_spawned() -> Result<(), Error> {
        let pty = spawn_pty().await?;

        let mut started = false;
        if let Some(process) = &pty.shell_process {
            let info = process.info().unwrap();
            started = ProcessInfoFlags::from_bits(info.flags)
                .unwrap()
                .contains(zx::ProcessInfoFlags::STARTED);
        }
        assert_eq!(started, true);

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn unspawned_shell_process_is_not_running() -> Result<(), Error> {
        let window_size = WindowSize { width: 300 as u32, height: 300 as u32 };
        let pty = Pty::new().unwrap();
        pty.resize(window_size).await?;

        assert_eq!(pty.is_shell_process_running(), false);
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn spawned_shell_process_is_running() -> Result<(), Error> {
        let pty = spawn_pty().await?;

        assert_eq!(pty.is_shell_process_running(), true);
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn exited_shell_process_is_not_running() -> Result<(), Error> {
        let window_size = WindowSize { width: 300 as u32, height: 300 as u32 };
        let mut pty = Pty::new().unwrap();

        // While argv[0] is usually the executable path, this particular program expects it to be
        // an integer which is then parsed and returned as the status code.
        pty.spawn_with_argv(&cstr!("/pkg/bin/exit_with_code_util"), &[cstr!("42")], None).await?;
        pty.resize(window_size).await?;

        // Since these tests don't seem to timeout automatically, we must
        // specify a deadline and cannot simply rely on fasync::OnSignals.
        pty.shell_process
            .as_ref()
            .unwrap()
            .wait_handle(
                zx::Signals::PROCESS_TERMINATED,
                zx::Time::after(zx::Duration::from_seconds(60)),
            )
            .expect("shell process did not exit in time");

        assert_eq!(pty.is_shell_process_running(), false);
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn can_write_to_shell() -> Result<(), Error> {
        let pty = spawn_pty().await?;
        // EventedFd::new() is unsafe because it can't guarantee the lifetime of
        // the file descriptor passed to it exceeds the lifetime of the EventedFd.
        // Since we're cloning the file when passing it in, the EventedFd
        // effectively owns that file descriptor and thus controls it's lifetime.
        let mut evented_fd = unsafe { fasync::net::EventedFd::new(pty.try_clone_fd()?)? };

        evented_fd.write_all("a".as_bytes()).await?;

        Ok(())
    }

    #[ignore] // TODO(63868): until we figure out why this test is flaking.
    #[fasync::run_singlethreaded(test)]
    async fn shell_process_is_not_running_after_writing_exit() -> Result<(), Error> {
        let pty = spawn_pty().await?;
        // EventedFd::new() is unsafe because it can't guarantee the lifetime of
        // the file descriptor passed to it exceeds the lifetime of the EventedFd.
        // Since we're cloning the file when passing it in, the EventedFd
        // effectively owns that file descriptor and thus controls it's lifetime.
        let mut evented_fd = unsafe { fasync::net::EventedFd::new(pty.try_clone_fd()?)? };

        evented_fd.write_all("exit\n".as_bytes()).await?;

        // Since these tests don't seem to timeout automatically, we must
        // specify a deadline and cannot simply rely on fasync::OnSignals.
        pty.shell_process
            .as_ref()
            .unwrap()
            .wait_handle(
                zx::Signals::PROCESS_TERMINATED,
                zx::Time::after(zx::Duration::from_seconds(60)),
            )
            .expect("shell process did not exit in time");

        assert_eq!(pty.is_shell_process_running(), false);

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn can_resize_window() -> Result<(), Error> {
        let pty = spawn_pty().await?;
        pty.resize(WindowSize { width: 400, height: 400 }).await?;
        Ok(())
    }

    async fn spawn_pty() -> Result<Pty, Error> {
        let window_size = WindowSize { width: 300 as u32, height: 300 as u32 };
        let mut pty = Pty::new().unwrap();
        pty.spawn(Some(&cstr!("/pkg/bin/sh")), None).await?;
        pty.resize(window_size).await?;
        Ok(pty)
    }
}
