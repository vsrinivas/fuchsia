// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::common::done_time,
    anyhow::{anyhow, bail, Context, Result},
    async_trait::async_trait,
    chrono::Utc,
    errors::{ffx_bail, ffx_error},
    flate2::read::GzDecoder,
    std::fs::{create_dir_all, File},
    std::io::{copy, Write},
    std::path::{Path, PathBuf},
    tar::Archive,
    tempfile::{tempdir, TempDir},
    walkdir::WalkDir,
    zip::read::ZipArchive,
};

#[async_trait(?Send)]
pub trait FileResolver {
    fn manifest(&self) -> &Path;
    async fn get_file<W: Write>(&mut self, writer: &mut W, file: &str) -> Result<String>;
}

pub struct EmptyResolver {
    fake: PathBuf,
}

impl EmptyResolver {
    pub fn new() -> Result<Self> {
        let mut fake = std::env::current_dir()?;
        fake.push("fake");
        Ok(Self { fake })
    }
}

#[async_trait(?Send)]
impl FileResolver for EmptyResolver {
    fn manifest(&self) -> &Path {
        self.fake.as_path() //should never get used
    }

    async fn get_file<W: Write>(&mut self, _writer: &mut W, file: &str) -> Result<String> {
        if PathBuf::from(file).is_absolute() {
            Ok(file.to_string())
        } else {
            let mut parent = std::env::current_dir()?;
            parent.push(file);
            if let Some(f) = parent.to_str() {
                Ok(f.to_string())
            } else {
                ffx_bail!("Only UTF-8 strings are currently supported in the flash manifest")
            }
        }
    }
}

pub struct Resolver {
    manifest_path: PathBuf,
}

impl Resolver {
    pub fn new(path: PathBuf) -> Result<Self> {
        Ok(Self {
            manifest_path: path.canonicalize().with_context(|| {
                format!("Getting absolute path of flashing manifest at {:?}", path)
            })?,
        })
    }
}

#[async_trait(?Send)]
impl FileResolver for Resolver {
    fn manifest(&self) -> &Path {
        self.manifest_path.as_path()
    }

    async fn get_file<W: Write>(&mut self, _writer: &mut W, file: &str) -> Result<String> {
        if PathBuf::from(file).is_absolute() {
            Ok(file.to_string())
        } else if let Some(p) = self.manifest().parent() {
            let mut parent = p.to_path_buf();
            parent.push(file);
            if let Some(f) = parent.to_str() {
                Ok(f.to_string())
            } else {
                ffx_bail!("Only UTF-8 strings are currently supported in the flash manifest")
            }
        } else {
            bail!("Could not get file to upload");
        }
    }
}

pub struct ArchiveResolver {
    temp_dir: TempDir,
    manifest_path: PathBuf,
    internal_manifest_path: PathBuf,
    archive: ZipArchive<File>,
}

impl ArchiveResolver {
    pub fn new<W: Write>(writer: &mut W, path: PathBuf) -> Result<Self> {
        let temp_dir = tempdir()?;
        let file = File::open(path.clone())
            .map_err(|e| ffx_error!("Could not open archive file: {}", e))?;
        let mut archive =
            ZipArchive::new(file).map_err(|e| ffx_error!("Could not read archive: {}", e))?;
        let mut internal_manifest_path = None;
        let mut manifest_path = None;

        for i in 0..archive.len() {
            let mut archive_file = archive.by_index(i)?;
            let outpath = archive_file.sanitized_name();
            if (&*archive_file.name()).ends_with("flash.json")
                || (&*archive_file.name()).ends_with("flash-manifest.manifest")
            {
                let mut internal_path = PathBuf::new();
                internal_path.push(outpath.clone());
                internal_manifest_path.replace(internal_path.clone());
                let mut manifest = PathBuf::new();
                manifest.push(temp_dir.path());
                manifest.push(outpath);
                if let Some(p) = manifest.parent() {
                    if !p.exists() {
                        create_dir_all(&p)?;
                    }
                }
                let mut outfile = File::create(&manifest)?;
                let time = Utc::now();
                write!(
                    writer,
                    "Extracting {} to {}... ",
                    internal_path.file_name().expect("has a file name").to_string_lossy(),
                    temp_dir.path().display()
                )?;
                writer.flush()?;
                copy(&mut archive_file, &mut outfile)?;
                let duration = Utc::now().signed_duration_since(time);
                done_time(writer, duration)?;
                manifest_path.replace(manifest);
                break;
            }
        }

        match (internal_manifest_path, manifest_path) {
            (Some(i), Some(m)) => {
                Ok(Self { temp_dir, manifest_path: m, internal_manifest_path: i, archive })
            }
            _ => ffx_bail!("Could not locate flash manifest in archive: {}", path.display()),
        }
    }
}

