// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_hardware_audio::*,
    fuchsia_async as fasync,
    fuchsia_zircon::{self as zx, DurationNum, HandleBased},
    futures::task::Waker,
    log::info,
};

use crate::driver::frames_from_duration;
use crate::types::{AudioSampleFormat, Error, Result};

/// A FrameVmo wraps a VMO with time tracking.  When a FrameVmo is started, it
/// assumes that audio frame data is being written to the VMO at the rate specific
/// in the format it is set to.  Frames that represent a time range can be
/// retrieved from the buffer.
pub(crate) struct FrameVmo {
    /// Ring Buffer VMO. Size zero until the ringbuffer is established.  Shared with
    /// the AudioFrameStream given back to the client.
    vmo: zx::Vmo,

    /// Cached size of the ringbuffer, in bytes.  Used to avoid zx_get_size() syscalls.
    size: usize,

    /// The time that streaming was started.
    /// Used to calculate the currently available frames.
    /// None if the stream is not started.
    start_time: Option<zx::Time>,

    /// This waker will be woken if we are started.
    wake_on_start: Option<Waker>,

    /// The number of frames per second.
    frames_per_second: u32,

    /// The audio format of the frames.
    format: Option<AudioSampleFormat>,

    /// Number of bytes per frame, 0 if format is not set.
    bytes_per_frame: usize,

    /// Number of notifications per ring.
    notifications_per_ring: u32,

    /// A position responder that will be used to notify when the ringbuffer has been read.
    /// This will be notified the correct number of times based on the last read position using
    /// `get_frames`
    position_responder: Option<RingBufferWatchClockRecoveryPositionInfoResponder>,

    /// The latest frame time that has been sent to notify for position.
    latest_notify: zx::Time,

    /// The number of channels.
    channels: u16,
}

impl FrameVmo {
    pub(crate) fn new() -> Result<FrameVmo> {
        Ok(FrameVmo {
            vmo: zx::Vmo::create(0).map_err(|e| Error::IOError(e))?,
            size: 0,
            start_time: None,
            wake_on_start: None,
            frames_per_second: 0,
            format: None,
            bytes_per_frame: 0,
            notifications_per_ring: 0,
            position_responder: None,
            latest_notify: zx::Time::INFINITE_PAST,
            channels: 0,
        })
    }

    /// Set the format of this buffer.   Returns a handle representing the VMO.
    /// `frames` is the number of frames the VMO should be able to hold.
    pub(crate) fn set_format(
        &mut self,
        frames_per_second: u32,
        format: AudioSampleFormat,
        channels: u16,
        frames: usize,
        notifications_per_ring: u32,
    ) -> Result<zx::Vmo> {
        if self.start_time.is_some() {
            return Err(Error::InvalidState);
        }
        let bytes_per_frame = format.compute_frame_size(channels as usize)?;
        let new_size = bytes_per_frame * frames;
        self.vmo = zx::Vmo::create(new_size as u64).map_err(|e| Error::IOError(e))?;
        self.bytes_per_frame = bytes_per_frame;
        self.size = new_size;
        self.format = Some(format);
        self.frames_per_second = frames_per_second;
        self.channels = channels;
        self.notifications_per_ring = notifications_per_ring;
        Ok(self.vmo.duplicate_handle(zx::Rights::SAME_RIGHTS).map_err(|e| Error::IOError(e))?)
    }

    pub(crate) fn set_position_responder(
        &mut self,
        position_responder: RingBufferWatchClockRecoveryPositionInfoResponder,
    ) {
        self.position_responder = Some(position_responder);
    }

    pub(crate) fn set_start_waker(&mut self, lw: Waker) {
        self.wake_on_start = Some(lw);
    }

    /// Start the audio clock for the buffer at `time`
    pub(crate) fn start(&mut self, time: zx::Time) -> Result<()> {
        if self.start_time.is_some() || self.format.is_none() {
            return Err(Error::InvalidState);
        }
        self.start_time = Some(time);
        self.latest_notify = time;
        if let Some(waker) = self.wake_on_start.take() {
            info!("ringing the start waker");
            waker.wake();
        }
        Ok(())
    }

    pub(crate) fn start_time(&self) -> Option<zx::Time> {
        self.start_time
    }

