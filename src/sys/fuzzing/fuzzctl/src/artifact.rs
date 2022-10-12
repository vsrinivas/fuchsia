// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::input::Input,
    crate::util::digest_path,
    anyhow::{Context as _, Result},
    fidl_fuchsia_fuzzer::{Input as FidlInput, Result_ as FuzzResult},
    fuchsia_zircon_status as zx,
    std::fs,
    std::path::{Path, PathBuf},
};

/// Combines the results of a long-running fuzzer workflow.
pub struct Artifact {
    /// If `status` is OK, indicates the outcome of the workflow; otherwise undefined.
    pub result: FuzzResult,

    /// The path to which the fuzzer input, if any, has been saved.
    pub path: Option<PathBuf>,

    /// Indicates an error encountered by the engine itself, or `zx::Status::OK`.
    pub status: zx::Status,
}

impl Artifact {
    /// Returns an artifact for a workflow that completed without producing results or data.
    pub fn ok() -> Self {
        Self { result: FuzzResult::NoErrors, path: None, status: zx::Status::OK }
    }

    /// Returns an artifact for a workflow that was canceled before completing.
    pub fn canceled() -> Self {
        let mut artifact = Self::ok();
        artifact.status = zx::Status::CANCELED;
        artifact
    }

    /// Returns an artifact for a workflow that produced a result without data.
    pub fn from_result(result: FuzzResult) -> Self {
        let mut artifact = Self::ok();
        artifact.result = result;
        artifact
    }

    /// Returns an artifact for a workflow that produced results and data.
    ///
    /// This will attempt to receive the data and save it locally, and return an error if unable to
    /// do so.
    pub async fn try_from_input<P: AsRef<Path>>(
        result: FuzzResult,
        fidl_input: FidlInput,
        artifact_dir: P,
    ) -> Result<Self> {
        let mut artifact = Self::from_result(result);
        let input =
            Input::try_receive(fidl_input).await.context("failed to receive artifact data")?;
        let path = digest_path(artifact_dir, Some(result), &input.data);
        fs::write(&path, input.data)
            .with_context(|| format!("failed to write artifact to '{}'", path.to_string_lossy()))?;
        artifact.path = Some(path);
        Ok(artifact)
    }
}

#[cfg(test)]
mod tests {
    use {
        super::Artifact,
        crate::input::InputPair,
        crate::util::digest_path,
        anyhow::Result,
        fidl_fuchsia_fuzzer::Result_ as FuzzResult,
        fuchsia_fuzzctl_test::{verify_saved, Test},
        futures::join,
    };

    #[fuchsia::test]
    async fn test_try_from_input() -> Result<()> {
        let test = Test::try_new()?;
        let saved_dir = test.create_dir("saved")?;

        let input_pair = InputPair::try_from_data(b"data".to_vec())?;
        let (fidl_input, input) = input_pair.as_tuple();
        let send_fut = input.send();
        let save_fut = Artifact::try_from_input(FuzzResult::Crash, fidl_input, &saved_dir);
        let results = join!(send_fut, save_fut);
        assert!(results.0.is_ok());
        assert!(results.1.is_ok());
        let saved = digest_path(&saved_dir, Some(FuzzResult::Crash), b"data");
        verify_saved(&saved, b"data")?;

        Ok(())
    }
}
