// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Error},
    async_trait::async_trait,
    chrono::prelude::*,
    fdio::service_connect,
    fidl::endpoints::create_proxy,
    fidl_fuchsia_hardware_rtc as frtc,
    fuchsia_async::{self as fasync, TimeoutExt},
    fuchsia_zircon::{self as zx, DurationNum},
    futures::TryFutureExt,
    lazy_static::lazy_static,
    log::error,
    std::{fs, path::PathBuf},
    thiserror::Error,
};
#[cfg(test)]
use {parking_lot::Mutex, std::sync::Arc};

lazy_static! {
    /// The absolute path at which RTC devices are exposed.
    static ref RTC_PATH: PathBuf = PathBuf::from("/dev/class/rtc/");
}

/// Time to wait before declaring a FIDL call to be failed.
const FIDL_TIMEOUT: zx::Duration = zx::Duration::from_millis(200);

// The minimum error at which to begin an async wait for top of second while setting RTC.
const WAIT_THRESHOLD: zx::Duration = zx::Duration::from_millis(1);

const NANOS_PER_SECOND: i64 = 1_000_000_000;

#[derive(Error, Debug)]
pub enum RtcCreationError {
    #[error("Could not find any RTC devices")]
    NoDevices,
    #[error("Found {0} RTC devices, more than the 1 we expected")]
    MultipleDevices(usize),
    #[error("Could not connect to RTC device: {0}")]
    ConnectionFailed(Error),
}

/// Interface to interact with a real-time clock. Note that the RTC hardware interface is limited
/// to a resolution of one second; times returned by the RTC will always be a whole number of
/// seconds and times sent to the RTC will discard any fractional second.
#[async_trait]
pub trait Rtc: Send + Sync {
    /// Returns the current time reported by the realtime clock.
    async fn get(&self) -> Result<zx::Time, Error>;
    /// Sets the time of the realtime clock to `value`.
    async fn set(&self, value: zx::Time) -> Result<(), Error>;
}

/// An implementation of the `Rtc` trait that connects to an RTC device in /dev/class/rtc.
pub struct RtcImpl {
    /// A proxy for the client end of a connection to the device.
    proxy: frtc::DeviceProxy,
}

impl RtcImpl {
    /// Returns a new `RtcImpl` connected to the only available RTC device. Returns an Error if no
    /// devices were found, multiple devices were found, or the connection failed.
    pub fn only_device() -> Result<RtcImpl, RtcCreationError> {
        let mut iterator = fs::read_dir(&*RTC_PATH).map_err(|_| RtcCreationError::NoDevices)?;
        match iterator.next() {
            Some(Ok(first_entry)) => match iterator.count() {
                0 => RtcImpl::new(first_entry.path()),
                additional_devices => {
                    Err(RtcCreationError::MultipleDevices(additional_devices + 1))
                }
            },
            Some(Err(err)) => {
                Err(RtcCreationError::ConnectionFailed(anyhow!("Failed to read entry: {}", err)))
            }
            None => Err(RtcCreationError::NoDevices),
        }
    }

    /// Returns a new `RtcImpl` connected to the device at the supplied path.
    pub fn new(path_buf: PathBuf) -> Result<RtcImpl, RtcCreationError> {
        let path_str = path_buf
            .to_str()
            .ok_or(RtcCreationError::ConnectionFailed(anyhow!("Non unicode path")))?;
        let (proxy, server) = create_proxy::<frtc::DeviceMarker>().map_err(|err| {
            RtcCreationError::ConnectionFailed(anyhow!("Failed to create proxy: {}", err))
        })?;
        service_connect(&path_str, server.into_channel()).map_err(|err| {
            RtcCreationError::ConnectionFailed(anyhow!("Failed to connect to device: {}", err))
        })?;
        Ok(RtcImpl { proxy })
    }
}

fn fidl_time_to_zx_time(fidl_time: frtc::Time) -> zx::Time {
    let chrono = Utc
        .ymd(fidl_time.year as i32, fidl_time.month as u32, fidl_time.day as u32)
        .and_hms(fidl_time.hours as u32, fidl_time.minutes as u32, fidl_time.seconds as u32);
    zx::Time::from_nanos(chrono.timestamp_nanos())
}

