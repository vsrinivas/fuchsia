// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A macro to generate pseudo directory trees using a small DSL.

use {crate::directory::entry::DirectoryEntry, fuchsia_zircon::Status};

/// A helper function used by the pseudo_directory! macro, to report nice errors in case
/// add_entry() fails.
#[doc(hidden)]
pub fn unwrap_add_entry_span<'entry>(
    entry: &str,
    location: &str,
    res: Result<(), (Status, Box<dyn DirectoryEntry + 'entry>)>,
) {
    if res.is_ok() {
        return;
    }

    let (status, _) = res.unwrap_err();
    let custom_error_text;
    let error_text = match status {
        Status::INVALID_ARGS => {
            "Entry name is too long - longer than fidl_fuchsia_io::MAX_FILENAME."
        }
        Status::ALREADY_EXISTS => "Duplicate entry name.",
        _ => {
            custom_error_text = format!("Unexpected status: {}", status);
            &custom_error_text
        }
    };

    panic!(
        "Pseudo directory tree generated via pseudo_directory! macro\n\
         {}\n\
         {}\n\
         Entry: '{}'",
        location, error_text, entry
    );
}
