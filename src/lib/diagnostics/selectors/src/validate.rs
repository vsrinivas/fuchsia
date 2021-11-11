// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    error::{StringPatternError, ValidationError},
    types::{
        self, ComparisonOperator, FilterExpression, Identifier, InclusionOperator, OneOrMany,
        Operator, Value,
    },
};
use fidl_fuchsia_diagnostics as fdiagnostics;
use lazy_static::lazy_static;
use regex::RegexSet;

// NOTE: if we could use the negative_impls unstable feature, we could have a single ValidateExt
// trait instead of one for each type of selector we need.
pub trait ValidateExt {
    fn validate(&self) -> Result<(), ValidationError>;
}

pub trait ValidateComponentSelectorExt {
    fn validate(&self) -> Result<(), ValidationError>;
}

pub trait ValidateTreeSelectorExt {
    fn validate(&self) -> Result<(), ValidationError>;
}

pub trait ValidateMetadataSelectorExt {
    fn validate(&self) -> Result<(), ValidationError>;
}

pub trait Selector {
    type Component: ComponentSelector;
    type Tree: TreeSelector;
    type Metadata: MetadataSelector;

    fn component(&self) -> Option<&Self::Component>;
    fn tree(&self) -> Option<&Self::Tree>;
    fn metadata(&self) -> Option<&Self::Metadata>;
}

pub trait ComponentSelector {
    type Segment: StringSelector;
    fn segments(&self) -> Option<&[Self::Segment]>;
}

pub trait TreeSelector {
    type Segment: StringSelector;

    fn node_path(&self) -> Option<&[Self::Segment]>;
    fn property(&self) -> Option<&Self::Segment>;
}

pub trait MetadataSelector {
    fn filters(&self) -> &[types::FilterExpression<'_>];
}

pub trait StringSelector {
    fn exact_match(&self) -> Option<&str>;
    fn pattern(&self) -> Option<&str>;
}

impl<T: Selector> ValidateExt for T {
    fn validate(&self) -> Result<(), ValidationError> {
        match (self.component(), self.tree()) {
            (Some(component), Some(tree)) => {
                component.validate()?;
                tree.validate()?;
            }
            (None, _) => return Err(ValidationError::MissingComponentSelector),
            (_, None) => return Err(ValidationError::MissingTreeSelector),
        }
        if let Some(metadata) = self.metadata() {
            metadata.validate()?;
        }
        Ok(())
    }
}

impl<T: ComponentSelector> ValidateComponentSelectorExt for T {
    fn validate(&self) -> Result<(), ValidationError> {
        let segments = self.segments().unwrap_or(&[]);
        if segments.is_empty() {
            return Err(ValidationError::EmptyComponentSelector);
        }
        let last_idx = segments.len() - 1;
        for segment in &segments[..last_idx] {
            segment.validate(StringSelectorValidationOpts::default())?;
        }
        segments[last_idx].validate(StringSelectorValidationOpts { allow_recursive_glob: true })?;
        Ok(())
    }
}

impl<T: TreeSelector> ValidateTreeSelectorExt for T {
    fn validate(&self) -> Result<(), ValidationError> {
        let node_path = self.node_path().unwrap_or(&[]);
        if node_path.is_empty() {
            return Err(ValidationError::EmptySubtreeSelector);
        }
        for segment in node_path {
            segment.validate(StringSelectorValidationOpts::default())?;
        }
        if let Some(segment) = self.property() {
            segment.validate(StringSelectorValidationOpts::default())?;
        }
        Ok(())
    }
}

impl<T: MetadataSelector> ValidateMetadataSelectorExt for T {
    fn validate(&self) -> Result<(), ValidationError> {
        for filter in self.filters() {
            filter.validate_identifier_and_operator()?;
            filter.validate_identifier_and_value()?;
            filter.validate_operator_and_value()?;
        }
        Ok(())
    }
}

// This macro is purely a utility fo validating that all the values in the given `$one_or_many` are
// of a given type.
macro_rules! match_one_or_many_value {
    ($one_or_many:ident, $variant:pat) => {
        match $one_or_many {
            OneOrMany::One($variant) => true,
            OneOrMany::Many(values) => values.iter().all(|value| matches!(value, $variant)),
            _ => false,
        }
    };
}

impl FilterExpression<'_> {
    /// Validates that all the values are of a type that can be used in an operation with this
    /// identifier.
    fn validate_identifier_and_value(&self) -> Result<(), ValidationError> {
        let is_valid = match (&self.identifier, &self.value) {
            (Identifier::Filename | Identifier::LifecycleEventType | Identifier::Tags, value) => {
                match_one_or_many_value!(value, Value::StringLiteral(_))
            }
            (
                Identifier::Pid | Identifier::Tid | Identifier::LineNumber | Identifier::Timestamp,
                value,
            ) => {
                match_one_or_many_value!(value, Value::Number(_))
            }
            // TODO(fxbug.dev/86962): it should also be possible to compare severities with a fixed
            // set of numbers.
            (Identifier::Severity, value) => {
                match_one_or_many_value!(value, Value::Severity(_))
            }
        };
        if is_valid {
            Ok(())
        } else {
            Err(ValidationError::InvalidValueType(self.identifier.clone(), self.value.ty()))
        }
    }

