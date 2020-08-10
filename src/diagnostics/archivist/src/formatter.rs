// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    anyhow::{format_err, Error},
    diagnostics_data::Schema,
    fidl_fuchsia_diagnostics,
    fuchsia_zircon::{self as zx, HandleBased},
};

pub fn write_schema_to_formatted_content(
    contents: Schema<String>,
    format: &fidl_fuchsia_diagnostics::Format,
) -> Result<fidl_fuchsia_diagnostics::FormattedContent, Error> {
    match format {
        fidl_fuchsia_diagnostics::Format::Json => {
            let content_string = serde_json::to_string_pretty(&contents)?;
            let vmo_size: u64 = content_string.len() as u64;

            let dump_vmo_result: Result<zx::Vmo, Error> = zx::Vmo::create(vmo_size as u64)
                .map_err(|s| format_err!("error creating buffer, zx status: {}", s));

            dump_vmo_result.and_then(|dump_vmo| {
                dump_vmo
                    .write(content_string.as_bytes(), 0)
                    .map_err(|s| format_err!("error writing buffer, zx status: {}", s))?;

                let client_vmo = dump_vmo.duplicate_handle(zx::Rights::READ | zx::Rights::BASIC)?;

                let mem_buffer = fidl_fuchsia_mem::Buffer { vmo: client_vmo, size: vmo_size };
                Ok(fidl_fuchsia_diagnostics::FormattedContent::Json(mem_buffer))
            })
        }
        fidl_fuchsia_diagnostics::Format::Text => {
            Err(format_err!("Text formatting not supported for lifecycle events."))
        }
    }
}