    /// Stop the audio clock in tbe buffer.
    /// returns true if the streaming was stopped.
    pub(crate) fn stop(&mut self) -> bool {
        if self.start_time.is_none() {
            return false;
        }
        self.start_time = None;
        true
    }

    /// Retrieve the complete frames available from `from` to `until`
    /// Frames that end on or after `from` and before `until` are included.
    /// `until` is not allowed to be in the future.
    /// Returns a vector of bytes representing the frames and a number of frames
    /// that are unavailable because of data overflow.
    pub(crate) fn get_frames(
        &mut self,
        mut from: zx::Time,
        until: zx::Time,
    ) -> Result<(Vec<u8>, usize)> {
        if self.start_time.is_none() {
            return Err(Error::InvalidState);
        }
        let now = fasync::Time::now();
        if until > now.into() {
            info!("Can't get frames from the future");
            return Err(Error::OutOfRange);
        }
        let start_time = self.start_time.clone().unwrap();
        if from < start_time {
            info!("Can't get frames from before start, delivering what we have");
            from = start_time;
        }

        let vmo_frames = self.size / self.bytes_per_frame;
        let vmo_duration = self.duration_from_frames(vmo_frames);

        let oldest_frame_start: zx::Time = (now - vmo_duration).into();

        if until <= oldest_frame_start {
            // every frame from this time period is missing
            return Ok((vec![], self.frames_from_duration(until - from) as usize));
        }

        let missing_frames = if oldest_frame_start < from {
            0
        } else {
            self.frames_from_duration(oldest_frame_start - from) as usize
        };

        if missing_frames > 0 {
            from = oldest_frame_start;
        }

        // Start time is the zero frame.
        let mut frame_from_idx = self.frames_before(from) % vmo_frames;
        let mut frame_until_idx = self.frames_before(until) % vmo_frames;

        if frame_from_idx == frame_until_idx && until != now.into() {
            return Ok((vec![], missing_frames));
        }

        if frame_from_idx >= frame_until_idx {
            frame_until_idx += vmo_frames;
        }

        let frames_available = frame_until_idx - frame_from_idx;
        let bytes_available = frames_available * self.bytes_per_frame;
        let mut out_vec = vec![0; bytes_available];

        let mut ndx = 0;

        if frame_until_idx > vmo_frames {
            let frames_to_read = vmo_frames - frame_from_idx;
            let bytes_to_read = frames_to_read * self.bytes_per_frame;
            let byte_start = frame_from_idx * self.bytes_per_frame;
            self.vmo
                .read(&mut out_vec[0..bytes_to_read], byte_start as u64)
                .map_err(|e| Error::IOError(e))?;
            frame_from_idx = 0;
            frame_until_idx -= vmo_frames;
            ndx = bytes_to_read;
        }

        let frames_to_read = frame_until_idx - frame_from_idx;
        let bytes_to_read = frames_to_read * self.bytes_per_frame;
        let byte_start = frame_from_idx * self.bytes_per_frame;

        self.vmo
            .read(&mut out_vec[ndx..ndx + bytes_to_read], byte_start as u64)
            .map_err(|e| Error::IOError(e))?;

        if self.notifications_per_ring > 0 && self.latest_notify < until {
            if self.position_responder.is_some() {
                let time_between_notifications = vmo_duration / self.notifications_per_ring;
                let time_since_notify = until - self.latest_notify;
                if time_since_notify > time_between_notifications {
                    let notify_time = self.latest_notify + time_between_notifications;
                    let notify_frame_idx = self.frames_before(notify_time) % vmo_frames;
                    let notify_frame_bytes = notify_frame_idx * self.bytes_per_frame;
                    let mut info = RingBufferPositionInfo {
                        timestamp: notify_time.into_nanos(),
                        position: notify_frame_bytes as u32,
                    };
                    let responder = self
                        .position_responder
                        .take()
                        .expect("position responder must still be there");
                    if let Ok(()) = responder.send(&mut info) {
                        self.latest_notify = notify_time;
                    }
                }
            }
        }
        Ok((out_vec, missing_frames))
    }