#[async_trait(?Send)]
impl FileResolver for ArchiveResolver {
    fn manifest(&self) -> &Path {
        self.manifest_path.as_path()
    }

    async fn get_file<W: Write>(&mut self, writer: &mut W, file: &str) -> Result<String> {
        let mut file = match self.internal_manifest_path.parent() {
            Some(p) => {
                let mut path = PathBuf::new();
                path.push(p);
                path.push(file);
                self.archive
                    .by_name(path.to_str().ok_or(anyhow!("invalid archive file name"))?)
                    .map_err(|_| anyhow!("File not found in archive: {}", file))?
            }
            None => self
                .archive
                .by_name(file)
                .map_err(|_| anyhow!("File not found in archive: {}", file))?,
        };
        let mut outpath = PathBuf::new();
        outpath.push(self.temp_dir.path());
        outpath.push(file.sanitized_name());
        if let Some(p) = outpath.parent() {
            if !p.exists() {
                create_dir_all(&p)?;
            }
        }
        let time = Utc::now();
        write!(
            writer,
            "Extracting {} to {}... ",
            file.sanitized_name().file_name().expect("has a file name").to_string_lossy(),
            self.temp_dir.path().display()
        )?;
        if file.size() > (1 << 24) {
            write!(writer, "large file, please wait... ")?;
        }
        writer.flush()?;
        let mut outfile = File::create(&outpath)?;
        copy(&mut file, &mut outfile)?;
        let duration = Utc::now().signed_duration_since(time);
        done_time(writer, duration)?;
        Ok(outpath.to_str().ok_or(anyhow!("invalid temp file name"))?.to_owned())
    }
}

pub struct TarResolver {
    _temp_dir: TempDir,
    manifest_path: PathBuf,
}

impl TarResolver {
    pub fn new<W: Write>(writer: &mut W, path: PathBuf) -> Result<Self> {
        let temp_dir = tempdir()?;
        let file = File::open(path.clone())
            .map_err(|e| ffx_error!("Could not open archive file: {}", e))?;
        let time = Utc::now();
        write!(writer, "Extracting {} to {}... ", path.display(), temp_dir.path().display())?;
        writer.flush()?;
        // Tarballs can't do per file extraction well like Zip, so just unpack it all.
        match path.extension() {
            Some(ext) if ext == "tar.gz" || ext == "tgz" => {
                let mut archive = Archive::new(GzDecoder::new(file));
                archive.unpack(temp_dir.path())?;
            }
            Some(ext) if ext == "tar" => {
                let mut archive = Archive::new(file);
                archive.unpack(temp_dir.path())?;
            }
            _ => ffx_bail!("Invalid tar archive"),
        }
        let duration = Utc::now().signed_duration_since(time);
        done_time(writer, duration)?;

        let manifest_path = WalkDir::new(temp_dir.path())
            .into_iter()
            .filter_map(|e| e.ok())
            .find(|e| e.file_name() == "flash.json" || e.file_name() == "flash-manifest.manifest");

        match manifest_path {
            Some(m) => Ok(Self { _temp_dir: temp_dir, manifest_path: m.into_path() }),
            _ => ffx_bail!("Could not locate flash manifest in archive: {}", path.display()),
        }
    }
}

#[async_trait(?Send)]
impl FileResolver for TarResolver {
    fn manifest(&self) -> &Path {
        self.manifest_path.as_path()
    }

    async fn get_file<W: Write>(&mut self, _writer: &mut W, file: &str) -> Result<String> {
        if let Some(p) = self.manifest().parent() {
            let mut parent = p.to_path_buf();
            parent.push(file);
            if let Some(f) = parent.to_str() {
                Ok(f.to_string())
            } else {
                ffx_bail!("Only UTF-8 strings are currently supported in the flash manifest")
            }
        } else {
            // Should not get here, the parent of the manifest file SHOULD AT LEAST be the temp dir.
            bail!("Could not get file to upload");
        }
    }
}
