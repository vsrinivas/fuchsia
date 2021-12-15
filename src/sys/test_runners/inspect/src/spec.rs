// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::error::ComponentError,
    fidl_fuchsia_data as fdata,
    selectors::{self, VerboseError},
    std::collections::BTreeMap,
    std::convert::TryFrom,
};

#[derive(Debug)]
pub(crate) struct ProgramSpec {
    /// The name of the accessor to use.
    pub accessor: Accessor,

    /// The maximum amount of time to wait for this value to be present.
    pub timeout_seconds: i64,

    /// The test cases to execute.
    pub cases: BTreeMap<String, TestCase>,
}

impl ProgramSpec {
    /// Returns the list of test names from the program spec.
    pub fn test_names(&self) -> Vec<String> {
        self.cases.iter().map(|c| c.0.to_string()).collect()
    }
}

impl TryFrom<fdata::Dictionary> for ProgramSpec {
    type Error = ComponentError;

    fn try_from(dict: fdata::Dictionary) -> Result<Self, Self::Error> {
        let mut accessor = None;
        let mut timeout_seconds = None;
        let mut cases = None;

        for case in dict.entries.ok_or(ComponentError::MissingRequiredKey("program"))?.into_iter() {
            match case.key.as_ref() {
                "accessor" => {
                    accessor = match *case
                        .value
                        .ok_or(ComponentError::MissingRequiredKey("accessor"))?
                    {
                        fdata::DictionaryValue::Str(s) if s == "ALL" => Some(Accessor::All),
                        fdata::DictionaryValue::Str(s) if s == "FEEDBACK" => {
                            Some(Accessor::Feedback)
                        }
                        fdata::DictionaryValue::Str(s) if s == "LEGACY" => Some(Accessor::Legacy),
                        v => {
                            return Err(ComponentError::UnknownAccessorValue(format!("{:?}", v)));
                        }
                    }
                }
                "timeout_seconds" => {
                    if let fdata::DictionaryValue::Str(val) =
                        *case.value.ok_or(ComponentError::MissingRequiredKey("timeout_seconds"))?
                    {
                        timeout_seconds = Some(val.parse::<i64>().map_err(|_| {
                            ComponentError::WrongType("timeout_seconds", "numeric string")
                        })?);
                        if *timeout_seconds.as_ref().unwrap() <= 0 {
                            return Err(ComponentError::InvalidTimeout);
                        }
                    } else {
                        return Err(ComponentError::WrongType("timeout_seconds", "numeric string"));
                    }
                }
                "cases" => {
                    if let fdata::DictionaryValue::StrVec(val) =
                        *case.value.ok_or(ComponentError::MissingRequiredKey("cases"))?
                    {
                        let items: Result<Vec<TestCase>, _> =
                            val.into_iter().map(|v| TestCase::try_from(v)).collect();
                        cases = Some(items?);
                    } else {
                        return Err(ComponentError::WrongType("cases", "string array"));
                    }
                }
                k => {
                    return Err(ComponentError::UnknownKey(k.to_string()));
                }
            }
        }

        match (accessor, timeout_seconds, cases) {
            (Some(accessor), Some(timeout_seconds), Some(cases)) => {
                let cases: BTreeMap<String, TestCase> =
                    cases.into_iter().map(|c| (c.key.clone(), c)).collect();

                Ok(ProgramSpec { accessor, timeout_seconds, cases })
            }
            (None, _, _) => Err(ComponentError::MissingRequiredKey("accessor")),
            (_, None, _) => Err(ComponentError::MissingRequiredKey("timeout_seconds")),
            (_, _, None) => Err(ComponentError::MissingRequiredKey("cases")),
        }
    }
}

#[derive(Debug, PartialEq)]
pub(crate) enum Accessor {
    All,
    Feedback,
    Legacy,
}

#[derive(Debug, PartialEq)]
pub(crate) struct TestCase {
    /// The unique key for this test case inside this suite.
    pub key: String,

    /// The selector that will be used to read from the Archivist for this test case.
    pub selector: String,

    /// If set, this triage expression is applied against the value returned by the selector.
    /// Otherwise, the test passes so long as any value is selected.
    pub expression: Option<String>,
}

impl std::string::ToString for TestCase {
    fn to_string(&self) -> String {
        self.key.clone()
    }
}