    /// Count of the number of frames that have occurred before `time`.
    fn frames_before(&self, time: zx::Time) -> usize {
        if self.start_time().is_none() {
            return 0;
        }
        let start_time = self.start_time.clone().unwrap();
        if time < start_time {
            return 0;
        }
        return self.frames_from_duration(time - start_time) as usize;
    }

    /// Open front, closed end frames from duration.
    /// This means if duration is an exact duration of a number of frames, the last
    /// frame will be considered to not be inside the duration, and will not be counted.
    fn frames_from_duration(&self, duration: zx::Duration) -> usize {
        frames_from_duration(self.frames_per_second as usize, duration)
    }

    /// Return an amount of time that guarantees that `frames` frames has passed.
    /// This means that partial nanoseconds will be rounded up, so that
    /// [time, time + duration_from_frames(n)] is guaranteed to include n audio frames.
    /// Only well-defined for positive numbers of frames.
    fn duration_from_frames(&self, frames: usize) -> zx::Duration {
        let secs = frames / self.frames_per_second as usize;
        let nanos_per_frame: f64 = 1e9 / self.frames_per_second as f64;
        let nanos =
            ((frames % self.frames_per_second as usize) as f64 * nanos_per_frame).ceil() as i64;
        (secs as i64).seconds() + nanos.nanos()
    }

