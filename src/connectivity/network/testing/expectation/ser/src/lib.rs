// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use serde::ser::SerializeSeq as _;

#[derive(serde::Deserialize, serde::Serialize, Debug)]
pub struct Expectations {
    #[serde(rename = "actions")]
    pub expectations: Vec<Expectation>,
}

#[derive(serde::Deserialize, serde::Serialize, Debug)]
pub struct Matchers {
    #[serde(deserialize_with = "deserialize_glob_vec", serialize_with = "serialize_glob_vec")]
    pub matchers: Vec<glob::Pattern>,
}

fn deserialize_glob_vec<'de, D>(deserializer: D) -> Result<Vec<glob::Pattern>, D::Error>
where
    D: serde::Deserializer<'de>,
{
    let strings: Vec<String> = serde::Deserialize::deserialize(deserializer)?;
    strings.into_iter().map(|s| s.parse().map_err(serde::de::Error::custom)).collect()
}

fn serialize_glob_vec<S>(globs: &[glob::Pattern], serializer: S) -> Result<S::Ok, S::Error>
where
    S: serde::Serializer,
{
    let mut seq = serializer.serialize_seq(Some(globs.len()))?;
    for glob in globs {
        seq.serialize_element(glob.as_str())?;
    }
    seq.end()
}

#[derive(serde::Deserialize, serde::Serialize, Debug)]
#[serde(tag = "type", rename_all = "snake_case")]
pub enum Expectation {
    ExpectFailure(Matchers),
    ExpectPass(Matchers),
    Skip(Matchers),
}

#[derive(serde::Deserialize, serde::Serialize, Debug)]
pub struct Include {
    #[serde(rename = "include")]
    pub path: String,
}

#[derive(serde::Deserialize, serde::Serialize, Debug)]
#[serde(untagged)]
pub enum UnmergedExpectation {
    Include(Include),
    Expectation(Expectation),
}

#[derive(serde::Deserialize, serde::Serialize, Debug)]
pub struct UnmergedExpectations {
    #[serde(rename = "actions")]
    pub expectations: Vec<UnmergedExpectation>,
}
