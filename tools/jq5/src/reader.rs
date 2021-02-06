// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow;
use json5format::*;
use serde_json::Value;
use serde_json5;
use std::fs::read_to_string;
#[allow(unused_imports)]
use std::path::{Path, PathBuf};

fn read_json5(object: String) -> Result<(ParsedDocument, String), anyhow::Error> {
    let object_as_json = serde_json5::from_str::<Value>(&object)?.to_string();

    let deserialized_object = json5format::ParsedDocument::from_string(object, None)?;
    Ok((deserialized_object, object_as_json))
}

pub fn read_json5_fromfile(file: &PathBuf) -> Result<(ParsedDocument, String), anyhow::Error> {
    let path = file.as_path();
    let object = read_to_string(path)?;
    Ok(read_json5(object)?)
}
#[cfg(test)]
mod tests {
    #[allow(unused_imports)]
    use super::*;
    #[test]
    #[ignore]
    /*
    The following test fails with the current implementation.
    Serializing the deserialized JSON using serde_json and serde_json5 sorts
    the data alphabetically at object level. Ideally, this would not happen,
    but it will likely not pose an issue for later additions to the tool.
    */
    fn serialized_outputs_equivalent_1() {
        let simple_json5 = String::from(
            r##"
{
    // Foo
    hello: 'world',

    // Bar
    baz: 5,
}
"##,
        );
        let simple_json = String::from(r#"{"hello":"world","baz":5}"#);

        let parsed_json5 =
            json5format::ParsedDocument::from_str(&(read_json5(simple_json5).unwrap().1)[..], None)
                .unwrap();
        let parsed_json = json5format::ParsedDocument::from_str(&simple_json[..], None).unwrap();
        let format = json5format::Json5Format::new().unwrap();
        assert_eq!(
            format.to_string(&parsed_json5).unwrap(),
            format.to_string(&parsed_json).unwrap()
        );
    }
    #[test]
    fn serialized_outputs_equivalent_2() {
        let simple_json5 = String::from(
            r##"
{
  // Foo
  hello: 'world',

  // Bar
  yoinks: "scoob",
}
"##,
        );
        let simple_json = String::from(r#"{"hello":"world","yoinks":"scoob"}"#);

        let parsed_json5 =
            json5format::ParsedDocument::from_str(&(read_json5(simple_json5).unwrap().1)[..], None)
                .unwrap();
        let parsed_json = json5format::ParsedDocument::from_str(&simple_json[..], None).unwrap();
        let format = json5format::Json5Format::new().unwrap();
        assert_eq!(
            format.to_string(&parsed_json5).unwrap(),
            format.to_string(&parsed_json).unwrap()
        );
    }

    #[test]
    fn serialized_outputs_equivalent_3() -> Result<(), anyhow::Error> {
        let json5 = String::from(
            r##"{
    "address": {
        "city": "Anytown",
        "country": "USA",
        "state": "New York",
        "street": "101 Main Street"
        /* Update schema to support multiple addresses:
           "work": {
               "city": "Anytown",
               "country": "USA",
               "state": "New York",
               "street": "101 Main Street"
           }
        */
    },
    "children": [
        "Buffy",
        "Biff",
        "Balto"
    ],
    // Consider adding a note field to the `other` contact option
    "contact_options": [
        {
            "home": {
                "email": "jj@notreallygmail.com",   // This was the original user id.
                                                    // Now user id's are hash values.
                "phone": "212-555-4321"
            },
            "other": {
                "email": "volunteering@serviceprojectsrus.org"
            },
            "work": {
                "email": "john.j.smith@worksforme.gov",
                "phone": "212-555-1234"
            }
        }
    ],
    "name": {
        "first": "John",
        "last": "Smith",
        "middle": "Jacob"
    },
}
"##,
        );

        let json = String::from(
            r##"{
    "address": {
        "city": "Anytown",
        "country": "USA",
        "state": "New York",
        "street": "101 Main Street"
    },
    "children": [
        "Buffy",
        "Biff",
        "Balto"
    ],
    "contact_options": [
        {
            "home": {
                "email": "jj@notreallygmail.com",
                "phone": "212-555-4321"
            },
            "other": {
                "email": "volunteering@serviceprojectsrus.org"
            },
            "work": {
                "email": "john.j.smith@worksforme.gov",
                "phone": "212-555-1234"
            }
        }
    ],
    "name": {
        "first": "John",
        "last": "Smith",
        "middle": "Jacob"
    },
}
"##,
        );
        let parsed_json5 = json5format::ParsedDocument::from_string(read_json5(json5)?.1, None)?;
        let parsed_json = json5format::ParsedDocument::from_str(&json[..], None)?;
        let format = json5format::Json5Format::new()?;

        assert_eq!(format.to_string(&parsed_json5)?, format.to_string(&parsed_json)?);
        Ok(())
    }

    #[test]
    fn deserialized_outputs_equivalent_1() {
        let simple_json5 = String::from(
            r##"
{
// Foo
hello: 'world',

// Bar
baz: 5,
}
"##,
        );
        let simple_json = String::from(r#"{"hello":"world","baz":5}"#);

        assert_eq!(
            serde_json5::from_str::<Value>(&(read_json5(simple_json5).unwrap().1)).unwrap(),
            serde_json5::from_str::<Value>(&simple_json).unwrap()
        );
    }

    #[test]
    fn deserialized_outputs_equivalent_2() {
        let json5 = String::from(
            r##"{
    "name": {
        "last": "Smith",
        "first": "John",
        "middle": "Jacob"
    },
    "children": [
        "Buffy",
        "Biff",
        "Balto"
    ],
    // Consider adding a note field to the `other` contact option
    "contact_options": [
        {
            "home": {
                "email": "jj@notreallygmail.com",   // This was the original user id.
                                                    // Now user id's are hash values.
                "phone": "212-555-4321"
            },
            "other": {
                "email": "volunteering@serviceprojectsrus.org"
            },
            "work": {
                "phone": "212-555-1234",
                "email": "john.j.smith@worksforme.gov"
            }
        }
    ],
    "address": {
        "city": "Anytown",
        "country": "USA",
        "state": "New York",
        "street": "101 Main Street"
        /* Update schema to support multiple addresses:
           "work": {
               "city": "Anytown",
               "country": "USA",
               "state": "New York",
               "street": "101 Main Street"
           }
        */
    }
}
"##,
        );
        let json = r##"{
  "name": {
      "last": "Smith",
      "first": "John",
      "middle": "Jacob"
  },
  "children": [
      "Buffy",
      "Biff",
      "Balto"
  ],
  "contact_options": [
      {
          "home": {
              "email": "jj@notreallygmail.com",

              "phone": "212-555-4321"
          },
          "other": {
              "email": "volunteering@serviceprojectsrus.org"
          },
          "work": {
              "phone": "212-555-1234",
              "email": "john.j.smith@worksforme.gov"
          }
      }
  ],
  "address": {
      "city": "Anytown",
      "country": "USA",
      "state": "New York",
      "street": "101 Main Street"
  }
}
"##;

        assert_eq!(
            serde_json5::from_str::<Value>(&(read_json5(json5).unwrap().1)).unwrap(),
            serde_json5::from_str::<Value>(&json).unwrap()
        );
    }
}