fn zx_time_to_fidl_time(zx_time: zx::Time) -> frtc::Time {
    let nanos = zx::Time::into_nanos(zx_time);
    let chrono = Utc.timestamp(nanos / NANOS_PER_SECOND, 0);
    frtc::Time {
        year: chrono.year() as u16,
        month: chrono.month() as u8,
        day: chrono.day() as u8,
        hours: chrono.hour() as u8,
        minutes: chrono.minute() as u8,
        seconds: chrono.second() as u8,
    }
}

#[async_trait]
impl Rtc for RtcImpl {
    async fn get(&self) -> Result<zx::Time, Error> {
        self.proxy
            .get()
            .map_err(|err| anyhow!("FIDL error: {}", err))
            .on_timeout(zx::Time::after(FIDL_TIMEOUT), || Err(anyhow!("FIDL timeout on get")))
            .await
            .map(fidl_time_to_zx_time)
    }

    async fn set(&self, value: zx::Time) -> Result<(), Error> {
        let fractional_second = zx::Duration::from_nanos(value.into_nanos() % NANOS_PER_SECOND);
        // The RTC API only accepts integer seconds but we really need higher accuracy, particularly
        // for the kernel clock set by the RTC driver...
        let mut fidl_time = if fractional_second < WAIT_THRESHOLD {
            // ...if we are being asked to set a time at or near the bottom of the second, truncate
            // the time and set the RTC immediately...
            zx_time_to_fidl_time(value)
        } else {
            // ...otherwise, wait until the top of the current second than set the RTC using the
            // following second.
            fasync::Timer::new(fasync::Time::after(1.second() - fractional_second)).await;
            zx_time_to_fidl_time(value + 1.second())
        };
        let status = self
            .proxy
            .set(&mut fidl_time)
            .map_err(|err| anyhow!("FIDL error: {}", err))
            .on_timeout(zx::Time::after(FIDL_TIMEOUT), || Err(anyhow!("FIDL timeout on set")))
            .await?;
        zx::Status::ok(status).map_err(|stat| anyhow!("Bad status on set: {:?}", stat))
    }
}

/// A Fake implementation of the Rtc trait for use in testing. The fake always returns a fixed
/// value set during construction and remembers the last value it was told to set (shared across
/// all clones of the `FakeRtc`).
#[cfg(test)]
#[derive(Clone)]
pub struct FakeRtc {
    /// The response used for get requests.
    value: Result<zx::Time, String>,
    /// The most recent value received in a set request.
    last_set: Arc<Mutex<Option<zx::Time>>>,
}

#[cfg(test)]
impl FakeRtc {
    /// Returns a new `FakeRtc` that always returns the supplied time.
    pub fn valid(time: zx::Time) -> FakeRtc {
        FakeRtc { value: Ok(time), last_set: Arc::new(Mutex::new(None)) }
    }

    /// Returns a new `FakeRtc` that always returns the supplied error message.
    pub fn invalid(error: String) -> FakeRtc {
        FakeRtc { value: Err(error), last_set: Arc::new(Mutex::new(None)) }
    }

    /// Returns the last time set on this clock, or none if the clock has never been set.
    pub fn last_set(&self) -> Option<zx::Time> {
        self.last_set.lock().map(|time| time.clone())
    }
}

#[cfg(test)]
#[async_trait]
impl Rtc for FakeRtc {
    async fn get(&self) -> Result<zx::Time, Error> {
        self.value.as_ref().map(|time| time.clone()).map_err(|msg| Error::msg(msg.clone()))
    }

    async fn set(&self, value: zx::Time) -> Result<(), Error> {
        let mut last_set = self.last_set.lock();
        last_set.replace(value);
        Ok(())
    }
}

#[cfg(test)]
mod test {
    use {
        super::*,
        fidl::endpoints::create_proxy_and_stream,
        fuchsia_async as fasync,
        futures::StreamExt,
        lazy_static::lazy_static,
        test_util::{assert_gt, assert_lt},
    };

    const TEST_FIDL_TIME: frtc::Time =
        frtc::Time { year: 2020, month: 8, day: 14, hours: 0, minutes: 0, seconds: 0 };
    const TEST_OFFSET: zx::Duration = zx::Duration::from_millis(100);

    lazy_static! {
        static ref TEST_ZX_TIME: zx::Time = zx::Time::from_nanos(1_597_363_200_000_000_000);
        static ref DIFFERENT_ZX_TIME: zx::Time = zx::Time::from_nanos(1_597_999_999_000_000_000);
    }

