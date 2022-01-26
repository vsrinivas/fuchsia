// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use futures::prelude::*;
use openthread::prelude::*;
use spinel_pack::prelude::*;

use fuchsia_async as fasync;
use futures::channel::mpsc as fmpsc;
use log::*;
use lowpan_driver_common::spinel::*;
use std::cell::{Cell, RefCell};
use std::sync::mpsc;
use std::time::Duration;

pub(super) struct PlatformBacking {
    pub(super) ot_to_rcp_sender: RefCell<mpsc::Sender<Vec<u8>>>,
    pub(super) rcp_to_ot_receiver: RefCell<mpsc::Receiver<Vec<u8>>>,
    pub(super) task_alarm: Cell<Option<fasync::Task<()>>>,
    pub(super) timer_sender: fmpsc::Sender<usize>,
}

impl PlatformBacking {
    // SAFETY: Unsafe because the type system cannot enforce thread safety on globals.
    //         Caller should ensure that no other calls in this section are being
    //         simultaneously made on other threads.
    unsafe fn glob() -> &'static mut Option<PlatformBacking> {
        static mut SINGLETON_BACKING: Option<PlatformBacking> = None;
        &mut SINGLETON_BACKING
    }

    // SAFETY: Unsafe because the type system cannot enforce thread safety on globals.
    //         Caller should ensure that no other calls in this section are being
    //         simultaneously made on other threads.
    unsafe fn as_ref() -> &'static PlatformBacking {
        Self::glob().as_ref().expect("Platform is uninitialized")
    }

    // SAFETY: Unsafe because the type system cannot enforce thread safety on globals.
    //         Caller should ensure that no other calls in this section are being
    //         simultaneously made on other threads.
    pub(super) unsafe fn set_singleton(backing: PlatformBacking) {
        assert!(Self::glob().replace(backing).is_none(), "Tried to make two Platform instances");
    }

    // SAFETY: Must only be called from Drop.
    pub(super) unsafe fn drop_singleton() {
        // SAFETY: When we are dropped, we can safely assume no other simultaneous calls are
        //         being made on other threads.
        assert!(Self::glob().take().is_some(), "Tried to drop singleton that was never allocated");
    }
}

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

    fn on_post_delayed_alarm_task(&self, instance: Option<&ot::Instance>, duration: Duration) {
        #[no_mangle]
        unsafe extern "C" fn platformCallbackPostDelayedAlarmTask(
            instance: *mut otsys::otInstance,
            delay_ns: u64,
        ) {
            PlatformBacking::on_post_delayed_alarm_task(
                // SAFETY: Must only be called from OpenThread thread,
                PlatformBacking::as_ref(),
                // SAFETY: `instance` must be a pointer to a valid `otInstance`
                ot::Instance::ref_from_ot_ptr(instance),
                Duration::from_nanos(delay_ns),
            )
        }

        trace!("on_post_delayed_alarm_task: scheduling alarm in {:?}", duration);

        let ot_instance_ptrval =
            instance.map(ot::Boxable::as_ot_ptr).map(|x| x as usize).unwrap_or(0usize);
        let mut timer_sender = self.timer_sender.clone();

        // Make and spawn a helper task that waits for the duration
        // and then puts the pointer value into the timer sender channel.
        // The receiver end of the channel is being serviced by Platform::process_poll,
        // which makes sure that the timer callback gets fired on the main
        // thread. The previous alarm task, if any, is cancelled.
        if let Some(old_task) = self.task_alarm.replace(Some(fasync::Task::spawn(async move {
            trace!("on_post_delayed_alarm_task: helper task now waiting {:?}", duration);
            fasync::Timer::new(duration).await;
            trace!("on_post_delayed_alarm_task: helper task finished waiting, now sending ot_instance_ptrval");
            timer_sender.send(ot_instance_ptrval).await.unwrap();
        }))) {
            // Cancel the previous/old alarm task, if any.
            old_task.cancel().now_or_never();
        }
    }

    fn on_plat_reset(&self, instance: Option<&ot::Instance>) {
        #[no_mangle]
        unsafe extern "C" fn otPlatReset(instance: *mut otsys::otInstance) {
            PlatformBacking::on_plat_reset(
                // SAFETY: Must only be called from OpenThread thread,
                PlatformBacking::as_ref(),
                // SAFETY: `instance` must be a pointer to a valid `otInstance`,
                //         which is guaranteed by the caller.
                ot::Instance::ref_from_ot_ptr(instance),
            )
        }

        if let Some(instance) = instance {
            instance.thread_set_enabled(false).unwrap();
            instance.ip6_set_enabled(false).unwrap();
        }

        info!("on_plat_reset for {:?}", instance);
    }
}
