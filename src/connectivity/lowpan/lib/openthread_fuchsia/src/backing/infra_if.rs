// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;
use crate::{platformInfraIfInit, platformInfraIfOnReceiveIcmp6Msg};
use fuchsia_async::net::EventedFd;
use std::os::unix::io::AsRawFd;
use std::task::{Context, Poll};

pub struct OtRawSocket {
    fd: i32,
}
impl AsRawFd for OtRawSocket {
    fn as_raw_fd(&self) -> i32 {
        self.fd
    }
}

impl From<i32> for OtRawSocket {
    fn from(file_desc: i32) -> Self {
        OtRawSocket { fd: file_desc }
    }
}

pub(crate) struct InfraIfInstance {
    event_fd: Option<fasync::net::EventedFd<OtRawSocket>>,
}

impl InfraIfInstance {
    pub fn new(infra_if_idx: ot::NetifIndex) -> Option<InfraIfInstance> {
        if infra_if_idx == 0 {
            info!("InfraIfInstance failed to initialize: invalid infra_if_idx");
            return None;
        }
        let raw_fd_int: i32 = unsafe { platformInfraIfInit(infra_if_idx) };
        if raw_fd_int <= 0 {
            info!("InfraIfInstance failed to initialize: invalid raw_fd_int");
            return None;
        }
        let raw_fd: OtRawSocket = raw_fd_int.into();
        info!("InfraIfInstance initialized");
        Some(InfraIfInstance { event_fd: Some(unsafe { EventedFd::new(raw_fd).unwrap() }) })
    }

    pub fn poll(&self, instance: &ot::Instance, cx: &mut Context<'_>) {
        if let Some(event_fd) = self.event_fd.as_ref() {
            match event_fd.poll_readable(cx) {
                Poll::Ready(Ok(())) => unsafe {
                    platformInfraIfOnReceiveIcmp6Msg(instance.as_ot_ptr())
                },
                Poll::Ready(Err(x)) => {
                    error!("InfraIfInstance: Error poll readable from raw socket: {:?}", x);
                }
                _ => {}
            }
        }
    }
}
