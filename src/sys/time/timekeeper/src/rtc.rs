// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
use parking_lot::Mutex;
use {
    anyhow::{anyhow, Error},
    async_trait::async_trait,
    chrono::prelude::*,
    fdio::service_connect,
    fidl::endpoints::create_proxy,
    fidl_fuchsia_hardware_rtc as frtc,
    fuchsia_async::TimeoutExt,
    fuchsia_zircon as zx,
    futures::TryFutureExt,
    lazy_static::lazy_static,
    log::error,
    std::{fs, path::PathBuf},
    thiserror::Error,
};

lazy_static! {
    /// The absolute path at which RTC devices are exposed.
    static ref RTC_PATH: PathBuf = PathBuf::from("/dev/class/rtc/");

    /// Time to wait before declaring a FIDL call to be failed.
    static ref FIDL_TIMEOUT: zx::Duration = zx::Duration::from_millis(100);
}

const NANOS_PER_SECOND: i64 = 1_000_000_000;

#[derive(Error, Debug)]
pub enum RtcCreationError {
    #[error("Could not find any RTC devices")]
    NoDevices,
    #[error("Could not connect to RTC device: {0}")]
    CouldNotConnect(Error),
    #[error("Found {0} RTC devices, more than the 1 we expected")]
    MultipleDevices(usize),
}

/// Interface to interact with a real-time clock. Note that the RTC hardware interface is limited
/// to a resolution of one second; times returned by the RTC will always be a whole number of
/// seconds and times sent to the RTC will discard any fractional second.
#[async_trait(?Send)]
pub trait Rtc {
    /// Returns the current time reported by the realtime clock.
    async fn get(&self) -> Result<zx::Time, Error>;
    /// Sets the time of the realtime clock to `value`.
    async fn set(&self, value: zx::Time) -> Result<(), Error>;
}

/// An implementation of the `Rtc` trait that connects to an RTC device in /dev/class/rtc.
#[allow(unused)]
struct RtcImpl {
    /// The path the device was connected from.
    path: String,
    /// A proxy for the client end of a connection to the device.
    proxy: frtc::DeviceProxy,
}

#[allow(unused)]
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
                Err(RtcCreationError::CouldNotConnect(anyhow!("Failed to read entry: {}", err)))
            }
            None => Err(RtcCreationError::NoDevices),
        }
    }

    /// Returns a new `RtcImpl` connected to the device at the supplied path.
    pub fn new(path_buf: PathBuf) -> Result<RtcImpl, RtcCreationError> {
        let path_str = path_buf
            .to_str()
            .ok_or(RtcCreationError::CouldNotConnect(anyhow!("Non unicode path")))?;
        let (proxy, server) = create_proxy::<frtc::DeviceMarker>().map_err(|err| {
            RtcCreationError::CouldNotConnect(anyhow!("Failed to create proxy: {}", err))
        })?;
        service_connect(&path_str, server.into_channel()).map_err(|err| {
            RtcCreationError::CouldNotConnect(anyhow!("Failed to connect to device: {}", err))
        })?;
        Ok(RtcImpl { path: path_str.to_string(), proxy })
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

#[async_trait(?Send)]
impl Rtc for RtcImpl {
    async fn get(&self) -> Result<zx::Time, Error> {
        self.proxy
            .get()
            .map_err(|err| anyhow!("FIDL error: {}", err))
            .on_timeout(zx::Time::after(*FIDL_TIMEOUT), || Err(anyhow!("FIDL timeout on get")))
            .await
            .map(fidl_time_to_zx_time)
    }

    async fn set(&self, value: zx::Time) -> Result<(), Error> {
        let mut fidl_time = zx_time_to_fidl_time(value);
        let status = self
            .proxy
            .set(&mut fidl_time)
            .map_err(|err| anyhow!("FIDL error: {}", err))
            .on_timeout(zx::Time::after(*FIDL_TIMEOUT), || Err(anyhow!("FIDL timeout on set")))
            .await?;
        zx::Status::ok(status).map_err(|stat| anyhow!("Bad status on set: {:?}", stat))
    }
}

/// A Fake implementation of the Rtc trait for use in testing. The fake always returns a fixed
/// value set during construction and remembers the last value it was told to set.
#[cfg(test)]
pub struct FakeRtc {
    /// The response used for get requests.
    value: Result<zx::Time, String>,
    /// The most recent value received in a set request.
    last_set: Mutex<Option<zx::Time>>,
}

#[cfg(test)]
impl FakeRtc {
    /// Returns a new `FakeRtc` that always returns the supplied time.
    pub fn valid(time: zx::Time) -> FakeRtc {
        FakeRtc { value: Ok(time), last_set: Mutex::new(None) }
    }

    /// Returns a new `FakeRtc` that always returns the supplied error message.
    pub fn invalid(error: String) -> FakeRtc {
        FakeRtc { value: Err(error), last_set: Mutex::new(None) }
    }

    /// Returns the last time set on this clock, or none if the clock has never been set.
    pub fn last_set(&self) -> Option<zx::Time> {
        self.last_set.lock().map(|time| time.clone())
    }
}

#[cfg(test)]
#[async_trait(?Send)]
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
    use {super::*, fuchsia_async as fasync, lazy_static::lazy_static};

    // NOTE: Coverage of the interaction between RtcImpl and an RTC device will be provided by
    // timekeeper integration tests. Here we cover the logic used to convert time formats and the
    // fake used in the rest of the component.

    const TEST_FIDL_TIME: frtc::Time =
        frtc::Time { year: 2020, month: 8, day: 14, hours: 0, minutes: 0, seconds: 0 };

    lazy_static! {
        static ref TEST_ZX_TIME: zx::Time = zx::Time::from_nanos(1_597_363_200_000_000_000);
        static ref DIFFERENT_ZX_TIME: zx::Time = zx::Time::from_nanos(1_597_999_999_000_000_000);
    }

    #[test]
    fn time_conversion() {
        let to_fidl = zx_time_to_fidl_time(*TEST_ZX_TIME);
        assert_eq!(to_fidl, TEST_FIDL_TIME);

        let to_zx = fidl_time_to_zx_time(TEST_FIDL_TIME);
        assert_eq!(to_zx, *TEST_ZX_TIME);
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
