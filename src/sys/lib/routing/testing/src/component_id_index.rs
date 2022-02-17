// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    ::component_id_index::Index, anyhow::Result, fidl::encoding::encode_persistent_with_context,
    fidl_fuchsia_component_internal as fcomponent_internal, std::convert::TryFrom, std::io::Write,
    tempfile::NamedTempFile,
};

/// Makes a temporary component ID index file with contents from the given `index`.
pub fn make_index_file(index: Index) -> Result<NamedTempFile> {
    let mut tmp_file = NamedTempFile::new()?;
    tmp_file.write_all(
        encode_persistent_with_context(
            &fidl::encoding::Context { wire_format_version: fidl::encoding::WireFormatVersion::V2 },
            &mut fcomponent_internal::ComponentIdIndex::try_from(index)?,
        )?
        .as_ref(),
    )?;
    Ok(tmp_file)
}
