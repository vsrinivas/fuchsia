// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::prelude_internal::*;

/// Represents the thread joiner state.
///
/// Functional equivalent of [`otsys::otJoinerState`](crate::otsys::otJoinerState).
#[derive(Debug, Copy, Clone, Eq, Ord, PartialOrd, PartialEq, num_derive::FromPrimitive)]
#[allow(missing_docs)]
pub enum JoinerState {
    /// Functional equivalent of [`otsys::otJoinerState_OT_JOINER_STATE_IDLE`](crate::otsys::otJoinerState_OT_JOINER_STATE_IDLE).
    Idle = otJoinerState_OT_JOINER_STATE_IDLE as isize,

    /// Functional equivalent of [`otsys::otJoinerState_OT_JOINER_STATE_DISCOVER`](crate::otsys::otJoinerState_OT_JOINER_STATE_DISCOVER).
    Discover = otJoinerState_OT_JOINER_STATE_DISCOVER as isize,

    /// Functional equivalent of [`otsys::otJoinerState_OT_JOINER_STATE_CONNECT`](crate::otsys::otJoinerState_OT_JOINER_STATE_CONNECT).
    Connect = otJoinerState_OT_JOINER_STATE_CONNECT as isize,

    /// Functional equivalent of [`otsys::otJoinerState_OT_JOINER_STATE_CONNECTED`](crate::otsys::otJoinerState_OT_JOINER_STATE_CONNECTED).
    Connected = otJoinerState_OT_JOINER_STATE_CONNECTED as isize,

    /// Functional equivalent of [`otsys::otJoinerState_OT_JOINER_STATE_ENTRUST`](crate::otsys::otJoinerState_OT_JOINER_STATE_ENTRUST).
    Entrust = otJoinerState_OT_JOINER_STATE_ENTRUST as isize,

    /// Functional equivalent of [`otsys::otJoinerState_OT_JOINER_STATE_JOINED`](crate::otsys::otJoinerState_OT_JOINER_STATE_JOINED).
    Joined = otJoinerState_OT_JOINER_STATE_JOINED as isize,
}

impl From<otJoinerState> for JoinerState {
    fn from(x: otJoinerState) -> Self {
        use num::FromPrimitive;
        Self::from_u32(x).expect(format!("Unknown otJoinerState value: {}", x).as_str())
    }
}

impl From<JoinerState> for otJoinerState {
    fn from(x: JoinerState) -> Self {
        x as otJoinerState
    }
}

/// Methods from the [OpenThread "Joiner" Module][1].
///
/// [1]: https://openthread.io/reference/group/api-joiner
pub trait Joiner {
    /// Functional equivalent of
    /// [`otsys::otJoinerStart`](crate::otsys::otJoinerStart).
    fn joiner_start<'a, F: FnOnce(Result) + 'a>(
        &self,
        pskd: &str,
        provisioning_url: Option<&str>,
        vendor_name: Option<&str>,
        vendor_model: Option<&str>,
        vendor_sw_version: Option<&str>,
        vendor_data: Option<&str>,
        callback: F,
    ) -> Result;

    /// Functional equivalent of
    /// [`otsys::otJoinerStop`](crate::otsys::otJoinerStop).
    fn joiner_stop(&self);

    /// Functional equivalent of
    /// [`otsys::otJoinerGetState`](crate::otsys::otJoinerGetState).
    fn joiner_get_state(&self) -> JoinerState;

    /// Similar to [`joiner_start()`], but as an async method.
    ///
    /// Note that the current implementation of this method will start the joining
    /// process immediately, rather than lazily.
    fn joiner_start_async(
        &self,
        pskd: &str,
        provisioning_url: Option<&str>,
        vendor_name: Option<&str>,
        vendor_model: Option<&str>,
        vendor_sw_version: Option<&str>,
        vendor_data: Option<&str>,
    ) -> futures::channel::oneshot::Receiver<Result> {
        use futures::channel::oneshot::*;
        let (sender, receiver) = channel();
        let sender = std::sync::Arc::new(parking_lot::Mutex::new(Some(sender)));
        let sender_clone = sender.clone();
        if let Err(err) = self.joiner_start(
            pskd,
            provisioning_url,
            vendor_name,
            vendor_model,
            vendor_sw_version,
            vendor_data,
            move |result| {
                let _ = sender_clone
                    .lock()
                    .take()
                    .and_then(move |x: Sender<Result>| x.send(result).ok());
            },
        ) {
            sender.lock().take().and_then(move |x: Sender<Result>| x.send(Err(err)).ok());
        }
        receiver
    }
}

