// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{
    convert::TryInto as _, io, mem, num::TryFromIntError, os::unix::io::AsRawFd as _,
    time::Duration,
};

pub trait TcpStreamExt {
    /// Sets SO_KEEPALIVE and TCP_KEEPIDLE. Fuchsia supports
    /// `1..=i16::max_value()` seconds.
    fn set_keepalive(&self, keepalive: Option<Duration>) -> io::Result<()>;

    /// Gets TCP_KEEPIDLE if SO_KEEPALIVE is enabled.
    fn keepalive(&self) -> Result<Option<Duration>, Error>;

    /// Sets TCP_KEEPINTVL. Fuchsia supports `1..=i16::max_value()` seconds.
    fn set_keepalive_interval(&self, interval: Duration) -> io::Result<()>;

    /// Gets TCP_KEEPINTVL.
    fn keepalive_interval(&self) -> Result<Duration, Error>;

    /// Sets TCP_KEEPCNT.
    fn set_keepalive_count(&self, count: i32) -> io::Result<()>;

    /// Gets TCP_KEEPCNT.
    fn keepalive_count(&self) -> io::Result<i32>;

    /// Sets TCP_USER_TIMEOUT. Fuchsia supports `1..=i32::max_value()`
    /// milliseconds.
    fn set_user_timeout(&self, timeout: Duration) -> io::Result<()>;

    /// Gets TCP_USER_TIMEOUT.
    fn user_timeout(&self) -> Result<Duration, Error>;
}

#[derive(thiserror::Error, Debug)]
pub enum Error {
    #[error("netstack returned an error: {0}")]
    Netstack(io::Error),
    #[error("netstack returned a negative duration: {0}")]
    NegativeDuration(i32),
}

impl TcpStreamExt for std::net::TcpStream {
    fn set_keepalive(&self, keepalive: Option<Duration>) -> io::Result<()> {
        let () = set_option(
            self,
            libc::SOL_SOCKET,
            libc::SO_KEEPALIVE,
            keepalive.is_some() as libc::c_int,
        )?;
        if let Some(keepalive) = keepalive {
            let keepalive =
                keepalive.as_secs().try_into().map_err(|TryFromIntError { .. }| {
                    io::Error::new(
                        io::ErrorKind::InvalidInput,
                        "keepalive idle duration seconds does not fit in an i32",
                    )
                })?;
            let () = set_tcp_option(self, libc::TCP_KEEPIDLE, keepalive)?;
        }
        Ok(())
    }

    fn keepalive(&self) -> Result<Option<Duration>, Error> {
        let keepalive =
            get_option(self, libc::SOL_SOCKET, libc::SO_KEEPALIVE).map_err(Error::Netstack)?;
        if keepalive == 0 {
            Ok(None)
        } else {
            let keepalive = get_tcp_option(self, libc::TCP_KEEPIDLE).map_err(Error::Netstack)?;
            let keepalive = keepalive
                .try_into()
                .map_err(|TryFromIntError { .. }| Error::NegativeDuration(keepalive))?;
            Ok(Some(Duration::from_secs(keepalive)))
        }
    }

    fn set_keepalive_interval(&self, interval: Duration) -> io::Result<()> {
        set_tcp_option(
            self,
            libc::TCP_KEEPINTVL,
            interval.as_secs().try_into().map_err(|TryFromIntError { .. }| {
                io::Error::new(
                    io::ErrorKind::InvalidInput,
                    "keepalive interval duration seconds does not fit in an i32",
                )
            })?,
        )
    }

    fn keepalive_interval(&self) -> Result<Duration, Error> {
        get_tcp_option(self, libc::TCP_KEEPINTVL).map_err(Error::Netstack).and_then(|interval| {
            Ok(Duration::from_secs(
                interval
                    .try_into()
                    .map_err(|TryFromIntError { .. }| Error::NegativeDuration(interval))?,
            ))
        })
    }

    fn set_keepalive_count(&self, count: i32) -> io::Result<()> {
        set_tcp_option(self, libc::TCP_KEEPCNT, count)
    }

    fn keepalive_count(&self) -> io::Result<i32> {
        get_tcp_option(self, libc::TCP_KEEPCNT)
    }

    fn set_user_timeout(&self, timeout: Duration) -> io::Result<()> {
        set_tcp_option(
            self,
            libc::TCP_USER_TIMEOUT,
            timeout.as_millis().try_into().map_err(|TryFromIntError { .. }| {
                io::Error::new(
                    io::ErrorKind::InvalidInput,
                    "user timeout duration milliseconds does not fit in an i32",
                )
            })?,
        )
    }

    fn user_timeout(&self) -> Result<Duration, Error> {
        get_tcp_option(self, libc::TCP_USER_TIMEOUT).map_err(Error::Netstack).and_then(|timeout| {
            Ok(Duration::from_millis(
                timeout
                    .try_into()
                    .map_err(|TryFromIntError { .. }| Error::NegativeDuration(timeout))?,
            ))
        })
    }
}

