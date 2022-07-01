// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::prelude_internal::*;

/// The maximum length of Thread network data, in bytes.
pub const MAX_NET_DATA_LEN: usize = 255;

/// Methods from the [OpenThread "NetData" Module][1].
///
/// [1]: https://openthread.io/reference/group/api-thread-general
pub trait NetData {
    /// Functional equivalent of [`otsys::otNetDataGet`](crate::otsys::otNetDataGet).
    fn net_data_get<'a>(&self, stable: bool, data: &'a mut [u8]) -> Result<&'a [u8]>;

    /// Same as [`net_data_get`], but returns the net data as a vector.
    fn net_data_as_vec(&self, stable: bool) -> Result<Vec<u8>> {
        let mut ret = vec![0; MAX_NET_DATA_LEN];

        let len = self.net_data_get(stable, ret.as_mut_slice())?.len();

        ret.truncate(len);

        Ok(ret)
    }

    /// Functional equivalent of [`otsys::otNetDataGetVersion`](crate::otsys::otNetDataGetVersion).
    fn net_data_get_version(&self) -> u8;

    /// Functional equivalent of
    /// [`otsys::otNetDataGetStableVersion`](crate::otsys::otNetDataGetStableVersion).
    fn net_data_get_stable_version(&self) -> u8;
}

impl<T: NetData + Boxable> NetData for ot::Box<T> {
    fn net_data_get<'a>(&self, stable: bool, data: &'a mut [u8]) -> Result<&'a [u8]> {
        self.as_ref().net_data_get(stable, data)
    }

    fn net_data_get_version(&self) -> u8 {
        self.as_ref().net_data_get_version()
    }

    fn net_data_get_stable_version(&self) -> u8 {
        self.as_ref().net_data_get_version()
    }
}

impl NetData for Instance {
    fn net_data_get<'a>(&self, stable: bool, data: &'a mut [u8]) -> Result<&'a [u8]> {
        let mut len: u8 = data.len().min(MAX_NET_DATA_LEN).try_into().unwrap();

        Error::from(unsafe {
            otNetDataGet(self.as_ot_ptr(), stable, data.as_mut_ptr(), (&mut len) as *mut u8)
        })
        .into_result()?;

        Ok(&data[..(len as usize)])
    }

    fn net_data_get_version(&self) -> u8 {
        unsafe { otNetDataGetVersion(self.as_ot_ptr()) }
    }

    fn net_data_get_stable_version(&self) -> u8 {
        unsafe { otNetDataGetStableVersion(self.as_ot_ptr()) }
    }
}
