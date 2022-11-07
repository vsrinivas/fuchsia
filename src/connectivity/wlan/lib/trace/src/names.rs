// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

macro_rules! trace_string_name_macro {
    ($name:ident, $s:literal) => {
        #[macro_export]
        macro_rules! $name {
            () => {
                $s
            };
        }
    };
}

trace_string_name_macro!(CATEGORY_WLAN, "wlan");
trace_string_name_macro!(NAME_WLANCFG_START, "wlancfg:start");
trace_string_name_macro!(NAME_WLANSTACK_START, "wlanstack:start");
