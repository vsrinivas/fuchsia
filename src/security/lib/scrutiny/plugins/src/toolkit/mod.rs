// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod controller;

use {
    crate::toolkit::controller::{
        blobfs::BlobFsExtractController, far::FarMetaExtractController, fvm::FvmExtractController,
        zbi::ZbiExtractController, zbi_bootfs::ZbiExtractBootfsPackageIndex,
        zbi_bootfs::ZbiListBootfsController, zbi_cmdline::ZbiExtractCmdlineController,
    },
    scrutiny::prelude::*,
    std::sync::Arc,
};

plugin!(
    ToolkitPlugin,
    PluginHooks::new(
        collectors! {},
        controllers! {
            "/tool/blobfs/extract" => BlobFsExtractController::default(),
            "/tool/far/extract/meta" => FarMetaExtractController::default(),
            "/tool/fvm/extract" => FvmExtractController::default(),
            "/tool/zbi/extract" => ZbiExtractController::default(),
            "/tool/zbi/extract/cmdline" => ZbiExtractCmdlineController::default(),
            "/tool/zbi/list/bootfs" => ZbiListBootfsController::default(),
            "/tool/zbi/extract/bootfs/packages" => ZbiExtractBootfsPackageIndex::default(),
        }
    ),
    // The toolkit plugin takes no dependencies on the model. It is just a set
    // of utility controllers for auditing.
    vec![]
);

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::toolkit::controller::{
            fvm::FvmExtractRequest, zbi::ZbiExtractRequest, zbi_cmdline::ZbiExtractCmdlineRequest,
        },
        scrutiny_testing::fake::*,
        tempfile::tempdir,
    };

    #[test]
    fn test_zbi_extractor_empty_zbi() {
        let model = fake_data_model();
        let zbi_controller = ZbiExtractController::default();
        let input_dir = tempdir().unwrap();
        let input_path = input_dir.path().join("empty-zbi");
        let output_dir = tempdir().unwrap();
        let output_path = output_dir.path();
        let request = ZbiExtractRequest {
            input: input_path.to_str().unwrap().to_string(),
            output: output_path.to_str().unwrap().to_string(),
        };
        let query = serde_json::to_value(request).unwrap();
        let response = zbi_controller.query(model, query);
        assert_eq!(response.is_ok(), false);
    }

    #[test]
    fn test_zbi_cmdline_extractor_empty_zbi() {
        let model = fake_data_model();
        let zbi_controller = ZbiExtractCmdlineController::default();
        let input_dir = tempdir().unwrap();
        let input_path = input_dir.path().join("empty-zbi");
        let request = ZbiExtractCmdlineRequest { input: input_path.to_str().unwrap().to_string() };
        let query = serde_json::to_value(request).unwrap();
        let response = zbi_controller.query(model, query);
        assert_eq!(response.is_ok(), false);
    }

    #[test]
    fn test_fvm_extractor_empty_fvm() {
        let model = fake_data_model();
        let fvm_controller = FvmExtractController::default();
        let input_dir = tempdir().unwrap();
        let input_path = input_dir.path().join("empty-fvm");
        let output_dir = tempdir().unwrap();
        let output_path = output_dir.path();
        let request = FvmExtractRequest {
            input: input_path.to_str().unwrap().to_string(),
            output: output_path.to_str().unwrap().to_string(),
        };
        let query = serde_json::to_value(request).unwrap();
        let response = fvm_controller.query(model, query);
        assert_eq!(response.is_ok(), false);
    }
}