    pub(crate) fn next_frame_after(&self, time: zx::Time) -> Result<zx::Time> {
        if self.start_time.is_none() {
            return Err(Error::InvalidState);
        }
        let start_time = self.start_time.clone().unwrap();
        let frames =
            if time <= start_time { 0 } else { self.frames_from_duration(time - start_time) };
        Ok(start_time + self.duration_from_frames(frames + 1))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use fuchsia_async as fasync;
    use fuchsia_zircon::{self as zx};

    // Convenience choice because one byte = one frame.
    const TEST_FORMAT: AudioSampleFormat = AudioSampleFormat::Eight { unsigned: false };
    const TEST_CHANNELS: u16 = 1;
    const TEST_FPS: u32 = 48000;

    // At 48kHz, each frame is 20833 and 1/3 nanoseconds. We add one nanosecond
    // because we always overestimate durations.
    const ONE_FRAME_NANOS: i64 = 20833 + 1;
    const TWO_FRAME_NANOS: i64 = 20833 * 2 + 1;
    const THREE_FRAME_NANOS: i64 = 20833 * 3 + 1;

    fn get_test_vmo(frames: usize) -> FrameVmo {
        let mut vmo = FrameVmo::new().expect("can't make a framevmo");
        let _handle = vmo.set_format(TEST_FPS, TEST_FORMAT, TEST_CHANNELS, frames, 0).unwrap();
        vmo
    }

    #[test]
    fn test_duration_from_frames() {
        let vmo = get_test_vmo(5);

        assert_eq!(ONE_FRAME_NANOS.nanos(), vmo.duration_from_frames(1));
        assert_eq!(TWO_FRAME_NANOS.nanos(), vmo.duration_from_frames(2));
        assert_eq!(THREE_FRAME_NANOS.nanos(), vmo.duration_from_frames(3));

        assert_eq!(1.second(), vmo.duration_from_frames(TEST_FPS as usize));

        assert_eq!(1500.millis(), vmo.duration_from_frames(72000));
    }

    #[test]
    fn test_frames_from_duration() {
        let vmo = get_test_vmo(5);

        assert_eq!(0, vmo.frames_from_duration(0.nanos()));

        assert_eq!(0, vmo.frames_from_duration((ONE_FRAME_NANOS - 1).nanos()));
        assert_eq!(1, vmo.frames_from_duration(ONE_FRAME_NANOS.nanos()));

        // Three frames is an exact number of nanoseconds, so it should only count if we provide
        // a duration that is LONGER.
        assert_eq!(2, vmo.frames_from_duration((THREE_FRAME_NANOS - 1).nanos()));
        assert_eq!(2, vmo.frames_from_duration(THREE_FRAME_NANOS.nanos()));
        assert_eq!(3, vmo.frames_from_duration((THREE_FRAME_NANOS + 1).nanos()));

        assert_eq!(TEST_FPS as usize - 1, vmo.frames_from_duration(1.second()));
        assert_eq!(72000 - 1, vmo.frames_from_duration(1500.millis()));

        assert_eq!(10660, vmo.frames_from_duration(zx::Duration::from_nanos(222084000)));
    }

    fn get_time_now() -> zx::Time {
        fasync::Time::now().into()
    }

    #[test]
    fn test_next_frame_after() {
        let _exec = fasync::Executor::new();
        let mut vmo = get_test_vmo(5);

        assert_eq!(Err(Error::InvalidState), vmo.next_frame_after(get_time_now()));

        let start_time = get_time_now();
        assert_eq!(Ok(()), vmo.start(start_time));

        // At 48kHz, each frame is 20833 and 1/3 nanoseconds.
        let first_frame_after = start_time + ONE_FRAME_NANOS.nanos();

        assert_eq!(Ok(first_frame_after), vmo.next_frame_after(start_time));
        assert_eq!(Ok(first_frame_after), vmo.next_frame_after(zx::Time::INFINITE_PAST));

        let next_frame = start_time + TWO_FRAME_NANOS.nanos();
        assert_eq!(Ok(next_frame), vmo.next_frame_after(first_frame_after));

        let one_sec_later = start_time + 1.second();
        assert_eq!(Ok(one_sec_later), vmo.next_frame_after(one_sec_later - 1.nanos()));
    }

    #[test]
    fn test_start_stop() {
        let _exec = fasync::Executor::new();
        let mut vmo = get_test_vmo(5);

        let start_time = get_time_now();
        assert_eq!(Ok(()), vmo.start(start_time));
        assert_eq!(Err(Error::InvalidState), vmo.start(start_time));

        vmo.stop();

        assert_eq!(Ok(()), vmo.start(start_time));
    }

    fn test_frames_before_exact(
        vmo: &mut FrameVmo,
        time_nanos: i64,
        duration: zx::Duration,
        frames: usize,
    ) {
        vmo.stop();
        let start_time = zx::Time::from_nanos(time_nanos);
        assert_eq!(Ok(()), vmo.start(start_time));
        assert_eq!(frames, vmo.frames_before(start_time + duration));
    }

    #[test]
    fn test_frames_before() {
        let _exec = fasync::Executor::new();
        let mut vmo = get_test_vmo(5);

        let start_time = get_time_now();
        assert_eq!(Ok(()), vmo.start(start_time));

        assert_eq!(0, vmo.frames_before(start_time));

        assert_eq!(1, vmo.frames_before(start_time + ONE_FRAME_NANOS.nanos()));
        assert_eq!(2, vmo.frames_before(start_time + THREE_FRAME_NANOS.nanos()));
        assert_eq!(3, vmo.frames_before(start_time + (THREE_FRAME_NANOS + 1).nanos()));

        assert_eq!(TEST_FPS as usize / 4 - 1, vmo.frames_before(start_time + 250.millis()));

        let three_quarters_dur = 375.millis();
        assert_eq!(17999, 3 * TEST_FPS as usize / 8 - 1);
        assert_eq!(
            3 * TEST_FPS as usize / 8 - 1,
            vmo.frames_before(start_time + three_quarters_dur)
        );

        assert_eq!(10521, vmo.frames_before(start_time + 219188000.nanos()));

        test_frames_before_exact(&mut vmo, 273533747037, 219188000.nanos(), 10521);
        test_frames_before_exact(&mut vmo, 714329925362, 219292000.nanos(), 10526);
    }

    #[test]
    fn test_get_frames() {
        let exec = fasync::Executor::new_with_fake_time().expect("executor needed");
        exec.set_fake_time(fasync::Time::from_nanos(1_000_000_000));
        let frames = TEST_FPS as usize / 2;
        let mut vmo = get_test_vmo(frames);

        let start_time = get_time_now();
        assert_eq!(Ok(()), vmo.start(start_time));

        let half_dur = 250.millis();
        exec.set_fake_time(fasync::Time::after(half_dur));

        let res = vmo.get_frames(start_time, start_time + half_dur);
        assert!(res.is_ok());
        let (bytes, missed) = res.unwrap();

        assert_eq!(0, missed);
        assert_eq!(frames / 2 - 1, bytes.len());

        // Each `dur` period should pseudo-fill half the vmo.
        // After 750 ms, we should have the oildest frame half-way through
        // the buffer.
        exec.set_fake_time(fasync::Time::after(half_dur * 2));

        // Should be able to get some frames that span the end of the buffer
        let three_quarters_dur = 375.millis();
        let res = vmo.get_frames(
            start_time + three_quarters_dur,
            start_time + three_quarters_dur + half_dur,
        );
        assert!(res.is_ok());
        let (bytes, missed) = res.unwrap();
        assert_eq!(0, missed);
        assert_eq!(frames / 2, bytes.len());

        // Should be able to get a set of frames that is all located before the oldest point.
        // This should be from about a quarter in to halfway in.
        let res =
            vmo.get_frames(start_time + three_quarters_dur + half_dur, start_time + (half_dur * 3));
        assert!(res.is_ok());
        let (bytes, missed) = res.unwrap();
        assert_eq!(0, missed);
        assert_eq!(frames / 4, bytes.len());

        // Get a set of frames that is exactly the whole buffer.
        let res = vmo.get_frames(get_time_now() - 500.millis(), get_time_now());

        assert!(res.is_ok());
        let (bytes, missed) = res.unwrap();

        assert_eq!(0, missed);
        assert_eq!(frames, bytes.len());
    }

    #[test]
    fn test_multibyte_get_frames() {
        let _exec = fasync::Executor::new();
        let mut vmo = FrameVmo::new().expect("can't make a framevmo");
        let format = AudioSampleFormat::Sixteen { unsigned: false, invert_endian: false };
        let frames = TEST_FPS as usize / 2;
        let _handle = vmo.set_format(TEST_FPS, format, 2, frames, 0).unwrap();

        let half_dur = 250.millis();

        // Start in the past so we can be sure the frames are in the past.
        let start_time = get_time_now() - half_dur;
        assert_eq!(Ok(()), vmo.start(start_time));

        let res = vmo.get_frames(start_time, start_time + half_dur);
        assert!(res.is_ok());
        let (bytes, missed) = res.unwrap();

        assert_eq!(0, missed);
        // There should be frames / 2 frames here, with 4 bytes per frame.
        assert_eq!(frames * 2 - 4, bytes.len())
    }

    #[test]
    fn test_get_frames_boundaries() {
        let exec = fasync::Executor::new_with_fake_time().expect("executor needed");
        exec.set_fake_time(fasync::Time::from_nanos(1_000_000_000));
        let frames = TEST_FPS as usize / 2;
        let mut vmo = get_test_vmo(frames);

        // Start in the past so we can be sure the whole test is always in the past.
        let start_time = get_time_now() - 400.millis();

        assert_eq!(Ok(()), vmo.start(start_time));

        let res = vmo.get_frames(
            start_time + THREE_FRAME_NANOS.nanos(),
            start_time + (THREE_FRAME_NANOS + 1).nanos(),
        );
        let (bytes, missed) = res.expect("valid times after vmo start should succeed");
        assert_eq!(0, missed);
        assert_eq!(1, bytes.len());

        let res = vmo.get_frames(
            start_time + (3999 * THREE_FRAME_NANOS - ONE_FRAME_NANOS).nanos(),
            start_time + (3999 * THREE_FRAME_NANOS).nanos(),
        );
        let (bytes, missed) = res.expect("valid times after vmo start should succeed");
        assert_eq!(0, missed);
        assert_eq!(1, bytes.len());

        let mut all_frames_len = 0;
        let mut total_duration = 0.nanos();

        let moment_length = 10_000.nanos();

        let mut moment_start = start_time;

        while total_duration < 250.millis() {
            let moment_end = moment_start + moment_length;
            let res = vmo.get_frames(moment_start, moment_end);
            total_duration += moment_length;
            let (bytes, missed) = res.expect("valid times after vmo start should succeed");
            assert_eq!(0, missed);
            all_frames_len += bytes.len();
            assert_eq!(
                all_frames_len,
                vmo.frames_before(moment_end),
                "frame miscount after {:?} - {:?} moment",
                moment_start,
                moment_end
            );
            moment_start = moment_end;
        }

        assert_eq!(frames / 2 - 1, all_frames_len, "should be a quarter second worth of frames");
    }
}
