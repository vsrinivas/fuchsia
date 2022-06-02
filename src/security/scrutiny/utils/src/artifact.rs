// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::blobfs::BlobFsReader,
    anyhow::{anyhow, Context, Result},
    log::warn,
    pathdiff::diff_paths,
    std::{
        collections::{HashMap, HashSet},
        fs,
        io::Read,
        path::{Path, PathBuf},
        sync::Arc,
    },
};

/// Interface for fetching raw bytes by file path.
pub trait ArtifactReader: Send + Sync {
    /// Read the raw bytes stored in filesystem location `path`.
    fn read_bytes(&mut self, path: &Path) -> Result<Vec<u8>>;

    /// Get the accumulated set of filesystem locations that have been read by
    /// this reader.
    fn get_deps(&self) -> HashSet<PathBuf>;
}

#[derive(Clone)]
pub struct BlobFsArtifactReader {
    blobfs_dep_path: PathBuf,
    blobs: Arc<HashMap<PathBuf, Vec<u8>>>,
}

impl BlobFsArtifactReader {
    pub fn try_new<P1: AsRef<Path>, P2: AsRef<Path>>(
        build_path: P1,
        blobfs_path: P2,
    ) -> Result<Self> {
        let build_path_ref = build_path.as_ref();
        let blobfs_path_ref = blobfs_path.as_ref();
        let build_path = match build_path_ref.canonicalize() {
            Ok(path) => path,
            Err(err) => {
                warn!(
                    "Blobfs artifact reader failed to canonicalize build path: {:?}: {}",
                    build_path_ref,
                    err.to_string()
                );
                build_path_ref.to_path_buf()
            }
        };
        let blobfs_path = match blobfs_path_ref.canonicalize() {
            Ok(path) => path,
            Err(err) => {
                warn!(
                    "File artifact reader failed to canonicalize blobfs archive path: {:?}: {}",
                    blobfs_path_ref,
                    err.to_string()
                );
                blobfs_path_ref.to_path_buf()
            }
        };

        if !blobfs_path.is_absolute() {
            return Err(anyhow!("Blobfs archive path {:?} is not absolute", blobfs_path));
        }
        let blobfs_path_str = blobfs_path.to_str().ok_or_else(|| {
            anyhow!("Blobfs archive path {:?} could not be converted to string", blobfs_path)
        })?;
        let blobfs_dep_path =
            dep_from_absolute(&build_path, blobfs_path_str).with_context(|| {
                format!(
                    "Blobfs archive path {:?} could not be made relative to build path {:?}",
                    blobfs_path, build_path
                )
            })?;

        let mut blobfs_file = fs::File::open(blobfs_path)
            .context("Failed to open blobfs archive for artifact reader")?;
        let mut blobfs_buffer = Vec::new();
        blobfs_file
            .read_to_end(&mut blobfs_buffer)
            .context("Failed to read blobfs archive for artifact reader")?;
        let mut blobfs_reader = BlobFsReader::new(blobfs_buffer);
        let blobs =
            blobfs_reader.parse().context("Failed to parse blobfs archive for artifact reader")?;
        Ok(Self {
            blobfs_dep_path,
            blobs: Arc::new(
                blobs.into_iter().map(|blob| (blob.merkle.into(), blob.buffer)).collect(),
            ),
        })
    }

    pub fn try_compound<P1: AsRef<Path>, P2: AsRef<Path>>(
        build_path: P1,
        blobfs_paths: &Vec<P2>,
    ) -> Result<CompoundArtifactReader> {
        Ok(CompoundArtifactReader::new(
            blobfs_paths
                .into_iter()
                .map(|blobfs_path| {
                    let reader = Self::try_new(&build_path, blobfs_path)?;
                    let boxed: Box<dyn ArtifactReader> = Box::new(reader);
                    Ok(boxed)
                })
                .collect::<Result<Vec<Box<dyn ArtifactReader>>>>()?,
        ))
    }
}

impl ArtifactReader for BlobFsArtifactReader {
    fn read_bytes(&mut self, path: &Path) -> Result<Vec<u8>> {
        if let Some(blob_contents) = self.blobs.get(path) {
            Ok(blob_contents.clone())
        } else {
            Err(anyhow!("Blob {:?} not found in blobfs archive {:?}", path, self.blobfs_dep_path))
        }
    }

    fn get_deps(&self) -> HashSet<PathBuf> {
        [self.blobfs_dep_path.clone()].into()
    }
}

pub struct CompoundArtifactReader {
    delegates: Vec<Box<dyn ArtifactReader>>,
}

impl CompoundArtifactReader {
    pub fn new(delegates: Vec<Box<dyn ArtifactReader>>) -> Self {
        Self { delegates }
    }
}

impl ArtifactReader for CompoundArtifactReader {
    fn read_bytes(&mut self, path: &Path) -> Result<Vec<u8>> {
        let mut errs = vec![];
        for delegate in self.delegates.iter_mut() {
            match delegate.read_bytes(path) {
                Ok(data) => {
                    return Ok(data);
                }
                Err(err) => {
                    errs.push(err);
                }
            }
        }
        let mut compound_err = anyhow!("Compound artifact read failed");
        for err in errs.into_iter() {
            compound_err = compound_err.context("Read failure");
            for ctx in err.chain() {
                compound_err = compound_err.context(ctx.to_string());
            }
        }
        Err(compound_err)
    }

