// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    cobalt_sw_delivery_registry as metrics,
    fuchsia_url::UnpinnedAbsolutePackageUrl,
    std::{
        collections::BTreeSet,
        fs, io,
        path::{Path, PathBuf},
    },
    thiserror::Error,
};

/// Tracks set of packages that contain single font files. Construct using
/// [FontPackageManagerBuilder].
#[derive(Debug)]
pub struct FontPackageManager {
    // A `BTreeSet` is used to preserve alphabetical order for easier debugging.
    package_urls: BTreeSet<UnpinnedAbsolutePackageUrl>,
    // TODO(fxbug.dev/8881): Add and populate Inspect node.
}

impl FontPackageManager {
    /// Returns true if the given [UnpinnedAbsolutePackageUrl] is a known font package.
    pub fn is_font_package(&self, package_url: &UnpinnedAbsolutePackageUrl) -> bool {
        self.package_urls.contains(package_url)
    }
}

/// Builder for [FontPackageManager].
#[derive(Debug)]
pub struct FontPackageManagerBuilder {
    package_urls: BTreeSet<UnpinnedAbsolutePackageUrl>,
}

impl FontPackageManagerBuilder {
    pub fn new() -> FontPackageManagerBuilder {
        FontPackageManagerBuilder { package_urls: BTreeSet::new() }
    }

    /// Adds a single font [UnpinnedAbsolutePackageUrl].
    #[cfg(test)]
    pub fn add_package_url(mut self, package_url: UnpinnedAbsolutePackageUrl) -> Self {
        self.package_urls.insert(package_url);
        self
    }

    /// Loads a JSON file containing an array of font package URLs.
    pub fn add_registry_file<P>(mut self, registry_file_path: P) -> Result<Self, (Self, LoadError)>
    where
        P: AsRef<Path>,
    {
        match load_font_packages_registry(registry_file_path) {
            Ok(urls) => {
                for url in urls {
                    self.package_urls.insert(url);
                }
                Ok(self)
            }
            Err(err) => Err((self, err)),
        }
    }

    /// Builds an immutable [FontPackageManager].
    pub fn build(self) -> FontPackageManager {
        FontPackageManager { package_urls: self.package_urls }
    }
}

/// Loads, parses, and validates a JSON listing of font package URLs from the given file path.
///
/// If the file fails to parse as valid JSON or any of the listed URLs is invalid the result will
/// be an [Err][std::result::Result::Err] indicating the parsing or validation error.
fn load_font_packages_registry<P: AsRef<Path>>(
    path: P,
) -> Result<Vec<UnpinnedAbsolutePackageUrl>, LoadError> {
    let path = path.as_ref();

    let f =
        fs::File::open(&path).map_err(|e| LoadError::Io { path: path.to_path_buf(), error: e })?;

    serde_json::from_reader::<_, Vec<UnpinnedAbsolutePackageUrl>>(io::BufReader::new(f))
        .map_err(|e| LoadError::Parse { path: path.to_path_buf(), error: e })
}

/// Describes the recoverable error conditions that can be encountered when building a
/// [FontPackageManager].
#[derive(Debug, Error)]
pub enum LoadError {
    #[error("error reading a font package registry file: {path}")]
    Io {
        /// The problematic file path.
        path: PathBuf,
        #[source]
        error: io::Error,
    },

    #[error("error parsing the JSON contents of a font package registry file: {path}")]
    Parse {
        /// The problematic file path.
        path: PathBuf,
        #[source]
        error: serde_json::Error,
    },
}

impl LoadError {
    /// Returns true iff we're an IO not found error
    pub fn is_not_found(&self) -> bool {
        match self {
            LoadError::Io { path: _, error } => error.kind() == io::ErrorKind::NotFound,
            _ => false,
        }
    }
}

impl From<&LoadError> for metrics::FontManagerLoadStaticRegistryMigratedMetricDimensionResult {
    fn from(error: &LoadError) -> Self {
        match error {
            LoadError::Io { .. } => {
                metrics::FontManagerLoadStaticRegistryMigratedMetricDimensionResult::Io
            }
            LoadError::Parse { .. } => {
                metrics::FontManagerLoadStaticRegistryMigratedMetricDimensionResult::Parse
            }
        }
    }
}

