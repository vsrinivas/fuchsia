// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::errors::VerifyError;

/// Dummy function to indicate where health verification will eventually go,
/// and how to handle associated errors. This is NOT to be confused with
/// verified execution; health verification is a different process we use
/// to determine if we should give up on the backup slot.
pub async fn do_health_verification() -> Result<(), VerifyError> {
    Ok(())
}
