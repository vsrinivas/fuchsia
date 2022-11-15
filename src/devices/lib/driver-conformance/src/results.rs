// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Result},
    flate2::write::GzEncoder,
    flate2::Compression,
    serde::{Deserialize, Serialize},
    serde_json,
    std::fs,
    std::path::{Path, PathBuf},
    std::str::FromStr,
    tar,
    test_output_directory::{ArtifactSubDirectory, MaybeUnknown, Outcome, TestRunResult},
    walkdir::WalkDir,
};

#[derive(Serialize, Deserialize, Debug, PartialEq, Clone, Eq, Hash)]
#[serde(rename_all = "SCREAMING_SNAKE_CASE")]
pub enum TestType {
    Suite,
    Case,
}

#[derive(Serialize, Deserialize, Debug, PartialEq, Clone, Eq, Hash, Copy)]
#[serde(rename_all = "SCREAMING_SNAKE_CASE")]
pub enum ProcessorArch {
    X64,
    Arm64,
}

#[derive(Serialize, Deserialize, Debug, PartialEq, Clone, Eq, Hash, Copy)]
#[serde(rename_all = "SCREAMING_SNAKE_CASE")]
pub enum SourceProvider {
    Github,
    Gerrit,
}

impl FromStr for SourceProvider {
    type Err = anyhow::Error;

    fn from_str(value: &str) -> Result<SourceProvider> {
        match value.to_lowercase().as_str() {
            "github" => Ok(SourceProvider::Github),
            "gerrit" => Ok(SourceProvider::Gerrit),
            _ => Err(anyhow!("{} is not a supported version control host.", value)),
        }
    }
}

#[derive(Serialize, Deserialize, Debug, PartialEq, Clone, Eq, Hash)]
pub struct SourceCode {
    #[serde(rename = "type")]
    pub provider: SourceProvider,
    pub location: String,
}

#[derive(Serialize, Deserialize, Debug, PartialEq, Clone, Eq, Hash)]
pub struct Binary {
    #[serde(rename = "type")]
    pub arch: ProcessorArch,
    pub location: String,
}

#[derive(Serialize, Deserialize, Debug, PartialEq, Clone, Eq, Hash)]
#[serde(tag = "type", content = "metadata", rename_all = "SCREAMING_SNAKE_CASE")]
pub enum DriverArtifact {
    Binary(Binary),
    SourceCode(SourceCode),
}

#[derive(Serialize, Deserialize, Debug, PartialEq, Clone, Eq, Hash)]
pub struct TestArtifact {
    #[serde(rename = "type")]
    pub artifact_type: test_output_directory::MaybeUnknown<test_output_directory::ArtifactType>,
    pub location: String,
}

#[derive(Serialize, Deserialize, Debug, PartialEq, Clone, Eq, Hash)]
pub struct TestResult {
    pub suite_id: String,
    pub test_id: String,
    pub pass: bool,
    #[serde(rename = "type")]
    pub test_type: TestType,
    pub artifacts: Box<Vec<TestArtifact>>,
}

#[derive(Serialize, Deserialize, Debug)]
pub struct SuperjetManifest {
    pub name: String,
    pub version: String,
    pub pass: bool,
    pub artifacts: Box<Vec<DriverArtifact>>,
    pub licenses: Box<Vec<String>>,
    pub test_results: Box<Vec<TestResult>>,
    #[serde(skip)]
    #[serde(default = "get_temp_dir")]
    working_dir: Box<tempfile::TempDir>,
}

fn get_temp_dir() -> Box<tempfile::TempDir> {
    Box::new(tempfile::TempDir::new().unwrap())
}

impl SuperjetManifest {
    pub fn new(name: String, version: String) -> SuperjetManifest {
        SuperjetManifest {
            name: name.to_string(),
            version: version.to_string(),
            pass: false,
            artifacts: Box::new(vec![]),
            licenses: Box::new(vec![]),
            test_results: Box::new(vec![]),
            working_dir: get_temp_dir(),
        }
    }

    fn copy_or_tar(source_path: &Path, destination_path: &Path) -> Result<()> {
        if let Some(parent) = destination_path.parent() {
            fs::create_dir_all(parent)?;
        }
        if source_path.is_file() {
            fs::copy(source_path, destination_path)?;
        } else if source_path.is_dir() {
            // TODO: Tar this so it is a single file.
            // This is possible according to ArtifactType::Custom defined in:
            // //src/sys/run_test_suite/directory/src/lib.rs
            return Err(anyhow!("We do not support copying a directory to the tarball yet."));
        } else {
            return Err(anyhow!("The source is not a file or directory."));
        }
        Ok(())
    }

    fn driver_dest_path(artifact_path: &Path, _: &Option<PathBuf>) -> Result<PathBuf> {
        Ok(Path::new("artifacts").join(artifact_path.file_name().unwrap()))
    }

    fn result_dest_path(artifact_path: &Path, _: &Option<PathBuf>) -> Result<PathBuf> {
        Ok(Path::new("test_results")
            .join(artifact_path.parent().unwrap().file_name().unwrap())
            .join(artifact_path.file_name().unwrap()))
    }

    fn license_dest_path(artifact_path: &Path, base: &Option<PathBuf>) -> Result<PathBuf> {
        if let Some(base) = base {
            Ok(Path::new("licenses").join(artifact_path.strip_prefix(base.as_path())?))
        } else {
            Err(anyhow!("A license path must provide its base/parent path."))
        }
    }

