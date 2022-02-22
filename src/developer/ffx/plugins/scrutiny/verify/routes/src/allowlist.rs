// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{bail, Context, Result},
    scrutiny_plugins::verify::{ResultsBySeverity, ResultsForCapabilityType},
    serde::{de::DeserializeOwned, Deserialize, Serialize},
    serde_json5::from_reader,
    std::io::Read,
};

/// Data format for allowlists before versioning was introduced.
pub type UnversionedAllowlistContents = Vec<ResultsForCapabilityType>;

/// Data format used by the verify results scrutiny plugin to report results.
pub type AnalysisResult = Vec<ResultsForCapabilityType>;

/// Data format produced by filtering out allowed results from an
/// `AnalysisResult` based on the contents of an allowlist.
pub type AllowlistResult = Vec<ResultsForCapabilityType>;

/// Builder for constructing allowlists from fragments stored in separate files.
pub trait AllowlistBuilder {
    /// Load allowlist from fragment in the given file.
    fn load(&mut self, reader: Box<dyn Read>) -> Result<()>;

    /// Build an allowlist filter from loaded fragments.
    fn build(&self) -> Box<dyn AllowlistFilter>;
}

/// Filter for applying allowlists to raw results from the verify routes
/// scrutiny plugin.
pub trait AllowlistFilter {
    /// Filter an analysis result according to the allowlist.
    fn filter_analysis(&self, route_analysis: AnalysisResult) -> AllowlistResult;
}

/// Allowlist that contains no version information; default for allowlists that
/// have not been migrated to versioned format.
#[derive(Clone)]
pub struct UnversionedAllowlist(VersionedAllowlist<V0ExtendableFormat>);

impl UnversionedAllowlist {
    pub fn new() -> Box<dyn AllowlistBuilder> {
        let versioned: VersionedExtendableFormat<V0ExtendableFormat> =
            V0ExtendableFormat::default().into();
        let allowlist: VersionedAllowlist<V0ExtendableFormat> = versioned.into();
        let builder: Self = allowlist.into();
        Box::new(builder)
    }
}

impl From<VersionedAllowlist<V0ExtendableFormat>> for UnversionedAllowlist {
    fn from(versioned_allowlist: VersionedAllowlist<V0ExtendableFormat>) -> Self {
        Self(versioned_allowlist)
    }
}

impl AllowlistBuilder for UnversionedAllowlist {
    fn load(&mut self, mut reader: Box<dyn Read>) -> Result<()> {
        // Append reader contents directly to `...allowlist`.
        let existing = &mut self.0 .0.allowlist;
        existing
            .append(from_reader(&mut reader).context("Failed to deserialize allowlist")?)
            .context("Failed to append to allowlist")?;
        Ok(())
    }

    fn build(&self) -> Box<dyn AllowlistFilter> {
        Box::new(self.clone())
    }
}

impl AllowlistFilter for UnversionedAllowlist {
    fn filter_analysis(&self, route_analysis: AnalysisResult) -> AllowlistResult {
        self.0.filter_analysis(route_analysis)
    }
}

/// Versioned form of initial allowlist format.
#[derive(Clone)]
pub struct V0Allowlist(VersionedAllowlist<V0ExtendableFormat>);

impl V0Allowlist {
    pub fn new() -> Box<dyn AllowlistBuilder> {
        let versioned: VersionedExtendableFormat<V0ExtendableFormat> =
            V0ExtendableFormat::default().into();
        let allowlist: VersionedAllowlist<V0ExtendableFormat> = versioned.into();
        let builder: Self = allowlist.into();
        Box::new(builder)
    }
}

impl From<VersionedAllowlist<V0ExtendableFormat>> for V0Allowlist {
    fn from(versioned_allowlist: VersionedAllowlist<V0ExtendableFormat>) -> Self {
        Self(versioned_allowlist)
    }
}

impl AllowlistBuilder for V0Allowlist {
    fn load(&mut self, reader: Box<dyn Read>) -> Result<()> {
        self.0.load(reader)
    }

    fn build(&self) -> Box<dyn AllowlistFilter> {
        Box::new(self.clone())
    }
}

impl AllowlistFilter for V0Allowlist {
    fn filter_analysis(&self, route_analysis: AnalysisResult) -> AllowlistResult {
        self.0.filter_analysis(route_analysis)
    }
}

