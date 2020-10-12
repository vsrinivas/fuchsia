// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A macro to generate pseudo directory trees using a small DSL.

use fuchsia_zircon::Status;

/// A helper function used by the `pseudo_directory!` macro, to report nice errors in case
/// add_entry() fails.
#[doc(hidden)]
pub fn unwrap_add_entry_span(entry: &str, location: &str, res: Result<(), Status>) {
    if res.is_ok() {
        return;
    }

    let status = res.unwrap_err();
    let text;
    let error_text = match status {
        Status::ALREADY_EXISTS => "Duplicate entry name.",
        _ => {
            text = format!("`add_entry` failed with an unexpected status: {}", status);
            &text
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

#[cfg(test)]
mod tests {
    use {fidl_fuchsia_io::MAX_FILENAME, vfs_macros::pseudo_directory_max_filename};

    #[test]
    fn macros_max_filename_constant() {
        // `pseudo_directory!` needs access to [`fidl_fuchsia_io::MAX_FILENAME`], but the
        // [`fidl_fuchsia_io`] crate is not available on the host.  So we hardcode the constant value
        // in there and then make sure that the values are in sync.
        let in_macros = pseudo_directory_max_filename! {};
        assert!(
            MAX_FILENAME == in_macros,
            "\n`fidl_fuchsia_io::MAX_FILENAME` and the value hardcoded in \
             `pseudo-fs/macros/src/lib.rs` have diverged.\n\
             Please update the `MAX_FILENAME` value in `pseudo-fs/macros/src/lib.rs`.\n\
             `fidl_fuchsia_io::MAX_FILENAME`: {}\n\
             pseudo-fs/macros/src/lib.rs:MAX_FILENAME: {}",
            MAX_FILENAME,
            in_macros
        );
    }
}