    fn get_deps(&self) -> HashSet<PathBuf> {
        let mut deps = HashSet::new();
        for delegate in self.delegates.iter() {
            deps.extend(delegate.get_deps().into_iter());
        }
        deps
    }
}

impl From<Vec<BlobFsArtifactReader>> for CompoundArtifactReader {
    fn from(readers: Vec<BlobFsArtifactReader>) -> Self {
        Self::new(
            readers
                .iter()
                .map(|reader| Box::new(reader.clone()) as Box<dyn ArtifactReader>)
                .collect(),
        )
    }
}

#[derive(Clone)]
pub struct FileArtifactReader {
    build_path: PathBuf,
    artifact_path: PathBuf,
    deps: HashSet<PathBuf>,
}

/// The FileArtifactLoader retrieves package data and blobs directly from the
/// build artifacts on disk.
impl FileArtifactReader {
    pub fn new(build_path: &Path, artifact_path: &Path) -> Self {
        let build_path = match build_path.canonicalize() {
            Ok(path) => path,
            Err(err) => {
                warn!(
                    "File artifact reader failed to canonicalize build path: {:?}: {}",
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
                    "File artifact reader failed to canonicalize artifact path: {:?}: {}",
                    artifact_path,
                    err.to_string()
                );
                artifact_path.to_path_buf()
            }
        };
        Self { build_path, artifact_path, deps: HashSet::new() }
    }
}

impl ArtifactReader for FileArtifactReader {
    fn read_bytes(&mut self, path: &Path) -> Result<Vec<u8>> {
        let absolute_path_string =
            absolute_from_absolute_or_artifact_relative(&self.artifact_path, path)
                .context("Absolute path conversion failure during read")?;
        let dep_path_string = dep_from_absolute(&self.build_path, &absolute_path_string)
            .context("Dep path conversion failed during read")?;
        self.deps.insert(dep_path_string);
        Ok(fs::read(&absolute_path_string).map_err(|err| {
            anyhow!("Artifact read failed ({}): {}", &absolute_path_string, err.to_string())
        })?)
    }

    fn get_deps(&self) -> HashSet<PathBuf> {
        self.deps.clone()
    }
}

fn absolute_from_absolute_or_artifact_relative<P1: AsRef<Path>, P2: AsRef<Path>>(
    artifact_path: P1,
    path: P2,
) -> Result<String> {
    let artifact_path_ref = artifact_path.as_ref();
    let path_ref = path.as_ref();
    let artifact_relative_path_buf = if path_ref.is_absolute() {
        diff_paths(path_ref, &artifact_path).ok_or_else(|| {
            anyhow!(
                "Absolute artifact path {:?} cannot be rebased to base artifact path {:?}",
                path_ref,
                artifact_path_ref,
            )
        })?
    } else {
        path_ref.to_path_buf()
    };
    let absolute_path_buf = artifact_path_ref.join(&artifact_relative_path_buf);
    let absolute_path_buf = absolute_path_buf.canonicalize().map_err(|err| {
        anyhow!(
            "Failed to canonicalize computed path: {:?}: {}",
            absolute_path_buf,
            err.to_string()
        )
    })?;

    if absolute_path_buf.is_relative() {
        return Err(anyhow!(
            "Computed artifact path is relative: computed {:?} from path {:?} and artifact base path {:?}",
            absolute_path_buf,
            path_ref,
            artifact_path_ref,
        ));
    }
    if absolute_path_buf.is_dir() {
        return Err(anyhow!(
            "Computed artifact path is directory: computed {:?} from path {:?} and artifact base path {:?}",
            absolute_path_buf,
            path_ref,
            artifact_path_ref,
        ));
    }

    let absolute_path_str = absolute_path_buf.to_str();
    if absolute_path_str.is_none() {
        return Err(anyhow!(
            "Computed absolute artifact path {:?} could not be converted to string",
            absolute_path_buf
        ));
    };
    Ok(absolute_path_str.unwrap().to_string())
}

fn dep_from_absolute<P1: AsRef<Path>, P2: AsRef<Path>>(
    build_path: P1,
    path: P2,
) -> Result<PathBuf> {
    let build_path_ref = build_path.as_ref();
    let path_ref = path.as_ref();
    let canonical_path_buf = path_ref.canonicalize().map_err(|err| {
        anyhow!("Failed to canonicalize absolute path: {:?}: {:?}", path_ref, err.to_string())
    })?;
    if canonical_path_buf.is_absolute() {
        diff_paths(&canonical_path_buf, &build_path).ok_or_else(|| {
            anyhow!(
                "Artifact path {:?} (from {:?}) cannot be formatted relative to build path {:?}",
                canonical_path_buf,
                path_ref,
                build_path_ref,
            )
        })
    } else {
        Err(anyhow!(
            "Canonicalized form of {:?} is {:?}, which is not an absolute path",
            path_ref,
            canonical_path_buf,
        ))
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
            path::Path,
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
        let result = loader.read_bytes(&Path::new("foo"));
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

        assert_eq!(loader.read_bytes(&Path::new("foo")).is_ok(), true);
        assert_eq!(loader.read_bytes(&Path::new("bar")).is_ok(), true);
        assert_eq!(loader.read_bytes(&Path::new("foo")).is_ok(), true);
        let deps = loader.get_deps();
        assert_eq!(
            deps,
            hashset! {"artifacts/foo".to_string().into(), "artifacts/bar".to_string().into()}
        );
    }
}
