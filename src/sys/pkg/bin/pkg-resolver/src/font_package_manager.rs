// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    cobalt_sw_delivery_registry as metrics,
    fuchsia_url::pkg_url::PkgUrl,
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
    package_urls: BTreeSet<PkgUrl>,
    // TODO(fxbug.dev/8881): Add and populate Inspect node.
}

impl FontPackageManager {
    /// Returns true if the given [PkgUrl] is a known font package.
    pub fn is_font_package(&self, package_url: &PkgUrl) -> bool {
        self.package_urls.contains(package_url)
    }
}

/// Builder for [FontPackageManager].
#[derive(Debug)]
pub struct FontPackageManagerBuilder {
    package_urls: BTreeSet<PkgUrl>,
}

impl FontPackageManagerBuilder {
    pub fn new() -> FontPackageManagerBuilder {
        FontPackageManagerBuilder { package_urls: BTreeSet::new() }
    }

    /// Adds a single font [PkgUrl].
    #[cfg(test)]
    pub fn add_package_url(mut self, package_url: PkgUrl) -> Result<Self, (Self, LoadError)> {
        match validate_package_url(None::<PathBuf>, &package_url) {
            Ok(()) => {
                self.package_urls.insert(package_url);
                Ok(self)
            }
            Err(err) => Err((self, err)),
        }
    }

    /// Loads a JSON file containing an array of font package URLs.
    pub fn add_registry_file<P>(
        mut self,
        registry_file_path: P,
    ) -> Result<Self, (Self, Vec<LoadError>)>
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
            Err(errs) => Err((self, errs)),
        }
    }

    /// Builds an immutable [FontPackageManager].
    pub fn build(self) -> FontPackageManager {
        FontPackageManager { package_urls: self.package_urls }
    }
}

/// Loads, parses, and validates a JSON listing of font package URLs from the given file path.
///
/// If the file fails to parse as valid JSON, or any of the listed URLs is invalid, the result will
/// an [Err][std::result::Result::Err] containing a list of all of the parsing and validation
/// errors.
fn load_font_packages_registry<P: AsRef<Path>>(path: P) -> Result<Vec<PkgUrl>, Vec<LoadError>> {
    let file_path = path.as_ref();
    match fs::File::open(&file_path) {
        Ok(f) => match serde_json::from_reader::<_, Vec<PkgUrl>>(io::BufReader::new(f)) {
            Ok(package_urls) => {
                validate_package_urls(file_path, &package_urls).map(|()| package_urls)
            }
            Err(err) => Err(vec![LoadError::Parse { path: file_path.to_path_buf(), error: err }]),
        },
        Err(err) => Err(vec![LoadError::Io { path: file_path.to_path_buf(), error: err }]),
    }
}

/// Validates a single `PkgUrl` to make sure that it could conceivably be a font package URL.
/// This means it must not contain a resource.
fn validate_package_url<P: AsRef<Path>>(
    file_path: Option<P>,
    package_url: &PkgUrl,
) -> Result<(), LoadError> {
    let valid = package_url.resource().is_none();
    if valid {
        Ok(())
    } else {
        Err(LoadError::PkgUrl {
            path: file_path.map(|p| p.as_ref().to_owned()),
            bad_url: package_url.clone(),
        })
    }
}

