// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// dummy main. We do not copy this binary to fuchsia, only tests.
fn main() {}

#[cfg(test)]
mod tests {
    use fidl_fuchsia_diagnostics_inspect::{
        DisplaySettings, FormatSettings, ReaderMarker, TextSettings,
    };

    use fuchsia_async as fasync;
    use fuchsia_component::client::connect_to_service;

    #[fasync::run_singlethreaded(test)]
    async fn test_all_reader_endpoints() {
        let control = connect_to_service::<ReaderMarker>().unwrap();
        control.clear_selectors().unwrap();
        let format_settings =
            FormatSettings { format: Some(DisplaySettings::Text(TextSettings { indent: 4 })) };

        // No point reading the returned data yet since this test is hermetic and
        // doesn't generate any /hub information for the inspect service to read.
        let format_response = control.format(format_settings).await;
        assert!(format_response.is_ok());
        assert!(format_response.unwrap().is_ok());
    }
}