    /// Validates that this identifier can be used in an operation defined by the given `operator`.
    fn validate_identifier_and_operator(&self) -> Result<(), ValidationError> {
        match (&self.identifier, &self.operator) {
            (
                Identifier::Filename
                | Identifier::LifecycleEventType
                | Identifier::Pid
                | Identifier::Tid
                | Identifier::LineNumber
                | Identifier::Severity,
                Operator::Comparison(ComparisonOperator::Equal)
                | Operator::Comparison(ComparisonOperator::NotEq)
                | Operator::Inclusion(InclusionOperator::In),
            ) => Ok(()),
            (Identifier::Severity | Identifier::Timestamp, Operator::Comparison(_)) => Ok(()),
            (
                Identifier::Tags,
                Operator::Inclusion(InclusionOperator::HasAny | InclusionOperator::HasAll),
            ) => Ok(()),
            _ => Err(ValidationError::InvalidOperator(
                self.identifier.clone(),
                self.operator.clone(),
            )),
        }
    }

    fn validate_operator_and_value(&self) -> Result<(), ValidationError> {
        // Validate the operation can be used with the type of value.
        match (&self.operator, &self.value) {
            (Operator::Inclusion(_), OneOrMany::Many(_)) => Ok(()),
            (Operator::Comparison(_), OneOrMany::One(_)) => Ok(()),
            (Operator::Inclusion(_), OneOrMany::One(_))
            | (Operator::Comparison(_), OneOrMany::Many(_)) => {
                Err(ValidationError::InvalidOperatorRhs(self.operator.clone(), self.value.ty()))
            }
        }
    }
}

#[derive(Default)]
struct StringSelectorValidationOpts {
    allow_recursive_glob: bool,
}

trait ValidateStringSelectorExt {
    fn validate(&self, opts: StringSelectorValidationOpts) -> Result<(), ValidationError>;
}

// TODO(fxbug.dev/55118): we might want to just implement this differently for FIDL and the
// internal types. The parser should cover all of the string pattern requirements. Verify.
impl<T: StringSelector> ValidateStringSelectorExt for T {
    fn validate(&self, opts: StringSelectorValidationOpts) -> Result<(), ValidationError> {
        match (self.exact_match(), self.pattern()) {
            (None, None) | (Some(_), Some(_)) => {
                return Err(ValidationError::InvalidStringSelector)
            }
            (Some(_), None) => {
                // A exact match is always valid.
                return Ok(());
            }
            (None, Some(pattern)) => {
                if opts.allow_recursive_glob && pattern == "**" {
                    return Ok(());
                } else {
                    return validate_pattern(pattern);
                }
            }
        }
    }
}

lazy_static! {
    static ref PATTERN_VALIDATOR: RegexSet = RegexSet::new(&[
        // No glob expressions allowed.
        r#"([^\\]\*\*|^\*\*)"#,
        // No unescaped selector delimiters allowed.
        r#"([^\\]:|^:)"#,
        // No unescaped path delimiters allowed.
        r#"([^\\]/|^/)"#,
    ]).unwrap();
}

