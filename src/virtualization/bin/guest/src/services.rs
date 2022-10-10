// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::arguments::GuestType,
    anyhow::{anyhow, Context, Error},
    fidl_fuchsia_virtualization::{GuestManagerMarker, GuestManagerProxy, GuestMarker, GuestProxy},
    fuchsia_async::{self as fasync},
    fuchsia_component::client::connect_to_protocol_at_path,
    fuchsia_zircon::{self as zx, HandleBased},
    std::os::unix::{io::AsRawFd, io::FromRawFd, prelude::RawFd},
};

pub struct GuestConsole {
    input: fasync::Socket,
    output: fasync::Socket,
}

impl GuestConsole {
    pub fn new(socket: zx::Socket) -> Result<Self, Error> {
        // We duplicate the handle to enable us to handle r/w simultaneously using streams
        // This is due to a limitation on the fasync::Socket wrapper, not the socket itself
        let guest_console_read =
            fasync::Socket::from_socket(socket.duplicate_handle(zx::Rights::SAME_RIGHTS)?)?;
        let guest_console_write = fasync::Socket::from_socket(socket)?;
        Ok(GuestConsole { input: guest_console_write, output: guest_console_read })
    }

    // EventedFd implements these, as does fasync::Socket
    pub async fn run<R: futures::io::AsyncRead, W: futures::io::AsyncWrite + Unpin>(
        &self,
        stdin: R,
        mut stdout: W,
    ) -> Result<(), anyhow::Error> {
        let (mut input, output) = self.get_io_sockets();

        let console_output = async {
            futures::io::copy(output, &mut stdout).await.map(|_| ()).map_err(anyhow::Error::from)
        };

        let console_input = async {
            futures::io::copy(stdin, &mut input).await.map(|_| ()).map_err(anyhow::Error::from)
        };

        futures::future::try_select(Box::pin(console_output), Box::pin(console_input))
            .await
            .map(|_| ())
            .map_err(|e| e.factor_first().0.into())
    }

    fn get_io_sockets(&self) -> (&fasync::Socket, &fasync::Socket) {
        (&self.input, &self.output)
    }

    pub async fn run_with_stdio(&self) -> Result<(), anyhow::Error> {
        unsafe {
            self.run(&get_evented_stdio(Stdio::Stdin), &get_evented_stdio(Stdio::Stdout)).await
        }
    }
}

fn set_fd_to_unblock(raw_fd: RawFd) -> () {
    // SAFETY: This is unsafe purely due to FFI. There are no assumptions
    // about this code.
    unsafe {
        libc::fcntl(raw_fd, libc::F_SETFL, libc::fcntl(raw_fd, libc::F_GETFL) | libc::O_NONBLOCK)
    };
}

pub enum Stdio {
    Stdin,
    Stdout,
    Stderr,
}

impl AsRawFd for Stdio {
    fn as_raw_fd(&self) -> RawFd {
        match self {
            Stdio::Stdin => std::io::stdin().as_raw_fd(),
            Stdio::Stdout => std::io::stdout().as_raw_fd(),
            Stdio::Stderr => std::io::stderr().as_raw_fd(),
        }
    }
}

pub unsafe fn get_evented_stdio(stdio: Stdio) -> fasync::net::EventedFd<std::fs::File> {
    // SAFETY: This method returns an EventedFd that wraps around a file linked to std{in,out,err}
    // This method should only be called once for each type, as having multiple files that
    // are tied to a given FD can cause conflicts.

    set_fd_to_unblock(stdio.as_raw_fd());
    // SAFETY: EventedFd::new() is unsafe because it can't guarantee the lifetime of
    // the file descriptor passed to it exceeds the lifetime of the EventedFd.
    // Stdin, stdout, and stderr should remain valid for the lifetime of the program.
    // File is unsafe due to the from_raw_fd assuming it's the only owner of the
    // underlying object; this may cause memory unsafety in cases where one
    // relies on this being true, which we handle by using a reference where this matters
    //
    // Note that since File takes ownership of the fd, the fd will be closed when the EventedFd
    // is dropped. This behaviour could be avoided by using a std::io::Stdin (etc.) directly, but
    // they are buffered which may be undesirable.
    fasync::net::EventedFd::new(std::fs::File::from_raw_fd(stdio.as_raw_fd())).unwrap()
}

pub fn connect_to_manager(
    guest_type: crate::arguments::GuestType,
) -> Result<GuestManagerProxy, Error> {
    let manager = connect_to_protocol_at_path::<GuestManagerMarker>(
        format!("/svc/{}", guest_type.guest_manager_interface()).as_str(),
    )
    .context("Failed to connect to manager service")?;
    Ok(manager)
}

pub async fn connect(guest_type: GuestType) -> Result<GuestProxy, Error> {
    let guest_manager = connect_to_manager(guest_type)?;
    let (guest, guest_server_end) =
        fidl::endpoints::create_proxy::<GuestMarker>().context("Failed to create Guest")?;
    guest_manager.connect(guest_server_end).await?.map_err(|err| anyhow!("{:?}", err))?;

    Ok(guest)
}

#[cfg(test)]
mod test {
    use {
        super::*,
        fuchsia_zircon::{self as zx},
    };

    #[fasync::run_singlethreaded(test)]
    async fn services_guest_console_copies_stdin_to_device() {
        let (client_in, client_out) = zx::Socket::create(zx::SocketOpts::STREAM).unwrap();
        let client_out_dupe = fasync::Socket::from_socket(
            client_out.duplicate_handle(zx::Rights::SAME_RIGHTS).unwrap(),
        )
        .unwrap();
        let (device_in, device_out) = zx::Socket::create(zx::SocketOpts::STREAM).unwrap();

        let test_string = "Test Command";
        client_in.write(format!("{test_string}").as_bytes()).unwrap();
        // Drop our end so the guest_console hits EOF on reading
        drop(client_in);

        let guest_console = GuestConsole::new(device_in).expect("Failed to make guest console");

        guest_console
            .run(fasync::Socket::from_socket(client_out).unwrap(), client_out_dupe)
            .await
            .expect("Failed to complete!");

        let mut buffer = [0; 1024];
        let n = device_out.read(&mut buffer[..]).expect("Failed to read from socket");

        // convert buffer to string and compare equality
        assert_eq!(n, test_string.len());
        assert_eq!(String::from_utf8(buffer[..n].to_vec()).unwrap(), test_string);
    }
}
