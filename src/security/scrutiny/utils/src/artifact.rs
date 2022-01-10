// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context, Result},
    log::warn,
    pathdiff::diff_paths,
    std::{
        collections::HashSet,
        fs,
        path::{Path, PathBuf},
    },
};

/// Interface for fetching raw bytes by file path.
pub trait ArtifactReader: Send + Sync {
    /// Read the raw bytes stored in filesystem location `path`.
    fn read_raw(&mut self, path: &str) -> Result<Vec<u8>>;

    /// Get the accumulated set of filesystem locations that have been read by
    /// this reader.
    fn get_deps(&self) -> HashSet<String>;
}

pub struct FileArtifactReader {
    build_path: PathBuf,
    artifact_path: PathBuf,
    deps: HashSet<String>,
}

/// The FileArtifactLoader retrieves package data and blobs directly from the
/// build artifacts on disk.
impl FileArtifactReader {
    pub fn new(build_path: &Path, artifact_path: &Path) -> Self {
        let build_path = match build_path.canonicalize() {
            Ok(path) => path,
            Err(err) => {
                warn!(
                    "File artifact reader failed to canonicalize build path: {:#?}: {}",
                    build_path,
                    err.to_string()
                );
                build_path.to_path_buf()
            }
        };
        let artifact_path = match artifact_path.canonicalize() {
            Ok(path) => path,
            Err(err) => {
                warn!(
                    "File artifact reader failed to canonicalize artifact path: {:#?}: {}",
                    artifact_path,
                    err.to_string()
                );
                artifact_path.to_path_buf()
            }
        };
        Self { build_path, artifact_path, deps: HashSet::new() }
    }

    fn absolute_from_absolute_or_artifact_relative(&self, path_str: &str) -> Result<String> {
        let path = Path::new(path_str);
        let artifact_relative_path_buf = if path.is_absolute() {
            diff_paths(path, &self.artifact_path)
            .ok_or(
                anyhow!("Absolute artifact path {:#?} (from {}) cannot be rebased to base artifact path {:#?}",
                &path,
                path_str,
                &self.artifact_path,
            ))?
        } else {
            path.to_path_buf()
        };
        let absolute_path_buf = self.artifact_path.join(&artifact_relative_path_buf);
        let absolute_path_buf = absolute_path_buf.canonicalize().map_err(|err| {
            anyhow!(
                "Failed to canonicalize computed path: {:#?}: {}",
                absolute_path_buf,
                err.to_string()
            )
        })?;

        if absolute_path_buf.is_relative() {
            return Err(anyhow!("Computed artifact path is relative: computed {:#?} from path {} and artifact base path {:#?}", &absolute_path_buf, path_str, &self.artifact_path));
        }
        if absolute_path_buf.is_dir() {
            return Err(anyhow!("Computed artifact path is directory: computed {:#?} from path {} and artifact base path {:#?}", &absolute_path_buf, path_str, &self.artifact_path));
        }

        let absolute_path_str = absolute_path_buf.to_str();
        if absolute_path_str.is_none() {
            return Err(anyhow!(
                "Computed absolute artifact path {:#?} could not be converted to string",
                absolute_path_buf
            ));
        };
        Ok(absolute_path_str.unwrap().to_string())
    }

    fn dep_from_absolute(&self, path_str: &str) -> Result<String> {
        let path_buf = Path::new(path_str).canonicalize().map_err(|err| {
            anyhow!("Failed to canonicalize absolute path: {}: {}", path_str, err.to_string())
        })?;
        Ok(if path_buf.is_absolute() {
            diff_paths(&path_buf, &self.build_path)
                .ok_or(anyhow!(
                "Artifact path {:#?} (from {}) cannot be formatted relative to build path {:#?}",
                &path_buf,
                path_str,
                &self.build_path,
            ))?
                .to_str()
                .ok_or_else(|| {
                    anyhow!(
                        "Artifact path {:#?} (from {}) cannot be converted to string",
                        path_buf,
                        path_str,
                    )
                })?
                .to_string()
        } else {
            path_str.to_string()
        })
    }
}

impl ArtifactReader for FileArtifactReader {
    fn read_raw(&mut self, path_str: &str) -> Result<Vec<u8>> {
        let absolute_path_string = self
            .absolute_from_absolute_or_artifact_relative(path_str)
            .context("Absolute path conversion failure during read")?;
        let dep_path_string = self
            .dep_from_absolute(&absolute_path_string)
            .context("Dep path conversion failed during read")?;
        self.deps.insert(dep_path_string);
        Ok(fs::read(&absolute_path_string).map_err(|err| {
            anyhow!("Artifact read failed ({}): {}", &absolute_path_string, err.to_string())
        })?)
    }

    fn get_deps(&self) -> HashSet<String> {
        self.deps.clone()
    }
}

#[cfg(test)]
mod tests {
    use {
        super::{ArtifactReader, FileArtifactReader},
        maplit::hashset,
        std::{
            fs::{create_dir, File},
            io::Write,
        },
        tempfile::tempdir,
    };

    #[test]
    fn test_basic() {
        let dir = tempdir().unwrap().into_path();
        let mut loader = FileArtifactReader::new(&dir, &dir);
        let mut file = File::create(dir.join("foo")).unwrap();
        file.write_all(b"test_data").unwrap();
        file.sync_all().unwrap();
        let result = loader.read_raw("foo");
        assert_eq!(result.is_ok(), true);
        let data = result.unwrap();
        assert_eq!(data, b"test_data");
    }

    #[test]
    fn test_deps() {
        let build_path = tempdir().unwrap().into_path();
        let artifact_path_buf = build_path.join("artifacts");
        let artifact_path = artifact_path_buf.as_path();
        create_dir(&artifact_path).unwrap();
        let mut loader = FileArtifactReader::new(&build_path, artifact_path);

        let mut file = File::create(&artifact_path.join("foo")).unwrap();
        file.write_all(b"test_data").unwrap();
        file.sync_all().unwrap();

        let mut file = File::create(&artifact_path.join("bar")).unwrap();
        file.write_all(b"test_data").unwrap();
        file.sync_all().unwrap();

        assert_eq!(loader.read_raw("foo").is_ok(), true);
        assert_eq!(loader.read_raw("bar").is_ok(), true);
        assert_eq!(loader.read_raw("foo").is_ok(), true);
        let deps = loader.get_deps();
        assert_eq!(deps, hashset! {"artifacts/foo".to_string(), "artifacts/bar".to_string()});
    }
}