/// Allowlist version 1: First version intened to provide a stable interface for
/// specifying allowlists that interact with analyzer internal details.
#[derive(Clone)]
pub struct V1Allowlist(VersionedAllowlist<V1ExtendableFormat>);

impl V1Allowlist {
    pub fn new() -> Box<dyn AllowlistBuilder> {
        let versioned: VersionedExtendableFormat<V1ExtendableFormat> =
            V1ExtendableFormat::default().into();
        let allowlist: VersionedAllowlist<V1ExtendableFormat> = versioned.into();
        let builder: Self = allowlist.into();
        Box::new(builder)
    }
}

impl From<VersionedAllowlist<V1ExtendableFormat>> for V1Allowlist {
    fn from(versioned_allowlist: VersionedAllowlist<V1ExtendableFormat>) -> Self {
        Self(versioned_allowlist)
    }
}

impl AllowlistBuilder for V1Allowlist {
    fn load(&mut self, reader: Box<dyn Read>) -> Result<()> {
        self.0.load(reader)
    }

    fn build(&self) -> Box<dyn AllowlistFilter> {
        Box::new(self.clone())
    }
}

impl AllowlistFilter for V1Allowlist {
    fn filter_analysis(&self, route_analysis: AnalysisResult) -> AllowlistResult {
        self.0.filter_analysis(route_analysis)
    }
}

// Trait for implementation types that map to a specific allowlist format
// version.
trait Versioned {
    const VERSION: u64;
}

// Trait for allowlist formats that can be appended by applying additional
// allowlist fragments.
trait ExtendableFormat: DeserializeOwned + Serialize {
    fn append(&mut self, other: Self) -> Result<()>;
}

// Abstract allowlist format that specifies its version.
#[derive(Deserialize, Serialize, Clone)]
struct VersionedExtendableFormat<EF> {
    version: u64,
    allowlist: EF,
}

impl<EF> From<EF> for VersionedExtendableFormat<EF>
where
    EF: Versioned + ExtendableFormat,
{
    fn from(allowlist: EF) -> Self {
        Self { version: EF::VERSION, allowlist }
    }
}

impl<EF> ExtendableFormat for VersionedExtendableFormat<EF>
where
    EF: Default + ExtendableFormat + Into<VersionedExtendableFormat<EF>> + Versioned,
{
    fn append(&mut self, other: Self) -> Result<()> {
        if other.version != EF::VERSION {
            bail!("Expected format version {} but got {}", EF::VERSION, other.version);
        }
        self.allowlist.append(other.allowlist)
    }
}

// Abstract trait object for wrapping versioned allowlist formats.
#[derive(Clone)]
struct VersionedAllowlist<EF>(VersionedExtendableFormat<EF>);

impl<EF> From<VersionedExtendableFormat<EF>> for VersionedAllowlist<EF>
where
    EF: Default + ExtendableFormat + Into<VersionedExtendableFormat<EF>>,
{
    fn from(versioned_extended_format: VersionedExtendableFormat<EF>) -> Self {
        Self(versioned_extended_format)
    }
}

impl<EF> AllowlistBuilder for VersionedAllowlist<EF>
where
    EF: 'static
        + Clone
        + Default
        + ExtendableFormat
        + Into<VersionedExtendableFormat<EF>>
        + Versioned,
    VersionedAllowlist<EF>: AllowlistFilter,
{
    fn load(&mut self, mut reader: Box<dyn Read>) -> Result<()> {
        let existing = &mut self.0;
        existing
            .append(from_reader(&mut reader).context("Failed to deserialize allowlist")?)
            .context("Failed to append to allowlist")?;
        Ok(())
    }

    fn build(&self) -> Box<dyn AllowlistFilter> {
        Box::new(self.clone())
    }
}

//
// V0 format and filter
//

#[derive(Clone, Deserialize, Serialize)]
struct V0ExtendableFormat(UnversionedAllowlistContents);

impl Versioned for V0ExtendableFormat {
    const VERSION: u64 = 0;
}

impl Default for V0ExtendableFormat {
    fn default() -> Self {
        Self(vec![])
    }
}