#[cfg(test)]
pub mod tests {
    use {
        super::*,
        anyhow::Error,
        assert_matches::assert_matches,
        serde::Serialize,
        serde_json,
        std::{fs::File, io::Write},
        tempfile::{self, TempDir},
    };

    fn create_json_file<S: Serialize>(
        file_name: &str,
        contents: S,
    ) -> Result<(TempDir, PathBuf), Error> {
        let dir = tempfile::tempdir()?;
        let path = dir.path().join(file_name);
        let mut f = io::BufWriter::new(File::create(&path)?);
        serde_json::to_writer(&mut f, &contents)?;
        f.flush().unwrap();

        Ok((dir, path))
    }

    #[test]
    fn test_manual_insertion() {
        let manager = FontPackageManagerBuilder::new()
            .add_package_url("fuchsia-pkg://fuchsia.com/font1".parse().unwrap())
            .add_package_url("fuchsia-pkg://fuchsia.com/font2".parse().unwrap())
            .add_package_url("fuchsia-pkg://fuchsia.com/font3".parse().unwrap())
            .build();
        assert!(manager.is_font_package(&"fuchsia-pkg://fuchsia.com/font2".parse().unwrap()));
        assert!(!manager.is_font_package(&"fuchsia-pkg://fuchsia.com/font5".parse().unwrap()));
    }

    #[test]
    fn test_load_font_packages_registry() {
        let file_name = "font_packages.json";
        let (_temp_dir, file_path) = create_json_file(
            file_name,
            vec![
                "fuchsia-pkg://fuchsia.com/font1",
                "fuchsia-pkg://fuchsia.com/font2",
                "fuchsia-pkg://fuchsia.com/font3",
            ],
        )
        .unwrap();

        let manager =
            FontPackageManagerBuilder::new().add_registry_file(&file_path).unwrap().build();
        assert!(manager.is_font_package(
            &UnpinnedAbsolutePackageUrl::parse("fuchsia-pkg://fuchsia.com/font1").unwrap()
        ));
        assert!(manager.is_font_package(
            &UnpinnedAbsolutePackageUrl::parse("fuchsia-pkg://fuchsia.com/font2").unwrap()
        ));
        assert!(manager.is_font_package(
            &UnpinnedAbsolutePackageUrl::parse("fuchsia-pkg://fuchsia.com/font3").unwrap()
        ));
        assert!(!manager.is_font_package(
            &UnpinnedAbsolutePackageUrl::parse("fuchsia-pkg://fuchsia.com/font4").unwrap()
        ));
    }

    #[test]
    fn test_load_font_packages_registry_missing_file() -> Result<(), Error> {
        let file_name = "404.json";
        let temp_dir = tempfile::tempdir()?;
        let bad_file_path = temp_dir.path().join(file_name);

        let builder = FontPackageManagerBuilder::new();
        let result = builder.add_registry_file(&bad_file_path);

        assert_matches!(
            result,
            Err((_, err)) => {
                assert_matches!(
                    err,
                    LoadError::Io { path, error: _ } => assert_eq!(path, bad_file_path)
                );
            }
        );

        Ok(())
    }

    #[test]
    fn test_load_font_packages_registry_bad_structure() -> Result<(), Error> {
        let file_name = "font_packages.json";
        let (_temp_dir, file_path) = create_json_file(file_name, vec![1, 2, 3])?;

        let builder = FontPackageManagerBuilder::new();
        let result = builder.add_registry_file(&file_path);

        assert_matches!(
            result,
            Err((_, err)) => {
                assert_matches!(
                    err,
                    LoadError::Parse { path, error: _ } => assert_eq!(path, file_path)
                );
            }
        );

        Ok(())
    }

    #[test]
    fn test_load_font_packages_registry_invalid_font_packages() {
        let file_name = "font_packages.json";
        let (_temp_dir, file_path) = create_json_file(
            file_name,
            vec![
                "fuchsia-pkg://fuchsia.com/font1",
                "fuchsia-pkg://fuchsia.com/font2#meta/font2.cml",
            ],
        )
        .unwrap();

        let builder = FontPackageManagerBuilder::new();
        let result = builder.add_registry_file(&file_path);

        assert_matches!(result, Err(_));
    }
}