/// Validates a collection of `PkgUrl`s to make sure that they could be font package URLs. If at
/// least one URL fails validation, the result will be a vector of errors.
fn validate_package_urls<P: AsRef<Path>>(
    file_path: P,
    package_urls: &[PkgUrl],
) -> Result<(), Vec<LoadError>> {
    let path = file_path.as_ref();
    let mut errors = vec![];
    for url in package_urls {
        match validate_package_url(Some(path), &url) {
            Ok(()) => {}
            Err(err) => {
                errors.push(err);
            }
        }
    }
    if errors.is_empty() {
        Ok(())
    } else {
        Err(errors)
    }
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

    #[error(
        "semantic problem with a (syntactically valid) PkgUrl possibly obtained from a file. \
             path: {path:?}, url: {bad_url}"
    )]
    PkgUrl {
        /// The file path from which the URL was read.
        path: Option<PathBuf>,
        /// The problematic `PkgUrl`.
        bad_url: PkgUrl,
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

impl From<&LoadError> for metrics::FontManagerLoadStaticRegistryMetricDimensionResult {
    fn from(error: &LoadError) -> Self {
        match error {
            LoadError::Io { .. } => metrics::FontManagerLoadStaticRegistryMetricDimensionResult::Io,
            LoadError::Parse { .. } => {
                metrics::FontManagerLoadStaticRegistryMetricDimensionResult::Parse
            }
            LoadError::PkgUrl { .. } => {
                metrics::FontManagerLoadStaticRegistryMetricDimensionResult::PkgUrl
            }
        }
    }
}

#[cfg(test)]
pub mod tests {
    use {
        super::*,
        anyhow::Error,
        serde::Serialize,
        serde_json,
        std::fs::File,
        tempfile::{self, TempDir},
    };

    macro_rules! assert_matches(
       ($e:expr, $p:pat => $a:expr) => (
           match $e {
               $p => $a,
               ref e => panic!("`{:?}` does not match `{}`", e, stringify!($p)),
           }
       )
   );

    fn create_json_file<S: Serialize>(
        file_name: &str,
        contents: S,
    ) -> Result<(TempDir, PathBuf), Error> {
        let dir = tempfile::tempdir()?;
        let path = dir.path().join(file_name);
        let f = File::create(&path)?;
        serde_json::to_writer(io::BufWriter::new(f), &contents)?;

        Ok((dir, path))
    }

    #[test]
    fn test_manual_insertion() -> Result<(), Error> {
        let manager = FontPackageManagerBuilder::new()
            .add_package_url(PkgUrl::parse("fuchsia-pkg://fuchsia.com/font1")?)
            .unwrap()
            .add_package_url(PkgUrl::parse("fuchsia-pkg://fuchsia.com/font2")?)
            .unwrap()
            .add_package_url(PkgUrl::parse("fuchsia-pkg://fuchsia.com/font3")?)
            .unwrap()
            .build();
        assert!(manager.is_font_package(&PkgUrl::parse("fuchsia-pkg://fuchsia.com/font2")?));
        assert!(!manager.is_font_package(&PkgUrl::parse("fuchsia-pkg://fuchsia.com/font5")?));
        Ok(())
    }

    #[test]
    fn test_non_package_urls() -> Result<(), Error> {
        let resource_url = PkgUrl::parse("fuchsia-pkg://fuchsia.com/foo#meta/foo.cmx")?;
        assert_matches!(
            FontPackageManagerBuilder::new().add_package_url(resource_url.clone()),
            Err((_, LoadError::PkgUrl{ path: None, bad_url })) =>
             assert_eq!(&bad_url, &resource_url)
        );

        Ok(())
    }

    #[test]
    fn test_load_font_packages_registry() -> Result<(), Error> {
        let file_name = "font_packages.json";
        let (_temp_dir, file_path) = create_json_file(
            file_name,
            vec![
                "fuchsia-pkg://fuchsia.com/font1",
                "fuchsia-pkg://fuchsia.com/font2",
                "fuchsia-pkg://fuchsia.com/font3",
            ],
        )?;

        let manager =
            FontPackageManagerBuilder::new().add_registry_file(&file_path).unwrap().build();
        assert!(manager.is_font_package(&PkgUrl::parse("fuchsia-pkg://fuchsia.com/font1")?));
        assert!(manager.is_font_package(&PkgUrl::parse("fuchsia-pkg://fuchsia.com/font2")?));
        assert!(manager.is_font_package(&PkgUrl::parse("fuchsia-pkg://fuchsia.com/font3")?));
        assert!(!manager.is_font_package(&PkgUrl::parse("fuchsia-pkg://fuchsia.com/font4")?));

        Ok(())
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
            Err((_, errs)) => {
                assert_eq!(errs.len(), 1);
                assert_matches!(
                    &errs[0],
                    LoadError::Io { path, error: _ } => assert_eq!(path, &bad_file_path)
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
            Err((_, errs)) => {
                assert_eq!(errs.len(), 1);
                assert_matches!(
                    &errs[0],
                    LoadError::Parse { path, error: _ } => assert_eq!(path, &file_path)
                );
            }
        );

        Ok(())
    }

    #[test]
    fn test_load_font_packages_registry_invalid_font_packages() -> Result<(), Error> {
        let file_name = "font_packages.json";
        let (_temp_dir, file_path) = create_json_file(
            file_name,
            vec![
                "fuchsia-pkg://fuchsia.com/font1",
                "fuchsia-pkg://fuchsia.com/font2#meta/font2.cmx",
            ],
        )?;

        let builder = FontPackageManagerBuilder::new();
        let result = builder.add_registry_file(&file_path);

        assert_matches!(
            result,
            Err((_, errs)) => assert_eq!(errs.len(), 1)
        );

        Ok(())
    }
}