impl ExtendableFormat for V0ExtendableFormat {
    fn append(&mut self, other: Self) -> Result<()> {
        for mut fragment_type_group in other.0.into_iter() {
            let mut merged = false;
            for type_group in self.0.iter_mut() {
                if type_group.capability_type == fragment_type_group.capability_type {
                    merged = true;
                    type_group.results.errors.append(&mut fragment_type_group.results.errors);
                    type_group.results.warnings.append(&mut fragment_type_group.results.warnings);
                    type_group.results.ok.append(&mut fragment_type_group.results.ok);
                }
            }
            if !merged {
                // We didn't find another set for this capability type. Just append this set to the
                // main list.
                self.0.push(fragment_type_group);
            }
        }

        Ok(())
    }
}

impl AllowlistFilter for VersionedAllowlist<V0ExtendableFormat> {
    fn filter_analysis(&self, route_analysis: AnalysisResult) -> AllowlistResult {
        let allowlist = &self.0.allowlist.0;
        route_analysis
            .iter()
            .map(|analysis_item| {
                // Entry for every `capability_type` in `route_analysis`.
                ResultsForCapabilityType {
                    capability_type: analysis_item.capability_type.clone(),
                    // Retain error when:
                    // 1. `allowlist` does not have results for
                    //    `capability_type` (i.e., nothing allowed for
                    //    `capability_type`), OR
                    // 2. `allowlist` does not have an identical `allow_error`
                    //    in its `capability_type` results.
                    results: ResultsBySeverity {
                        errors: analysis_item
                            .results
                            .errors
                            .iter()
                            .filter_map(|analysis_error| {
                                match allowlist.iter().find(|&allow_item| {
                                    allow_item.capability_type == analysis_item.capability_type
                                }) {
                                    Some(allow_item) => {
                                        match allow_item
                                            .results
                                            .errors
                                            .iter()
                                            .find(|&allow_error| analysis_error == allow_error)
                                        {
                                            Some(_matching_allowlist_error) => None,
                                            // No allowlist error match; report
                                            // error from within `filter_map`.
                                            None => Some(analysis_error.clone()),
                                        }
                                    }
                                    // No allowlist defined for capability type;
                                    // report error from within `filter_map`.
                                    None => Some(analysis_error.clone()),
                                }
                            })
                            .collect(),
                        ..Default::default()
                    },
                }
            })
            .collect()
    }
}

//
// V1 format and filter
//

#[derive(Clone, Deserialize, Serialize)]
struct V1ExtendableFormat {}

impl Versioned for V1ExtendableFormat {
    const VERSION: u64 = 1;
}

impl Default for V1ExtendableFormat {
    fn default() -> Self {
        Self {}
    }
}

impl ExtendableFormat for V1ExtendableFormat {
    fn append(&mut self, _other: Self) -> Result<()> {
        Ok(())
    }
}

impl AllowlistFilter for VersionedAllowlist<V1ExtendableFormat> {
    fn filter_analysis(&self, route_analysis: AnalysisResult) -> AllowlistResult {
        route_analysis
    }
}

#[cfg(test)]
mod tests {
    use super::{UnversionedAllowlist, V0Allowlist, V1Allowlist};

    #[test]
    fn unversioned_allowlist() {
        assert!(UnversionedAllowlist::new().load(Box::new(r"[]".as_bytes())).is_ok());
    }

    #[test]
    fn v0_allowlist() {
        assert!(V0Allowlist::new()
            .load(Box::new(
                r#"{
                    version: 0,
                    allowlist: [],
                }"#
                .as_bytes()
            ))
            .is_ok());
    }

    #[test]
    fn not_v0_allowlist() {
        assert!(V0Allowlist::new()
            .load(Box::new(
                r#"{
                    version: 0,
                    allowlist: {},
                }"#
                .as_bytes()
            ))
            .is_err());
        assert!(V0Allowlist::new()
            .load(Box::new(
                r#"{
                    version: 1,
                    allowlist: {},
                }"#
                .as_bytes()
            ))
            .is_err());
    }

    #[test]
    fn v1_allowlist() {
        assert!(V1Allowlist::new()
            .load(Box::new(
                r#"{
                    version: 1,
                    allowlist: {},
                }"#
                .as_bytes()
            ))
            .is_ok());
    }

    #[test]
    fn not_v1_allowlist() {
        assert!(V1Allowlist::new()
            .load(Box::new(
                r#"{
                    version: 0,
                    allowlist: {},
                }"#
                .as_bytes()
            ))
            .is_err());
    }
}
