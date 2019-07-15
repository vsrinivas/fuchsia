// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use {
    crate::action_match::query_text_match,
    serde_derive::Deserialize,
    std::{collections::HashMap, str::FromStr},
};

type StoryId = String;
type StoryName = String;

#[derive(Clone, Deserialize, Debug)]
pub struct StoryNameIndex {
    story_name_ids: HashMap<StoryId, StoryName>,
}

// deserialize StoryNameIndex from a string
impl FromStr for StoryNameIndex {
    type Err = serde_json::error::Error;

    fn from_str(content: &str) -> Result<Self, Self::Err> {
        serde_json::from_str(content)
    }
}

impl StoryNameIndex {
    pub fn new() -> Self {
        StoryNameIndex { story_name_ids: HashMap::new() }
    }

    // add a new story entry into the index with its name and id
    pub fn add_story(&mut self, story_id: impl Into<String>, story_name: impl Into<String>) {
        self.story_name_ids.insert(story_id.into(), story_name.into());
    }

    //match the story name with query text and return related stories
    pub fn match_by_query(&self, query: &'static str) -> impl Iterator<Item = &StoryId> + '_ {
        self.story_name_ids.iter().filter_map(move |(k, v)| {
            if query_text_match(query, v) {
                Some(k)
            } else {
                None
            }
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn test_add_story() {
        let mut index1 = StoryNameIndex::new();
        assert_eq!(index1.story_name_ids.len(), 0);
        index1.add_story("001".to_string(), "cities in spain".to_string());
        index1.add_story("002".to_string(), "cities in france".to_string());
        index1.add_story("003".to_string(), "cities in germany".to_string());
        assert_eq!(index1.story_name_ids.len(), 3);
        assert_eq!(index1.story_name_ids["001"], "cities in spain".to_string());
    }

    #[test]
    fn test_from_str() {
        let index1 =
            StoryNameIndex::from_str(include_str!("../test_data/story_name_index.json")).unwrap();
        assert_eq!(index1.story_name_ids.len(), 6);
        assert_eq!(index1.story_name_ids["001"], "cities in spain".to_string());
    }

    #[test]
    fn test_match_by_query() {
        let index1 =
            StoryNameIndex::from_str(include_str!("../test_data/story_name_index.json")).unwrap();
        assert_eq!(index1.match_by_query("cities").count(), 3);
        assert_eq!(index1.match_by_query("spain").count(), 2);
        assert_eq!(index1.match_by_query("in").count(), 6);
    }
}
