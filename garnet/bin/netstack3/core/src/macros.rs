// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Macros used in Netstack3.

macro_rules! log_unimplemented {
    ($nocrash:expr, $fmt:expr $(,$arg:expr)*) => {{

        #[cfg(feature = "crash_on_unimplemented")]
        unimplemented!($fmt, $($arg),*);

        #[cfg(not(feature = "crash_on_unimplemented"))]
        // Clippy doesn't like blocks explicitly returning ().
        #[allow(clippy::unused_unit)]
        {
            // log doesn't play well with the new macro system; it expects all
            // of its macros to be in scope
            use ::log::*;
            trace!(concat!("Unimplemented: ", $fmt), $($arg),*);
            $nocrash
        }
    }};

    ($nocrash:expr, $fmt:expr $(,$arg:expr)*,) =>{
        log_unimplemented!($nocrash, $fmt $(,$arg)*)
    };
}

macro_rules! increment_counter {
    ($ctx:ident, $key:expr) => {
        #[cfg(test)]
        $ctx.state_mut().test_counters.increment($key);
    };
}
