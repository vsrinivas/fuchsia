// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod controller;

use {
    crate::toolkit::controller::{
        blobfs::BlobFsExtractController, fvm::FvmExtractController,
        package::PackageExtractController, zbi::ZbiExtractController,
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
            "/tool/fvm/extract" => FvmExtractController::default(),
            "/tool/package/extract" => PackageExtractController::default(),
            "/tool/zbi/extract" => ZbiExtractController::default(),
        }
    ),
    vec![PluginDescriptor::new("CorePlugin")]
);

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::toolkit::controller::{
            fvm::FvmExtractRequest, package::PackageExtractRequest, zbi::ZbiExtractRequest,
        },
        tempfile::tempdir,
    };

    #[test]
    fn test_zbi_extractor_empty_zbi() {
        let store_dir = tempdir().unwrap();
        let uri = store_dir.into_path().into_os_string().into_string().unwrap();
        let model = Arc::new(DataModel::connect(uri).unwrap());
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
    fn test_package_extractor_invalid_url() {
        let store_dir = tempdir().unwrap();
        let uri = store_dir.into_path().into_os_string().into_string().unwrap();
        let model = Arc::new(DataModel::connect(uri).unwrap());
        let package_controller = PackageExtractController::default();
        let output_dir = tempdir().unwrap();
        let output_path = output_dir.path();
        let request = PackageExtractRequest {
            url: "fake_path".to_string(),
            output: output_path.to_str().unwrap().to_string(),
        };
        let query = serde_json::to_value(request).unwrap();
        let response = package_controller.query(model, query);
        assert_eq!(response.is_ok(), false);
    }

    #[test]
    fn test_fvm_extractor_empty_fvm() {
        let store_dir = tempdir().unwrap();
        let uri = store_dir.into_path().into_os_string().into_string().unwrap();
        let model = Arc::new(DataModel::connect(uri).unwrap());
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
