// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_diagnostics::Selector,
    selectors::{parse_selector, selector_to_string, VerboseError},
    serde::{Deserialize, Serialize},
    std::collections::HashMap,
    thiserror::Error,
};

#[derive(Error, Debug, PartialEq)]
pub enum MappingError {
    #[error("List match was unbounded or contained cycles")]
    Unbounded,
    #[error("map_selector was attempted but converting the input to a string failed: {0}")]
    BadInputSelector(String),
    #[error(
        "map_selector was attempted but the source mapping list contains an invalid selector: {0}. Error was: {1}"
    )]
    BadSelector(String, String),
}

#[derive(Debug, Default, Deserialize, Serialize)]
pub struct MappingList {
    #[serde(flatten)]
    mappings: HashMap<String, String>,
}

impl MappingList {
    pub fn new(mappings: HashMap<String, String>) -> Self {
        Self { mappings }
    }

    pub fn map(&self, input: &str) -> Result<String, MappingError> {
        let mut result = input.to_string();
        let limit = self.mappings.len();
        let mut count = 0;
        while let Some(new_result) = self.mappings.get(&result) {
            result = new_result.clone();
            count += 1;
            if count > limit {
                return Err(MappingError::Unbounded);
            }
        }
        Ok(result)
    }
}

#[derive(Debug, Default, Deserialize, Serialize)]
pub struct SelectorMappingList {
    #[serde(flatten)]
    mappings: MappingList,
}

impl SelectorMappingList {
    pub fn new(mappings: HashMap<String, String>) -> Self {
        Self { mappings: MappingList::new(mappings) }
    }

    pub fn map_selector(&self, input: Selector) -> Result<Selector, MappingError> {
        if let Ok(selector_str) = selector_to_string(input.clone()) {
            match self.mappings.map(&selector_str) {
                Ok(mapped_selector) => parse_selector::<VerboseError>(&mapped_selector)
                    .map_err(|e| MappingError::BadSelector(mapped_selector, format!("{:?}", e))),
                Err(e) => Err(e),
            }
        } else {
            Err(MappingError::BadInputSelector(format!("{:?}", input)))
        }
    }
}

#[cfg(test)]
mod test {
    use {super::*, assert_matches::assert_matches};

    fn tup_to_map(items: Vec<(&str, &str)>) -> HashMap<String, String> {
        items.iter().map(|s| (s.0.to_string(), s.1.to_string())).collect()
    }

    #[test]
    fn map_list_one_matching_entry() {
        let list =
            MappingList::new(tup_to_map(vec![("apples", "oranges"), ("bananas", "strawberries")]));

        assert_eq!(list.map("bananas"), Ok("strawberries".to_string()))
    }

    #[test]
    fn map_list_no_matching_entries() {
        let list =
            MappingList::new(tup_to_map(vec![("apples", "oranges"), ("bananas", "strawberries")]));

        assert_eq!(list.map("blueberries"), Ok("blueberries".to_string()))
    }

    #[test]
    fn map_list_maps_recursively() {
        let list =
            MappingList::new(tup_to_map(vec![("oranges", "strawberries"), ("apples", "oranges")]));

        assert_eq!(list.map("apples"), Ok("strawberries".to_string()))
    }

    #[test]
    fn map_list_returns_unbounded_error_on_cycles() {
        let list = MappingList::new(tup_to_map(vec![("oranges", "apples"), ("apples", "oranges")]));

        assert_matches!(list.map("apples"), Err(MappingError::Unbounded))
    }

    #[test]
    fn selector_map_with_matching_entries() {
        let list = SelectorMappingList::new(tup_to_map(vec![
            ("oranges:apples:bananas", "apples:bananas:oranges"),
            ("a:b:c", "c:b:a"),
        ]));

        assert_eq!(
            list.map_selector(parse_selector::<VerboseError>("oranges:apples:bananas").unwrap())
                .unwrap(),
            parse_selector::<VerboseError>("apples:bananas:oranges").unwrap()
        )
    }

    #[test]
    fn selector_map_with_broken_entry() {
        let list = SelectorMappingList::new(tup_to_map(vec![
            ("oranges:apples:bananas", "not_real::::::"),
            ("a:b:c", "c:b:a"),
        ]));

        match list
            .map_selector(parse_selector::<VerboseError>("oranges:apples:bananas").unwrap())
            .unwrap_err()
        {
            MappingError::BadSelector(s, _) => {
                assert_eq!(s, String::from("not_real::::::"))
            }
            e => panic!("unexpected error type {:?}", e),
        }
    }
}
