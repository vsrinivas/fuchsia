// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// StringList is adapted from SelectorEntry in
// src/diagnostics/lib/triage/src/config.rs

#[derive(Clone, Debug, PartialEq)]
pub(crate) struct StringList(Vec<String>);

use serde::{Deserialize, Deserializer};

impl std::ops::Deref for StringList {
    type Target = Vec<String>;

    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

impl std::ops::DerefMut for StringList {
    fn deref_mut(&mut self) -> &mut Self::Target {
        &mut self.0
    }
}

impl<'de> Deserialize<'de> for StringList {
    fn deserialize<D>(d: D) -> Result<Self, D::Error>
    where
        D: Deserializer<'de>,
    {
        struct StringVec(std::marker::PhantomData<Vec<String>>);

        impl<'de> serde::de::Visitor<'de> for StringVec {
            type Value = Vec<String>;

            fn expecting(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
                f.write_str("either a single string or an array of strings")
            }

            fn visit_str<E>(self, value: &str) -> Result<Self::Value, E>
            where
                E: serde::de::Error,
            {
                Ok(vec![value.to_string()])
            }

            fn visit_seq<A>(self, mut value: A) -> Result<Self::Value, A::Error>
            where
                A: serde::de::SeqAccess<'de>,
            {
                let mut out = vec![];
                while let Some(s) = value.next_element::<String>()? {
                    out.push(s);
                }
                if out.is_empty() {
                    use serde::de::Error;
                    Err(A::Error::invalid_length(0, &"expected at least one string"))
                } else {
                    Ok(out)
                }
            }
        }

        Ok(StringList(d.deserialize_any(StringVec(std::marker::PhantomData))?))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use anyhow::Error;

    #[fuchsia::test]
    fn parse_valid_single_string() -> Result<(), Error> {
        let json = "\"whatever-1982035*()$*H\"";
        let strings: StringList = serde_json5::from_str(json)?;
        assert_eq!(strings.len(), 1);
        assert_eq!(strings[0], "whatever-1982035*()$*H");
        Ok(())
    }

    #[fuchsia::test]
    fn parse_valid_multiple_strings() -> Result<(), Error> {
        let json = "[ \"core/foo:not:a:selector:root/branch:leaf\", \"core/bar:root/twig:leaf\"]";
        let strings: StringList = serde_json5::from_str(json)?;
        assert_eq!(strings.len(), 2);
        assert_eq!(strings[0], "core/foo:not:a:selector:root/branch:leaf");
        assert_eq!(strings[1], "core/bar:root/twig:leaf");
        Ok(())
    }

    #[fuchsia::test]
    fn refuse_invalid_strings() {
        let not_string = "42";
        let bad_list = "[ 42, \"core/bar:not:a:selector:root/twig:leaf\"]";
        let parsed: Result<StringList, serde_json5::Error> = serde_json5::from_str(not_string);
        parsed.expect_err("this should fail");
        let parsed: Result<StringList, serde_json5::Error> = serde_json5::from_str(bad_list);
        parsed.expect_err("this should fail");
    }
}
