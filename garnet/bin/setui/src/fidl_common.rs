// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[macro_export]
macro_rules! fidl_hanging_get_responder {
    ($setting_type:ty, $responder_type:ty, $marker_debug_name:expr) => {
        use fidl::endpoints::ServiceMarker;
        use crate::switchboard::base::FidlResponseErrorLogger;

        impl Sender<$setting_type> for $responder_type {
            fn send_response(self, data: $setting_type) {
                self.send(data).log_fidl_response_error($marker_debug_name);
            }
        }
    };
}

#[macro_export]
macro_rules! fidl_hanging_get_result_responder {
    ($setting_type:ty, $responder_type:ty, $marker_debug_name:expr) => {
        impl Sender<$setting_type> for $responder_type {
            fn send_response(self, data: $setting_type) {
                self.send(&mut Ok(data)).log_fidl_response_error($marker_debug_name);
            }
        }
    };
}
