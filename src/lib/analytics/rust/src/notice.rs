// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub use crate::user_status::*;

use std::io::Write;

// pub for ffx testing
pub const FULL_NOTICE: &str =
    "Welcome to Fuchsia! - https://fuchsia.dev

Fuchsia core developer tools use Google Analytics to report feature usage
statistics and basic crash reports. Google may examine the collected data
in aggregate to help improve these tools, other Fuchsia tools, and the
Fuchsia SDK.

Analytics are not sent on this very first run. To disable reporting, type

    ffx config analytics disable

To display the current setting and what is collected, type

    ffx config analytics

If you opt out of analytics, an opt-out event will be sent, and then no
further information will be sent by the core developer tools to Google.

By using Fuchsia core developer tools, you agree to the Google Terms of Service.
Note: The Google Privacy Policy describes how data is handled in your use of
this service.

Read about data we send with crash reports:
https://fuchsia.dev/reference/crash-reporting

See Google's privacy policy:
https://policies.google.com/privacy
";

// Number of lines in FULL_NOTICE.
// pub for ffx testing.
pub const ANALYTICS_NOTICE_LINE_COUNT: usize = 25;

const BRIEF_NOTICE: &str = "Welcome to use ffx!

As part of the Fuchsia core developer tools, this tool uses Google Analytics to
report feature usage statistics and basic crash reports. Google may examine the
collected data in aggregate to help improve the core developer tools, other
Fuchsia tools, and the Fuchsia SDK.

To disable reporting, type

    ffx config analytics disable

To display the current setting, a full list of tools sharing this setting, and
what is collected, type

    ffx config analytics

If you opt out of analytics, an opt-out event will be sent, and then no further
information will be sent by the core developer tools to Google.

Read about data we send with crash reports:
https://fuchsia.dev/reference/crash-reporting

See Google's privacy policy:
https://policies.google.com/privacy
";

pub fn show_analytics_notice<W: Write>(mut writer: W) {
    if is_test_env() {
        return;
    }

    if is_new_user() {
        set_has_opened_ffx();
        let uuid = uuid();
        opt_in_status(&true); // TODO do we log on the first run? If so, don't set this until the second run
        print_long_notice(&mut writer);
    } else if !has_opened_ffx_previously() {
        set_has_opened_ffx();
        print_short_notice(&mut writer);
    }
}

fn print_short_notice<W: Write>(mut writer: W) {
    writeln!(&mut writer, "{}", BRIEF_NOTICE);
}

fn print_long_notice<W: Write>(mut writer: W) {
    writeln!(&mut writer, "{}", FULL_NOTICE);
}
