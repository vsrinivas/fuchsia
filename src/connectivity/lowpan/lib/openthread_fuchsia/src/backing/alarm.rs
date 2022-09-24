// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;
use crate::Platform;
use fuchsia_async as fasync;
use fuchsia_zircon as zx;
//use futures::channel::mpsc as fmpsc;
use std::task::{Context, Poll};

// Number of entries in the timer wakeup buffer.
// This value was chosen somewhat arbitrarily, with the only
// requirement being that it be larger than what should happen
// during normal operation.
const TIMER_BUFFER_LEN: usize = 20;

pub(crate) struct AlarmInstance {
    pub task_alarm: Cell<Option<fasync::Task<()>>>,
    pub timer_sender: fmpsc::Sender<usize>,
}

impl AlarmInstance {
    pub(crate) fn new() -> (AlarmInstance, fmpsc::Receiver<usize>) {
        let (timer_sender, timer_receiver) = fmpsc::channel(TIMER_BUFFER_LEN);

        (AlarmInstance { task_alarm: Cell::new(None), timer_sender }, timer_receiver)
    }

    fn on_alarm_milli_get_now(&self) -> u32 {
        #[no_mangle]
        unsafe extern "C" fn otPlatAlarmMilliGetNow() -> u32 {
            // SAFETY: Must only be called from OpenThread thread,
            PlatformBacking::as_ref().alarm.on_alarm_milli_get_now()
        }
        (zx::Time::get_monotonic() - zx::Time::ZERO).into_millis() as u32
    }

    fn on_time_get(&self) -> u64 {
        #[no_mangle]
        unsafe extern "C" fn otPlatTimeGet() -> u64 {
            // SAFETY: Must only be called from OpenThread thread,
            PlatformBacking::as_ref().alarm.on_time_get()
        }
        (zx::Time::get_monotonic() - zx::Time::ZERO).into_micros() as u64
    }

    fn on_alarm_milli_start_at(&self, instance: Option<&ot::Instance>, t0: u32, dt: u32) {
        #[no_mangle]
        unsafe extern "C" fn otPlatAlarmMilliStartAt(
            instance: *mut otsys::otInstance,
            t0: u32,
            dt: u32,
        ) {
            AlarmInstance::on_alarm_milli_start_at(
                // SAFETY: Must only be called from OpenThread thread,
                &PlatformBacking::as_ref().alarm,
                // SAFETY: `instance` must be a pointer to a valid `otInstance`
                ot::Instance::ref_from_ot_ptr(instance),
                t0,
                dt,
            )
        }

        trace!("on_alarm_milli_start_at: scheduling alarm for {:?}ms after {:?}", dt, t0);

        let ot_instance_ptrval =
            instance.map(ot::Boxable::as_ot_ptr).map(|x| x as usize).unwrap_or(0usize);
        let mut timer_sender = self.timer_sender.clone();

        let future = async move {
            let now_in_millis = (zx::Time::get_monotonic() - zx::Time::ZERO).into_millis() as u32;
            let offset = ((now_in_millis - t0) as i32).min(0) as u32;
            let duration = if offset <= dt {
                Duration::from_millis((dt - offset) as u64)
            } else {
                Duration::ZERO
            };
            trace!("on_alarm_milli_start_at: helper task now waiting {:?}", duration);
            fasync::Timer::new(duration).await;
            trace!("on_alarm_milli_start_at: helper task finished waiting, now sending ot_instance_ptrval");
            timer_sender.send(ot_instance_ptrval).await.unwrap();
        };

        // Make and spawn a helper task that waits for the duration
        // and then puts the pointer value into the timer sender channel.
        // The receiver end of the channel is being serviced by Platform::process_poll,
        // which makes sure that the timer callback gets fired on the main
        // thread. The previous alarm task, if any, is cancelled.
        if let Some(old_task) = self.task_alarm.replace(Some(fasync::Task::spawn(future))) {
            // Cancel the previous/old alarm task, if any.
            old_task.cancel().now_or_never();
        }
    }

    fn on_alarm_milli_stop(&self, _instance: Option<&ot::Instance>) {
        #[no_mangle]
        unsafe extern "C" fn otPlatAlarmMilliStop(instance: *mut otsys::otInstance) {
            AlarmInstance::on_alarm_milli_stop(
                // SAFETY: Must only be called from OpenThread thread,
                &PlatformBacking::as_ref().alarm,
                // SAFETY: `instance` must be a pointer to a valid `otInstance`
                ot::Instance::ref_from_ot_ptr(instance),
            )
        }

        if let Some(old_task) = self.task_alarm.take() {
            trace!("on_alarm_milli_stop: Alarm cancelled");

            // Cancel the previous/old alarm task, if any.
            old_task.cancel().now_or_never();
        }
    }

    fn on_alarm_fired(&self, instance: &ot::Instance, value: usize) {
        trace!("on_alarm_fired");

        let instance_ptr = instance.as_ot_ptr();
        assert_eq!(instance_ptr as usize, value, "Got wrong pointer from timer receiver");

        // SAFETY: Must be called with a valid pointer to otInstance,
        //         must also only be called from the main OpenThread thread,
        //         which is a guarantee of this method.
        unsafe {
            if otsys::otPlatDiagModeGet() {
                otsys::otPlatDiagAlarmFired(instance_ptr);
            }

            otsys::otPlatAlarmMilliFired(instance_ptr);
        }
    }
}

impl Platform {
    pub(crate) fn process_poll_alarm(&mut self, instance: &ot::Instance, cx: &mut Context<'_>) {
        while let Poll::Ready(Some(value)) = self.timer_receiver.poll_next_unpin(cx) {
            // SAFETY: Guaranteed to only be called from the OpenThread thread.
            unsafe { PlatformBacking::as_ref() }.alarm.on_alarm_fired(instance, value);
        }
    }
}