    fn import_artifact<C>(
        &self,
        artifact_path: &Path,
        dest_path_fn: C,
        source_base: &Option<PathBuf>,
    ) -> Result<PathBuf>
    where
        C: Fn(&Path, &Option<PathBuf>) -> Result<PathBuf>,
    {
        let working_path = self.working_dir.path();
        let in_archive_path = dest_path_fn(artifact_path, source_base)?;
        let destination_path = working_path.join(in_archive_path.as_path());
        SuperjetManifest::copy_or_tar(artifact_path, &destination_path)?;
        Ok(in_archive_path)
    }

    pub fn make_tar(&self, destination: &Path) -> Result<()> {
        let manifest_str = serde_json::to_string(self).unwrap();
        fs::write(self.working_dir.path().join("manifest.json"), &manifest_str)?;
        let tar_gz =
            fs::OpenOptions::new().write(true).create(true).truncate(true).open(destination)?;
        let enc = GzEncoder::new(tar_gz, Compression::default());
        let mut tar = tar::Builder::new(enc);
        tar.append_dir_all("", self.working_dir.path())?;
        Ok(tar.finish()?)
    }

    fn parse_artifacts(
        &mut self,
        artifacts: &mut Vec<TestArtifact>,
        outcome: &MaybeUnknown<Outcome>,
        artifact_dir: &ArtifactSubDirectory,
    ) -> Result<bool> {
        let mut passed = false;
        if let MaybeUnknown::Known(outcome) = outcome {
            passed = outcome == &Outcome::Passed;
        };
        for (path, art_info) in artifact_dir.artifact_iter() {
            if let Some(source_path) = artifact_dir.path_to_artifact(path) {
                let destination_path = self.import_artifact(
                    source_path.as_path(),
                    SuperjetManifest::result_dest_path,
                    &None,
                )?;
                artifacts.push(TestArtifact {
                    location: destination_path.into_os_string().into_string().unwrap(),
                    artifact_type: art_info.artifact_type.clone(),
                })
            }
        }
        Ok(passed)
    }

    pub fn from_results(&mut self, ffx_test_result_dir: &Path) -> Result<()> {
        let run_results = TestRunResult::from_dir(ffx_test_result_dir)?;

        // Set the overall pass/fail status.
        if let MaybeUnknown::Known(outcome) = run_results.common.outcome {
            self.pass = outcome == Outcome::Passed;
        }

        for suite in &run_results.suites {
            let mut suite_artifacts: Vec<TestArtifact> = Vec::new();
            let passed = self.parse_artifacts(
                &mut suite_artifacts,
                &suite.common.outcome,
                &suite.common.artifact_dir,
            )?;

            self.test_results.push(TestResult {
                artifacts: Box::new(suite_artifacts),
                test_id: suite.common.name.to_string(),
                test_type: TestType::Suite,
                suite_id: suite.common.name.to_string(),
                pass: passed,
            });

            for case in &suite.cases {
                let mut test_artifacts: Vec<TestArtifact> = Vec::new();
                let passed = self.parse_artifacts(
                    &mut test_artifacts,
                    &case.common.outcome,
                    &case.common.artifact_dir,
                )?;

                self.test_results.push(TestResult {
                    artifacts: Box::new(test_artifacts),
                    test_id: case.common.name.to_string(),
                    test_type: TestType::Case,
                    suite_id: suite.common.name.to_string(),
                    pass: passed,
                });
            }
        }
        Ok(())
    }

    pub fn add_driver_binary<T: AsRef<Path>>(
        &mut self,
        driver_binary_path: T,
        arch: ProcessorArch,
    ) -> Result<()> {
        let source_path = fs::canonicalize(driver_binary_path.as_ref())?;
        let destination_path =
            self.import_artifact(source_path.as_path(), SuperjetManifest::driver_dest_path, &None)?;
        self.artifacts.push(DriverArtifact::Binary(Binary {
            arch: arch,
            location: destination_path.into_os_string().into_string().unwrap(),
        }));
        Ok(())
    }

    pub fn add_driver_source(
        &mut self,
        driver_source: &String,
        host: SourceProvider,
    ) -> Result<()> {
        self.artifacts.push(DriverArtifact::SourceCode(SourceCode {
            provider: host,
            location: driver_source.to_string(),
        }));
        Ok(())
    }

    pub fn add_licenses<T: AsRef<Path>>(&mut self, license_dir: T) -> Result<()> {
        WalkDir::new(&license_dir)
            .into_iter()
            .filter_map(|e| match e {
                Ok(val) => {
                    if val.file_type().is_file() {
                        Some(val)
                    } else {
                        None
                    }
                }
                _ => None,
            })
            .for_each(|entry| {
                let path = entry.path();
                let source_path = fs::canonicalize(path).unwrap();
                let destination_path = self
                    .import_artifact(
                        source_path.as_path(),
                        SuperjetManifest::license_dest_path,
                        &Some(license_dir.as_ref().to_path_buf()),
                    )
                    .unwrap();
                self.licenses.push(destination_path.into_os_string().into_string().unwrap());
            });
        Ok(())
    }
}

#[cfg(test)]
mod test {
    // These methods are collectively tested in //src/devices/lib/driver-conformance/src/lib.rs.
}