fn validate_pattern(pattern: &str) -> Result<(), ValidationError> {
    if pattern.is_empty() {
        return Err(ValidationError::EmptyStringPattern);
    }

    let validator_matches = PATTERN_VALIDATOR.matches(pattern);
    if validator_matches.matched_any() {
        let mut errors = vec![];
        if validator_matches.matched(0) {
            errors.push(StringPatternError::UnescapedGlob);
        }
        if validator_matches.matched(1) {
            errors.push(StringPatternError::UnescapedColon);
        }
        if validator_matches.matched(2) {
            errors.push(StringPatternError::UnescapedForwardSlash);
        }
        return Err(ValidationError::InvalidStringPattern(pattern.to_string(), errors));
    }
    Ok(())
}

impl Selector for fdiagnostics::Selector {
    type Component = fdiagnostics::ComponentSelector;
    type Tree = fdiagnostics::TreeSelector;
    // TODO(fxbug.dev/55118): placeholder implementation until we have metadata in the FIDL API.
    type Metadata = ();

    fn component(&self) -> Option<&Self::Component> {
        self.component_selector.as_ref()
    }

    fn tree(&self) -> Option<&Self::Tree> {
        self.tree_selector.as_ref()
    }

    fn metadata(&self) -> Option<&Self::Metadata> {
        // TODO(fxbug.dev/55118): placeholder implementation until we have metadata in the FIDL API.
        None
    }
}

impl MetadataSelector for () {
    fn filters(&self) -> &[types::FilterExpression<'_>] {
        unreachable!("placholder impl. Metadata FIDL not implemented yet")
    }
}

impl ComponentSelector for fdiagnostics::ComponentSelector {
    type Segment = fdiagnostics::StringSelector;

    fn segments(&self) -> Option<&[Self::Segment]> {
        self.moniker_segments.as_ref().map(|s| s.as_slice())
    }
}

impl TreeSelector for fdiagnostics::TreeSelector {
    type Segment = fdiagnostics::StringSelector;

    fn node_path(&self) -> Option<&[Self::Segment]> {
        match self {
            Self::SubtreeSelector(t) => Some(&t.node_path[..]),
            Self::PropertySelector(p) => Some(&p.node_path[..]),
            fdiagnostics::TreeSelectorUnknown!() => None,
        }
    }

    fn property(&self) -> Option<&Self::Segment> {
        match self {
            Self::SubtreeSelector(_) => None,
            Self::PropertySelector(p) => Some(&p.target_properties),
            fdiagnostics::TreeSelectorUnknown!() => None,
        }
    }
}

impl StringSelector for fdiagnostics::StringSelector {
    fn exact_match(&self) -> Option<&str> {
        match self {
            Self::ExactMatch(s) => Some(s),
            _ => None,
        }
    }

    fn pattern(&self) -> Option<&str> {
        match self {
            Self::StringPattern(s) => Some(s),
            _ => None,
        }
    }
}

impl<'a> Selector for types::Selector<'a> {
    type Component = types::ComponentSelector<'a>;
    type Tree = types::TreeSelector<'a>;
    type Metadata = types::MetadataSelector<'a>;

    fn component(&self) -> Option<&Self::Component> {
        Some(&self.component)
    }

    fn tree(&self) -> Option<&Self::Tree> {
        Some(&self.tree)
    }

    fn metadata(&self) -> Option<&Self::Metadata> {
        self.metadata.as_ref()
    }
}

impl<'a> ComponentSelector for types::ComponentSelector<'a> {
    type Segment = types::Segment<'a>;

    fn segments(&self) -> Option<&[Self::Segment]> {
        Some(&self.segments[..])
    }
}

impl<'a> TreeSelector for types::TreeSelector<'a> {
    type Segment = types::Segment<'a>;

    fn node_path(&self) -> Option<&[Self::Segment]> {
        Some(&self.node)
    }

    fn property(&self) -> Option<&Self::Segment> {
        self.property.as_ref()
    }
}

impl<'a> MetadataSelector for types::MetadataSelector<'a> {
    fn filters(&self) -> &[types::FilterExpression<'a>] {
        self.filters()
    }
}

impl<'a> StringSelector for types::Segment<'a> {
    fn exact_match(&self) -> Option<&str> {
        match self {
            Self::ExactMatch(s) => Some(s),
            _ => None,
        }
    }

