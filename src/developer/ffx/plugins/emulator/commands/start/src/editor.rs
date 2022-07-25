// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{bail, Context, Result};
use ffx_emulator_config::EmulatorConfiguration;
use ffx_emulator_engines::process_flags_from_str;
use serde_json;
use std::{
    env::{temp_dir, var},
    fs::File,
    io::{Read, Write},
    process::Command,
    str,
};

const TEMP_FILE_HEADER: &'static str = r#"
{{! This is a comment. Anything contained in double-brackets like these will be ignored. }}
{{! The flags below will be used to start up the emulator after this editor is closed.
    If you would like to keep this configuration for later reuse, you can save any
    changes to this temporary file, and also save off a copy in a persistent location.
    Then you can pass that file to the emulator launcher later by calling:

        ffx emu start --config <your_file>
}}
"#;

pub(crate) fn edit_configuration(emu_config: &mut EmulatorConfiguration) -> Result<()> {
    let flags = emu_config.flags.clone();
    // Start by generating a temporary file to hold the flags during editing.
    let editor = var("EDITOR").unwrap_or("/usr/bin/nano".to_string());
    let mut file_path = temp_dir();
    file_path.push("editable");
    {
        let mut temp = File::create(&file_path)
            .context("Could not create temp file for editing configuration.")?;
        write!(temp, "{}", TEMP_FILE_HEADER).context("Problem writing header to temp file.")?;
        write!(
            temp,
            "{}",
            serde_json::to_string_pretty(&flags).context("Couldn't pretty print flags.")?
        )
        .context("Problem prepopulating flags in editor.")?;
    }

    // Launch the editor on the temporary file.
    let status = Command::new(&editor)
        .arg(&file_path)
        .status()
        .context(format!("Something went wrong launching the editor {}.", &editor))?;

    // Bail if the editor was cancelled or encountered an error.
    if !status.success() {
        bail!("Editor terminated with non-zero exit code. Cancelling emulator start.");
    }

    // Read the new data from the temporary file, and load it into the configuration.
    let mut file = File::open(file_path).context("Could not open file")?;
    let mut edited_text = String::new();
    file.read_to_string(&mut edited_text)
        .context("Error reading new flags from temporary file.")?;
    let new_flag_data =
        process_flags_from_str(&edited_text, emu_config).context("Error processing user input.")?;
    emu_config.flags = new_flag_data;
    Ok(())
}
