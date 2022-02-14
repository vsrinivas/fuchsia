// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::prelude_internal::*;
use std::os::raw::c_int;

/// Methods from the [OpenThread "CLI" Module](https://openthread.io/reference/group/api-cli).
pub trait Cli {
    /// Functional equivalent of [`otsys::otCliInputLine`](crate::otsys::otCliInputLine).
    fn cli_input_line(&self, line: &CStr);

    /// Functional equivalent of [`otsys::otCliInit`](crate::otsys::otCliInit).
    fn cli_init<'a, F>(&self, output_callback: F)
    where
        F: FnMut(&CStr) + 'a;
}

impl<T: Cli + ot::Boxable> Cli for ot::Box<T> {
    fn cli_input_line(&self, line: &CStr) {
        self.as_ref().cli_input_line(line);
    }

    fn cli_init<'a, F>(&self, output_callback: F)
    where
        F: FnMut(&CStr) + 'a,
    {
        self.as_ref().cli_init(output_callback);
    }
}

impl Cli for Instance {
    fn cli_input_line(&self, line: &CStr) {
        unsafe { otCliInputLine(line.as_ptr() as *mut c_char) }
    }

    fn cli_init<'a, F>(&self, f: F)
    where
        F: FnMut(&CStr) + 'a,
    {
        unsafe extern "C" fn _ot_cli_output_callback<'a, F: FnMut(&CStr) + 'a>(
            context: *mut ::std::os::raw::c_void,
            line: *const c_char,
            _: *mut otsys::__va_list_tag,
        ) -> c_int {
            let line = CStr::from_ptr(line);

            trace!("_ot_cli_output_callback: {:?}", line);

            // Reconstitute a reference to our closure.
            let sender = &mut *(context as *mut F);

            sender(line);

            line.to_bytes().len().try_into().unwrap()
        }

        let mut x = Box::new(f);
        let (fn_ptr, fn_box, cb): (_, _, otCliOutputCallback) = (
            x.as_mut() as *mut F as *mut ::std::os::raw::c_void,
            Some(x as Box<dyn FnMut(&CStr) + 'a>),
            Some(_ot_cli_output_callback::<F>),
        );

        unsafe {
            otCliInit(self.as_ot_ptr(), cb, fn_ptr);

            // Make sure our object eventually gets cleaned up.
            // Here we must also transmute our closure to have a 'static lifetime.
            // We need to do this because the borrow checker cannot infer the
            // proper lifetime for the singleton instance backing, but
            // this is guaranteed by the API.
            self.borrow_backing().cli_output_fn.set(std::mem::transmute::<
                Option<Box<dyn FnMut(&'_ CStr) + 'a>>,
                Option<Box<dyn FnMut(&'_ CStr) + 'static>>,
            >(fn_box));
        }
    }
}