impl TryFrom<String> for TestCase {
    type Error = ComponentError;
    fn try_from(value: String) -> Result<Self, Self::Error> {
        let splits: Vec<&str> = value.split(" WHERE ").collect();
        if splits.len() == 1 {
            let selector = splits[0].to_string();
            selectors::parse_selector::<VerboseError>(&selector).map_err(|e| {
                ComponentError::InvalidTestCase { value: value.clone(), reason: e.to_string() }
            })?;
            Ok(TestCase { key: value, selector, expression: None })
        } else if splits.len() == 2 {
            let selector = splits[0].to_string();
            let expression = Some(splits[1].to_string());
            selectors::parse_selector::<VerboseError>(&selector).map_err(|e| {
                ComponentError::InvalidTestCase { value: value.clone(), reason: e.to_string() }
            })?;
            Ok(TestCase { key: value, selector, expression })
        } else {
            Err(ComponentError::InvalidTestCase {
                value,
                reason: "too many 'WHERE' clauses".to_string(),
            })
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[derive(Clone, Debug)]
    struct DictBuilder {
        accessor: Option<String>,
        timeout_seconds: Option<String>,
        cases: Vec<String>,
    }

    impl DictBuilder {
        fn new() -> Self {
            DictBuilder { accessor: None, timeout_seconds: None, cases: vec![] }
        }

        fn with_accessor(mut self, value: impl Into<String>) -> Self {
            self.accessor = Some(value.into());
            self
        }

        fn with_timeout_seconds(mut self, value: impl Into<String>) -> Self {
            self.timeout_seconds = Some(value.into());
            self
        }

        fn add_case(mut self, value: impl Into<String>) -> Self {
            self.cases.push(value.into());
            self
        }

        fn build(self) -> fdata::Dictionary {
            let mut ret = fdata::Dictionary { entries: Some(vec![]), ..fdata::Dictionary::EMPTY };

            let mut add_value = |key: &str, value: fdata::DictionaryValue| {
                ret.entries.as_mut().expect("entries exists").push(fdata::DictionaryEntry {
                    key: key.to_string(),
                    value: Some(Box::new(value)),
                });
            };

            if let Some(val) = self.accessor {
                add_value("accessor", fdata::DictionaryValue::Str(val));
            }
            if let Some(val) = self.timeout_seconds {
                add_value("timeout_seconds", fdata::DictionaryValue::Str(val));
            }
            if self.cases.len() > 0 {
                add_value("cases", fdata::DictionaryValue::StrVec(self.cases));
            }

            ret
        }
    }

    #[test]
    fn empty_dictionary_fails() {
        assert_eq!(
            ComponentError::MissingRequiredKey("program"),
            ProgramSpec::try_from(fdata::Dictionary { ..fdata::Dictionary::EMPTY }).unwrap_err()
        );
    }

    #[test]
    fn fields_are_present() {
        assert_eq!(
            ComponentError::MissingRequiredKey("accessor"),
            ProgramSpec::try_from(DictBuilder::new().build()).unwrap_err(),
        );
        assert_eq!(
            ComponentError::MissingRequiredKey("timeout_seconds"),
            ProgramSpec::try_from(DictBuilder::new().with_accessor("ALL").build()).unwrap_err(),
        );
        assert_eq!(
            ComponentError::MissingRequiredKey("cases"),
            ProgramSpec::try_from(
                DictBuilder::new().with_accessor("ALL").with_timeout_seconds("10").build()
            )
            .unwrap_err(),
        );
        assert!(ProgramSpec::try_from(
            DictBuilder::new()
                .with_accessor("ALL")
                .with_timeout_seconds("10")
                .add_case("a:b:c")
                .build()
        )
        .is_ok());
    }

    #[test]
    fn accessor_validation() {
        let base_dict = DictBuilder::new().with_timeout_seconds("10").add_case("a:b:c");
        assert_eq!(
            ComponentError::UnknownAccessorValue("Str(\"SOMETHING\")".to_string()),
            ProgramSpec::try_from(base_dict.clone().with_accessor("SOMETHING").build())
                .unwrap_err(),
        );
        for (name, value) in vec![
            ("ALL", Accessor::All),
            ("FEEDBACK", Accessor::Feedback),
            ("LEGACY", Accessor::Legacy),
        ] {
            let spec = ProgramSpec::try_from(base_dict.clone().with_accessor(name).build())
                .expect("validate accessor");
            assert_eq!(spec.accessor, value);
        }
    }

    #[test]
    fn timeout_seconds_validation() {
        let base_dict = DictBuilder::new().with_accessor("ALL").add_case("a:b:c");
        assert_eq!(
            ComponentError::WrongType("timeout_seconds", "numeric string"),
            ProgramSpec::try_from(base_dict.clone().with_timeout_seconds("invalid").build())
                .unwrap_err()
        );
        let spec = ProgramSpec::try_from(base_dict.clone().with_timeout_seconds("100").build())
            .expect("validate timeout_seconds");
        assert_eq!(spec.timeout_seconds, 100);
    }

    #[test]
    fn test_case_validation() {
        let base_dict = DictBuilder::new().with_accessor("ALL").with_timeout_seconds("10");
        assert_eq!(
            ComponentError::InvalidTestCase {
                value: "a WHERE b WHERE c".to_string(),
                reason: "too many 'WHERE' clauses".to_string(),
            },
            ProgramSpec::try_from(base_dict.clone().add_case("a WHERE b WHERE c").build())
                .unwrap_err()
        );

        // Test that selector parsing catches invalid selectors.
        assert!(ProgramSpec::try_from(base_dict.clone().add_case("a:b:c:d").build()).is_err());
        assert!(ProgramSpec::try_from(base_dict.clone().add_case("a").build()).is_err());

        let spec = ProgramSpec::try_from(
            base_dict.clone().add_case("a:b:c").add_case("a:b:c WHERE [d] d > 0").build(),
        )
        .expect("validate cases");
        assert_eq!(
            spec.cases["a:b:c"],
            TestCase { key: "a:b:c".to_string(), selector: "a:b:c".to_string(), expression: None }
        );
        assert_eq!(
            spec.cases["a:b:c WHERE [d] d > 0"],
            TestCase {
                key: "a:b:c WHERE [d] d > 0".to_string(),
                selector: "a:b:c".to_string(),
                expression: Some("[d] d > 0".to_string())
            }
        );
    }
}
