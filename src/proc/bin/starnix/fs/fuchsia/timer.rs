// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon as zx;
use fuchsia_zircon::AsHandleRef;
use zerocopy::AsBytes;

use crate::fs::*;
use crate::lock::Mutex;
use crate::mm::MemoryAccessorExt;
use crate::task::*;
use crate::types::*;

/// A `TimerFile` represents a file created by `timerfd_create`.
///
/// Clients can read the number of times the timer has triggered from the file. The file supports
/// blocking reads, waiting for the timer to trigger.
pub struct TimerFile {
    /// The timer that is used to wait for blocking reads.
    timer: zx::Timer,

    /// The deadline (`zx::Time`) for the next timer trigger, and the associated interval
    /// (`zx::Duration`).
    ///
    /// When the file is read, the deadline is recomputed based on the current time and the set
    /// interval. If the interval is 0, `self.timer` is cancelled after the file is read.
    deadline_interval: Mutex<(zx::Time, zx::Duration)>,
}

impl TimerFile {
    /// Creates a new anonymous `TimerFile` in `kernel`.
    ///
    /// Returns an error if the `zx::Timer` could not be created.
    pub fn new_file(current_task: &CurrentTask, flags: OpenFlags) -> Result<FileHandle, Errno> {
        let timer = zx::Timer::create().map_err(|status| from_status_like_fdio!(status))?;

        Ok(Anon::new_file(
            current_task,
            Box::new(TimerFile {
                timer,
                deadline_interval: Mutex::new((zx::Time::default(), zx::Duration::default())),
            }),
            flags,
        ))
    }

    /// Returns the current `itimerspec` for the file.
    ///
    /// The returned `itimerspec.it_value` contains the amount of time remaining until the
    /// next timer trigger.
    pub fn current_timer_spec(&self) -> itimerspec {
        let (deadline, interval) = *self.deadline_interval.lock();

        let now = zx::Time::get_monotonic();
        let remaining_time = if interval == zx::Duration::default() && deadline <= now {
            timespec_from_duration(zx::Duration::default())
        } else {
            timespec_from_duration(deadline - now)
        };

        itimerspec { it_interval: timespec_from_duration(interval), it_value: remaining_time }
    }

    /// Sets the `itimerspec` for the timer, which will either update the associated `zx::Timer`'s
    /// scheduled trigger or cancel the timer.
    ///
    /// Returns the previous `itimerspec` on success.
    pub fn set_timer_spec(&self, timer_spec: itimerspec, flags: u32) -> Result<itimerspec, Errno> {
        let mut deadline_interval = self.deadline_interval.lock();
        let (old_deadline, old_interval) = *deadline_interval;

        let now = zx::Time::get_monotonic();

        let new_deadline = if flags & TFD_TIMER_ABSTIME != 0 {
            // If the time_spec represents an absolute time, then treat the
            // `it_value` as the deadline..
            time_from_timespec(timer_spec.it_value)?
        } else {
            // .. otherwise the deadline is computed relative to the current time.
            let duration = duration_from_timespec(timer_spec.it_value)?;
            now + duration
        };
        let new_interval = duration_from_timespec(timer_spec.it_interval)?;

        if new_deadline > now {
            // If the new deadline is in the future, set the timer to trigger at deadline.
            self.timer
                .set(new_deadline, zx::Duration::default())
                .map_err(|status| from_status_like_fdio!(status))?;
        } else {
            // If the new deadline is in the past (or was 0), cancel the timer.
            self.timer.cancel().map_err(|status| from_status_like_fdio!(status))?;
        }

        let old_itimerspec = itimerspec_from_deadline_interval(old_deadline, old_interval);
        *deadline_interval = (new_deadline, new_interval);

        Ok(old_itimerspec)
    }

    /// Returns the `zx::Signals` to listen for given `events`. Used to wait on a `zx::Timer`
    /// associated with a `TimerFile`.
    fn get_signals_from_events(events: FdEvents) -> zx::Signals {
        if events & FdEvents::POLLIN {
            zx::Signals::TIMER_SIGNALED
        } else {
            zx::Signals::NONE
        }
    }

    fn get_events_from_signals(signals: zx::Signals) -> FdEvents {
        let mut events = FdEvents::empty();

        if signals.contains(zx::Signals::TIMER_SIGNALED) {
            events |= FdEvents::POLLIN;
        }

        events
    }
}

impl FileOps for TimerFile {
    fileops_impl_nonseekable!();
    fn write(
        &self,
        file: &FileObject,
        _current_task: &CurrentTask,
        _data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        // The expected error seems to vary depending on the open flags..
        if file.flags().contains(OpenFlags::NONBLOCK) {
            error!(EINVAL)
        } else {
            error!(ESPIPE)
        }
    }

    fn read(
        &self,
        _file: &FileObject,
        current_task: &CurrentTask,
        data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        let mut deadline_interval = self.deadline_interval.lock();
        let (deadline, interval) = *deadline_interval;

        if deadline == zx::Time::default() {
            // The timer has not been set.
            return error!(EAGAIN);
        }

        let now = zx::Time::get_monotonic();
        if deadline > now {
            // The next deadline has not yet passed.
            return error!(EAGAIN);
        }

        let count: i64 = if interval > zx::Duration::default() {
            let elapsed_nanos = (now - deadline).into_nanos();
            // The number of times the timer has triggered is written to `data`.
            let num_intervals = elapsed_nanos / interval.into_nanos() + 1;
            let new_deadline = deadline + interval * num_intervals;

            // The timer is set to clear the `ZX_TIMER_SIGNALED` signal until the next deadline is
            // reached.
            self.timer
                .set(new_deadline, zx::Duration::default())
                .map_err(|status| from_status_like_fdio!(status))?;

            // Update the stored deadline.
            *deadline_interval = (new_deadline, interval);

            num_intervals
        } else {
            // The timer is non-repeating, so cancel the timer to clear the `ZX_TIMER_SIGNALED`
            // signal.
            *deadline_interval = (zx::Time::default(), interval);
            self.timer.cancel().map_err(|status| from_status_like_fdio!(status))?;
            1
        };

        let bytes = count.as_bytes();
        current_task.mm.write_all(data, bytes)?;
        Ok(bytes.len())
    }

    fn wait_async(
        &self,
        _file: &FileObject,
        _current_task: &CurrentTask,
        waiter: &Waiter,
        events: FdEvents,
        handler: EventHandler,
        options: WaitAsyncOptions,
    ) -> WaitKey {
        let signal_handler = move |signals: zx::Signals| {
            let events = TimerFile::get_events_from_signals(signals);
            handler(events);
        };
        waiter
            .wake_on_signals(
                &self.timer,
                TimerFile::get_signals_from_events(events),
                Box::new(signal_handler),
                options,
            )
            .unwrap() // TODO return error
    }

    fn cancel_wait(&self, _current_task: &CurrentTask, waiter: &Waiter, key: WaitKey) {
        waiter.cancel_signal_wait(&self.timer, key);
    }

    fn query_events(&self, _current_task: &CurrentTask) -> FdEvents {
        let observed =
            self.timer.wait_handle(zx::Signals::TIMER_SIGNALED, zx::Time::INFINITE_PAST).unwrap();
        TimerFile::get_events_from_signals(observed)
    }
}
