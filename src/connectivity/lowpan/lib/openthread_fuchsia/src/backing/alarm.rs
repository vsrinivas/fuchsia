// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;

impl PlatformBacking {
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
}