    fn pattern(&self) -> Option<&str> {
        match self {
            Self::Pattern(s) => Some(s),
            _ => None,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    lazy_static! {
        static ref SHARED_PASSING_TEST_CASES: Vec<(Vec<&'static str>, &'static str)> = {
            vec![
                (vec![r#"abc"#, r#"def"#, r#"g"#], r#"bob"#),
                (vec![r#"\**"#], r#"\**"#),
                (vec![r#"\/"#], r#"\/"#),
                (vec![r#"\:"#], r#"\:"#),
                (vec![r#"asda\\\:"#], r#"a"#),
                (vec![r#"asda*"#], r#"a"#),
            ]
        };
        static ref SHARED_FAILING_TEST_CASES: Vec<(Vec<&'static str>, &'static str)> = {
            vec![
                // Slashes aren't allowed in path nodes.
                (vec![r#"/"#], r#"a"#),
                // Colons aren't allowed in path nodes.
                (vec![r#":"#], r#"a"#),
                // Checking that path nodes ending with offlimits
                // chars are still identified.
                (vec![r#"asdasd:"#], r#"a"#),
                (vec![r#"a**"#], r#"a"#),
                // Checking that path nodes starting with offlimits
                // chars are still identified.
                (vec![r#":asdasd"#], r#"a"#),
                (vec![r#"**a"#], r#"a"#),
                // Neither moniker segments nor node paths
                // are allowed to be empty.
                (vec![], r#"bob"#),
            ]
        };
    }

    #[fuchsia::test]
    fn tree_selector_validator_test() {
        let unique_failing_test_cases = vec![
            // All failing validators due to property selectors are
            // unique since the component validator doesnt look at them.
            (vec![r#"a"#], r#"**"#),
            (vec![r#"a"#], r#"/"#),
        ];

        fn create_tree_selector(
            node_path: &Vec<&str>,
            property: &str,
        ) -> fdiagnostics::TreeSelector {
            let node_path = node_path
                .iter()
                .map(|path_node_str| {
                    fdiagnostics::StringSelector::StringPattern(path_node_str.to_string())
                })
                .collect::<Vec<fdiagnostics::StringSelector>>();
            let target_properties =
                fdiagnostics::StringSelector::StringPattern(property.to_string());
            fdiagnostics::TreeSelector::PropertySelector(fdiagnostics::PropertySelector {
                node_path,
                target_properties,
            })
        }

        for (node_path, property) in SHARED_PASSING_TEST_CASES.iter() {
            let tree_selector = create_tree_selector(node_path, property);
            assert!(tree_selector.validate().is_ok());
        }

        for (node_path, property) in SHARED_FAILING_TEST_CASES.iter() {
            let tree_selector = create_tree_selector(node_path, property);
            assert!(
                ValidateTreeSelectorExt::validate(&tree_selector).is_err(),
                "Failed to validate tree selector: {:?}",
                tree_selector
            );
        }

        for (node_path, property) in unique_failing_test_cases.iter() {
            let tree_selector = create_tree_selector(node_path, property);
            assert!(
                ValidateTreeSelectorExt::validate(&tree_selector).is_err(),
                "Failed to validate tree selector: {:?}",
                tree_selector
            );
        }
    }

    #[fuchsia::test]
    fn component_selector_validator_test() {
        fn create_component_selector(
            component_moniker: &Vec<&str>,
        ) -> fdiagnostics::ComponentSelector {
            let mut component_selector = fdiagnostics::ComponentSelector::EMPTY;
            component_selector.moniker_segments = Some(
                component_moniker
                    .into_iter()
                    .map(|path_node_str| {
                        fdiagnostics::StringSelector::StringPattern(path_node_str.to_string())
                    })
                    .collect::<Vec<fdiagnostics::StringSelector>>(),
            );
            component_selector
        }

        for (component_moniker, _) in SHARED_PASSING_TEST_CASES.iter() {
            let component_selector = create_component_selector(component_moniker);

            assert!(component_selector.validate().is_ok());
        }

        for (component_moniker, _) in SHARED_FAILING_TEST_CASES.iter() {
            let component_selector = create_component_selector(component_moniker);

            assert!(
                component_selector.validate().is_err(),
                "Failed to validate component selector: {:?}",
                component_selector
            );
        }
    }
}