impl<T: Joiner + Boxable> Joiner for ot::Box<T> {
    fn joiner_start<'a, F: FnOnce(Result) + 'a>(
        &self,
        pskd: &str,
        provisioning_url: Option<&str>,
        vendor_name: Option<&str>,
        vendor_model: Option<&str>,
        vendor_sw_version: Option<&str>,
        vendor_data: Option<&str>,
        callback: F,
    ) -> Result {
        self.as_ref().joiner_start(
            pskd,
            provisioning_url,
            vendor_name,
            vendor_model,
            vendor_sw_version,
            vendor_data,
            callback,
        )
    }

    fn joiner_stop(&self) {
        self.as_ref().joiner_stop()
    }

    fn joiner_get_state(&self) -> JoinerState {
        self.as_ref().joiner_get_state()
    }
}

impl Joiner for Instance {
    fn joiner_start<'a, F: FnOnce(Result) + 'a>(
        &self,
        pskd: &str,
        provisioning_url: Option<&str>,
        vendor_name: Option<&str>,
        vendor_model: Option<&str>,
        vendor_sw_version: Option<&str>,
        vendor_data: Option<&str>,
        callback: F,
    ) -> Result {
        unsafe extern "C" fn _ot_joiner_callback(
            error: otError,
            context: *mut ::std::os::raw::c_void,
        ) {
            trace!("_ot_joiner_callback");

            // Reconstitute a reference to our closure.
            let instance = Instance::ref_from_ot_ptr(context as *mut otInstance).unwrap();

            if let Some(func) = instance.borrow_backing().joiner_fn.take() {
                func(Error::from(error).into_result());
            }
        }

        let pskd = CString::new(pskd).map_err(|_| Error::InvalidArgs)?;
        let provisioning_url =
            provisioning_url.map(CString::new).transpose().map_err(|_| Error::InvalidArgs)?;
        let vendor_name =
            vendor_name.map(CString::new).transpose().map_err(|_| Error::InvalidArgs)?;
        let vendor_model =
            vendor_model.map(CString::new).transpose().map_err(|_| Error::InvalidArgs)?;
        let vendor_sw_version =
            vendor_sw_version.map(CString::new).transpose().map_err(|_| Error::InvalidArgs)?;
        let vendor_data =
            vendor_data.map(CString::new).transpose().map_err(|_| Error::InvalidArgs)?;

        let pskd_ptr = pskd.as_bytes_with_nul().as_ptr() as *const ::std::os::raw::c_char;
        let provisioning_url_ptr =
            provisioning_url.as_ref().map(|x| x.as_bytes_with_nul().as_ptr()).unwrap_or(null())
                as *const ::std::os::raw::c_char;
        let vendor_name_ptr =
            vendor_name.as_ref().map(|x| x.as_bytes_with_nul().as_ptr()).unwrap_or(null())
                as *const ::std::os::raw::c_char;
        let vendor_model_ptr =
            vendor_model.as_ref().map(|x| x.as_bytes_with_nul().as_ptr()).unwrap_or(null())
                as *const ::std::os::raw::c_char;
        let vendor_sw_version_ptr =
            vendor_sw_version.as_ref().map(|x| x.as_bytes_with_nul().as_ptr()).unwrap_or(null())
                as *const ::std::os::raw::c_char;
        let vendor_data_ptr =
            vendor_data.as_ref().map(|x| x.as_bytes_with_nul().as_ptr()).unwrap_or(null())
                as *const ::std::os::raw::c_char;

        let fn_box = Some(Box::new(callback) as Box<dyn FnOnce(Result) + 'a>);

        let ret = Error::from(unsafe {
            // Make sure our object eventually gets cleaned up.
            // Here we must also transmute our closure to have a 'static lifetime.
            // We need to do this because the borrow checker cannot infer the
            // proper lifetime for the singleton instance backing, but
            // this is guaranteed by the API.
            self.borrow_backing().joiner_fn.set(std::mem::transmute::<
                Option<Box<dyn FnOnce(Result) + 'a>>,
                Option<Box<dyn FnOnce(Result) + 'static>>,
            >(fn_box));

            otJoinerStart(
                self.as_ot_ptr(),
                pskd_ptr,
                provisioning_url_ptr,
                vendor_name_ptr,
                vendor_model_ptr,
                vendor_sw_version_ptr,
                vendor_data_ptr,
                Some(_ot_joiner_callback),
                self.as_ot_ptr() as *mut ::std::os::raw::c_void,
            )
        })
        .into_result();

        if ret.is_err() {
            // There was an error during otJoinerStart,
            // Make sure the callback is cleaned up.
            self.borrow_backing().joiner_fn.take();
        }

        ret
    }

    fn joiner_stop(&self) {
        unsafe { otJoinerStop(self.as_ot_ptr()) }

        // Make sure the callback is cleaned up.
        self.borrow_backing().joiner_fn.take();
    }

    fn joiner_get_state(&self) -> JoinerState {
        unsafe { otJoinerGetState(self.as_ot_ptr()) }.into()
    }
}
