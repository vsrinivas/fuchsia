// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;

impl PlatformBacking {
    fn on_send_spinel_frame_to_rcp(&self, _instance: Option<&ot::Instance>, buffer: &[u8]) {
        #[no_mangle]
        unsafe extern "C" fn platformCallbackSendOneFrameToRadio(
            instance: *mut otsys::otInstance,
            buffer_ptr: *const u8,
            len: usize,
        ) {
            PlatformBacking::on_send_spinel_frame_to_rcp(
                // SAFETY: Must only be called from OpenThread thread,
                PlatformBacking::as_ref(),
                // SAFETY: `instance` must be a pointer to a valid `otInstance`
                ot::Instance::ref_from_ot_ptr(instance),
                // SAFETY: `buffer_ptr` must point to a `u8` buffer at least `len` bytes long.
                std::slice::from_raw_parts(buffer_ptr, len),
            )
        }

        debug!("> {:?}", SpinelFrameRef::try_unpack_from_slice(buffer));
        self.ot_to_rcp_sender.borrow_mut().send(buffer.to_vec()).expect("ot_to_rcp_sender::send");
    }

    fn on_recv_wait_spinel_frame_from_rcp<'a>(
        &self,
        _instance: Option<&ot::Instance>,
        buffer: &'a mut [u8],
        duration: Duration,
    ) -> usize {
        #[no_mangle]
        unsafe extern "C" fn platformCallbackWaitForFrameFromRadio(
            instance: *mut otsys::otInstance,
            buffer_ptr: *mut u8,
            len_max: usize,
            timeout_us: u64,
        ) -> usize {
            PlatformBacking::on_recv_wait_spinel_frame_from_rcp(
                // SAFETY: Must only be called from OpenThread thread,
                PlatformBacking::as_ref(),
                // SAFETY: `instance` must be a pointer to a valid `otInstance`
                ot::Instance::ref_from_ot_ptr(instance),
                // SAFETY: `buffer_ptr` must point to a mutable `u8` buffer at least `len` bytes long.
                std::slice::from_raw_parts_mut(buffer_ptr, len_max),
                Duration::from_micros(timeout_us),
            )
        }
        #[no_mangle]
        unsafe extern "C" fn platformCallbackFetchQueuedFrameFromRadio(
            instance: *mut otsys::otInstance,
            buffer_ptr: *mut u8,
            len_max: usize,
        ) -> usize {
            PlatformBacking::on_recv_wait_spinel_frame_from_rcp(
                // SAFETY: Must only be called from OpenThread thread,
                PlatformBacking::as_ref(),
                // SAFETY: `instance` must be a pointer to a valid `otInstance`
                ot::Instance::ref_from_ot_ptr(instance),
                // SAFETY: `buffer_ptr` must point to a mutable `u8` buffer at least `len` bytes long.
                std::slice::from_raw_parts_mut(buffer_ptr, len_max),
                Duration::from_micros(0),
            )
        }

        if !duration.is_zero() {
            trace!("on_recv_wait_spinel_frame_from_rcp: Waiting {:?} for spinel frame", duration);
        }
        match self.rcp_to_ot_receiver.borrow_mut().recv_timeout(duration) {
            Ok(vec) => {
                debug!("< {:?}", SpinelFrameRef::try_unpack_from_slice(vec.as_slice()));
                buffer[0..vec.len()].clone_from_slice(&vec);
                vec.len()
            }
            Err(mpsc::RecvTimeoutError::Timeout) => {
                if !duration.is_zero() {
                    trace!("on_recv_wait_spinel_frame_from_rcp: Timeout");
                }
                0
            }
            Err(mpsc::RecvTimeoutError::Disconnected) => panic!("Spinel Thread Disconnected"),
        }
    }
}
