// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    fidl::endpoints::{RequestStream, ServerEnd},
    fidl_fuchsia_hardware_pty::DeviceMarker,
    fidl_fuchsia_virtualconsole::{SessionManagerRequest, SessionManagerRequestStream},
    fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::prelude::*,
    futures::{io::AsyncReadExt, select},
    pty::Pty,
    std::{cell::RefCell, rc::Rc, vec::Vec},
};

const BYTE_BUFFER_MAX_SIZE: usize = 128;

#[derive(Default)]
pub struct SessionManager {
    ptys: Rc<RefCell<Vec<Pty>>>,
}

impl SessionManager {
    pub fn new() -> Self {
        let ptys = Rc::new(RefCell::new(vec![]));
        SessionManager { ptys }
    }

    pub fn bind(&mut self, channel: fasync::Channel) {
        let ptys = Rc::clone(&self.ptys);

        fasync::Task::local(
            async move {
                let mut stream = SessionManagerRequestStream::from_channel(channel);
                while let Some(request) = stream.try_next().await? {
                    match request {
                        SessionManagerRequest::CreateSession { session, responder } => {
                            match Self::create_session(session).await {
                                Ok(pty) => {
                                    responder
                                        .send(zx::Status::OK.into_raw())
                                        .context("error sending response")?;
                                    ptys.borrow_mut().push(pty);
                                }
                                _ => {
                                    responder
                                        .send(zx::Status::INTERNAL.into_raw())
                                        .context("error sending response")?;
                                }
                            }
                        }
                        SessionManagerRequest::HasPrimaryConnected { responder } => {
                            responder
                                .send(Self::has_primary_connected())
                                .context("error sending response")?;
                        }
                    }
                }
                Ok(())
            }
            .unwrap_or_else(|e: anyhow::Error| eprintln!("{:?}", e)),
        )
        .detach();
    }

    async fn create_session(session: ServerEnd<DeviceMarker>) -> Result<Pty, Error> {
        let pty = Pty::with_server_end(session).await.expect("failed to create PTY");
        let fd = pty.try_clone_fd().expect("unable to clone PTY fd");

        fasync::Task::local(async move {
            let mut evented_fd = unsafe {
                // EventedFd::new() is unsafe because it can't guarantee the lifetime of
                // the file descriptor passed to it exceeds the lifetime of the EventedFd.
                // Since we're cloning the file when passing it in, the EventedFd
                // effectively owns that file descriptor and thus controls it's lifetime.
                fasync::net::EventedFd::new(fd).expect("failed to create evented_fd")
            };

            let mut read_buf = [0u8; BYTE_BUFFER_MAX_SIZE];
            loop {
                let mut read_fut = evented_fd.read(&mut read_buf).fuse();
                select!(
                    result = read_fut => {
                        let read_count = result.unwrap_or_else(|e: std::io::Error| {
                            println!(
                                "vc: failed to read bytes, dropping current message: {:?}",
                                e
                            );
                            0
                        });
                        if read_count > 0 {
                            // TODO(fxb/74628): Write to console.
                            println!("vc: read_count {:?}", read_count);
                        }
                    }
                );
            }
        })
        .detach();

        Ok(pty)
    }

    fn has_primary_connected() -> bool {
        // TODO(fxb/73628): Return correct state when exposed by Carnelian.
        false
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[fasync::run_singlethreaded(test)]
    async fn can_create_session() -> Result<(), Error> {
        let (_, server_end) = fidl::endpoints::create_endpoints()?;
        let _ = SessionManager::create_session(server_end).await?;
        Ok(())
    }
}