fn set_option(
    stream: &std::net::TcpStream,
    option_level: libc::c_int,
    option_name: libc::c_int,
    option_value: i32,
) -> io::Result<()> {
    let fd = stream.as_raw_fd();
    // Safe because `setsockopt` does not retain memory passed to it.
    if unsafe {
        libc::setsockopt(
            fd,
            option_level,
            option_name,
            &option_value as *const _ as *const libc::c_void,
            mem::size_of_val(&option_value) as libc::socklen_t,
        )
    } != 0
    {
        Err(io::Error::last_os_error())?;
    }
    Ok(())
}

fn set_tcp_option(
    stream: &std::net::TcpStream,
    option_name: libc::c_int,
    option_value: i32,
) -> io::Result<()> {
    set_option(stream, libc::IPPROTO_TCP, option_name, option_value)
}

fn get_option(
    stream: &std::net::TcpStream,
    option_level: libc::c_int,
    option_name: libc::c_int,
) -> io::Result<i32> {
    let fd = stream.as_raw_fd();
    let mut option_value = 0i32;
    let mut option_value_size = mem::size_of_val(&option_value) as libc::socklen_t;
    // Safe because `getsockopt` does not retain memory passed to it.
    if unsafe {
        libc::getsockopt(
            fd,
            option_level,
            option_name,
            &mut option_value as *mut _ as *mut libc::c_void,
            &mut option_value_size,
        )
    } != 0
    {
        Err(io::Error::last_os_error())?;
    }
    Ok(option_value)
}

fn get_tcp_option(stream: &std::net::TcpStream, option_name: libc::c_int) -> io::Result<i32> {
    get_option(stream, libc::IPPROTO_TCP, option_name)
}

#[cfg(test)]
mod test {
    use {super::*, matches::assert_matches, proptest::prelude::*};

    fn stream() -> std::io::Result<std::net::TcpStream> {
        use socket2::{Domain, Socket, Type};

        let socket = Socket::new(Domain::ipv4(), Type::stream(), None)?;
        Ok(socket.into_tcp_stream())
    }

    const MAX_TCP_KEEPALIVE_IDLE: u64 = 32_767;
    const MAX_TCP_KEEPALIVE_INTERVAL: u64 = 32_767;
    const MAX_TCP_KEEPALIVE_COUNT: i32 = 127;

    #[test]
    fn set_keepalive_rejects_more_than_i32_seconds() {
        let stream = stream().unwrap();
        let interval = Duration::from_secs(i32::max_value() as u64 + 1);

        assert_matches!(
            stream.set_keepalive(Some(interval)),
            Err(e) if e.kind() == io::ErrorKind::InvalidInput
        );
    }

    #[test]
    fn set_keepalive_interval_rejects_more_than_i32_seconds() {
        let stream = stream().unwrap();
        let interval = Duration::from_secs(i32::max_value() as u64 + 1);

        assert_matches!(
            stream.set_keepalive_interval(interval),
            Err(e) if e.kind() == io::ErrorKind::InvalidInput
        );
    }

    #[test]
    fn set_user_timeout_rejects_more_than_i32_milliseconds() {
        let stream = stream().unwrap();
        let timeout = Duration::from_millis(i32::max_value() as u64 + 1);

        assert_matches!(
            stream.set_user_timeout(timeout),
            Err(e) if e.kind() == io::ErrorKind::InvalidInput
        );
    }

    proptest! {
        #[test]
        fn keepalive_roundtrip
            (seconds in 1..=MAX_TCP_KEEPALIVE_IDLE)
        {
            let stream = stream().unwrap();
            let interval = Some(Duration::from_secs(seconds));

            stream.set_keepalive(interval).unwrap();
            prop_assert_eq!(stream.keepalive().unwrap(), interval);
        }

        #[test]
        fn keepalive_interval_roundtrip
            (seconds in 1..=MAX_TCP_KEEPALIVE_INTERVAL)
        {
            let stream = stream().unwrap();
            let interval = Duration::from_secs(seconds);

            stream.set_keepalive_interval(interval).unwrap();
            prop_assert_eq!(stream.keepalive_interval().unwrap(), interval);
        }

        #[test]
        fn keepalive_count_roundtrip
            (count in 1..=MAX_TCP_KEEPALIVE_COUNT)
        {
            let stream = stream().unwrap();

            stream.set_keepalive_count(count).unwrap();
            prop_assert_eq!(stream.keepalive_count().unwrap(), count);
        }

        #[test]
        fn user_timeout_roundtrip
            (timeout in 0..=i32::max_value() as u64)
        {
            let stream = stream().unwrap();
            let timeout = Duration::from_millis(timeout);

            stream.set_user_timeout(timeout).unwrap();
            prop_assert_eq!(stream.user_timeout().unwrap(), timeout);
        }
    }
}
