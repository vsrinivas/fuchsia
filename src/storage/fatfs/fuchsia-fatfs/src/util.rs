// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    chrono::{offset::TimeZone, Local},
    fatfs::DateTime,
    fuchsia_zircon::Status,
    std::io::{self, ErrorKind},
};

/// Returns the equivalent of the given DOS time as ns past the unix epoch.
pub fn dos_to_unix_time(dos_time: DateTime) -> u64 {
    let datetime = chrono::DateTime::<Local>::from(dos_time);
    datetime.timestamp_nanos() as u64
}

/// Returns the given unix timestamp in ns as a FAT-compatible DateTime.
pub fn unix_to_dos_time(timestamp: u64) -> DateTime {
    DateTime::from(Local.timestamp_nanos(timestamp as i64))
}

pub fn fatfs_error_to_status(error: io::Error) -> Status {
    match error.kind() {
        ErrorKind::AddrInUse => Status::ADDRESS_IN_USE,
        ErrorKind::AddrNotAvailable => Status::UNAVAILABLE,
        ErrorKind::AlreadyExists => Status::ALREADY_EXISTS,
        ErrorKind::BrokenPipe => Status::PEER_CLOSED,
        ErrorKind::ConnectionAborted => Status::CONNECTION_ABORTED,
        ErrorKind::ConnectionRefused => Status::CONNECTION_REFUSED,
        ErrorKind::ConnectionReset => Status::CONNECTION_RESET,
        ErrorKind::Interrupted => Status::INTERRUPTED_RETRY,
        ErrorKind::InvalidData => Status::IO_INVALID,
        ErrorKind::InvalidInput => Status::INVALID_ARGS,
        ErrorKind::NotConnected => Status::NOT_CONNECTED,
        ErrorKind::NotFound => Status::NOT_FOUND,
        ErrorKind::PermissionDenied => Status::ACCESS_DENIED,
        ErrorKind::TimedOut => Status::TIMED_OUT,
        ErrorKind::UnexpectedEof => Status::BUFFER_TOO_SMALL,
        ErrorKind::WouldBlock => Status::BAD_STATE,
        ErrorKind::WriteZero => Status::IO, // Never used in fatfs.
        // TODO(53796): We should figure out a way to get better errors from fatfs.
        // For instance, the rust io::Error has no concept of Status::NOT_DIR and Status::NOT_FILE,
        // which fatfs returns as `ErrorKind::Other` with a string error message.
        ErrorKind::Other => Status::IO,
        _ => Status::IO,
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fatfs::{Date, Time},
    };

    fn dos_datetime(
        year: u16,
        month: u16,
        day: u16,
        hour: u16,
        min: u16,
        sec: u16,
        millis: u16,
    ) -> DateTime {
        DateTime { date: Date { year, month, day }, time: Time { hour, min, sec, millis } }
    }

    const NS_PER_MS: u64 = 1_000_000;

    fn ms_to_ns(ts: u64) -> u64 {
        ts * NS_PER_MS
    }

    #[test]
    fn test_dos_to_unix_time() {
        let earliest_time = dos_datetime(1980, 1, 1, 0, 0, 0, 0);
        assert_eq!(dos_to_unix_time(earliest_time), ms_to_ns(315532800000));

        let latest_time = dos_datetime(2107, 12, 31, 23, 59, 59, 999);
        assert_eq!(dos_to_unix_time(latest_time), ms_to_ns(4354819199999));

        let time = dos_datetime(2038, 1, 19, 3, 14, 7, 0);
        assert_eq!(dos_to_unix_time(time), ms_to_ns(2147483647000));
    }

    #[test]
    fn test_unix_to_dos_time() {
        let earliest_time = dos_datetime(1980, 1, 1, 0, 0, 0, 0);
        assert_eq!(earliest_time, unix_to_dos_time(ms_to_ns(315532800000)));

        let latest_time = dos_datetime(2107, 12, 31, 23, 59, 59, 999);
        assert_eq!(latest_time, unix_to_dos_time(ms_to_ns(4354819199999)));

        let time = dos_datetime(2038, 1, 19, 3, 14, 7, 0);
        assert_eq!(time, unix_to_dos_time(ms_to_ns(2147483647000)));
    }
}