    #[test]
    fn time_conversion() {
        let to_fidl = zx_time_to_fidl_time(*TEST_ZX_TIME);
        assert_eq!(to_fidl, TEST_FIDL_TIME);
        // Times should be truncated to the previous second
        let to_fidl_2 = zx_time_to_fidl_time(*TEST_ZX_TIME + 999.millis());
        assert_eq!(to_fidl_2, TEST_FIDL_TIME);

        let to_zx = fidl_time_to_zx_time(TEST_FIDL_TIME);
        assert_eq!(to_zx, *TEST_ZX_TIME);
    }

    #[fasync::run_singlethreaded(test)]
    async fn rtc_impl_get() {
        let (proxy, mut stream) = create_proxy_and_stream::<frtc::DeviceMarker>().unwrap();

        let rtc_impl = RtcImpl { proxy };
        let _responder = fasync::Task::spawn(async move {
            if let Some(Ok(frtc::DeviceRequest::Get { responder })) = stream.next().await {
                let mut fidl_time = TEST_FIDL_TIME;
                responder.send(&mut fidl_time).expect("Failed response");
            }
        });
        assert_eq!(rtc_impl.get().await.unwrap(), *TEST_ZX_TIME);
    }

    #[fasync::run_singlethreaded(test)]
    async fn rtc_impl_set_whole_second() {
        let (proxy, mut stream) = create_proxy_and_stream::<frtc::DeviceMarker>().unwrap();

        let rtc_impl = RtcImpl { proxy };
        let _responder = fasync::Task::spawn(async move {
            if let Some(Ok(frtc::DeviceRequest::Set { rtc, responder })) = stream.next().await {
                let status = match rtc {
                    TEST_FIDL_TIME => zx::Status::OK,
                    _ => zx::Status::INVALID_ARGS,
                };
                responder.send(status.into_raw()).expect("Failed response");
            }
        });
        let before = zx::Time::get_monotonic();
        assert!(rtc_impl.set(*TEST_ZX_TIME).await.is_ok());
        let span = zx::Time::get_monotonic() - before;
        // Setting an integer second should not require any delay and therefore should complete
        // very fast - well under a millisecond typically.
        assert_lt!(span, 10.millis());
    }

    #[fasync::run_singlethreaded(test)]
    async fn rtc_impl_set_partial_second() {
        let (proxy, mut stream) = create_proxy_and_stream::<frtc::DeviceMarker>().unwrap();

        let rtc_impl = RtcImpl { proxy };
        let _responder = fasync::Task::spawn(async move {
            if let Some(Ok(frtc::DeviceRequest::Set { rtc, responder })) = stream.next().await {
                let status = match rtc {
                    TEST_FIDL_TIME => zx::Status::OK,
                    _ => zx::Status::INVALID_ARGS,
                };
                responder.send(status.into_raw()).expect("Failed response");
            }
        });
        let before = zx::Time::get_monotonic();
        assert!(rtc_impl.set(*TEST_ZX_TIME - TEST_OFFSET).await.is_ok());
        let span = zx::Time::get_monotonic() - before;
        // Setting a fractional second should cause a delay until the top of second before calling
        // the FIDL interface. We only verify half the expected time has passed to allow for some
        // slack in the timer calculation.
        assert_gt!(span, TEST_OFFSET / 2);
    }

    #[fasync::run_until_stalled(test)]
    async fn valid_fake() {
        let fake = FakeRtc::valid(*TEST_ZX_TIME);
        assert_eq!(fake.get().await.unwrap(), *TEST_ZX_TIME);
        assert_eq!(fake.last_set(), None);

        // Set a new time, this should be recorded but get should still return the original time.
        assert!(fake.set(*DIFFERENT_ZX_TIME).await.is_ok());
        assert_eq!(fake.last_set(), Some(*DIFFERENT_ZX_TIME));
        assert_eq!(fake.get().await.unwrap(), *TEST_ZX_TIME);
    }

    #[fasync::run_until_stalled(test)]
    async fn invalid_fake() {
        let message = "I'm designed to fail".to_string();
        let fake = FakeRtc::invalid(message.clone());
        assert_eq!(&fake.get().await.unwrap_err().to_string(), &message);
        assert_eq!(fake.last_set(), None);

        // Setting a new time should still succeed and be recorded but it won't make get valid.
        assert!(fake.set(*DIFFERENT_ZX_TIME).await.is_ok());
        assert_eq!(fake.last_set(), Some(*DIFFERENT_ZX_TIME));
        assert_eq!(&fake.get().await.unwrap_err().to_string(), &message);
    }
}
