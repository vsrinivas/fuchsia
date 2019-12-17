// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::MetricValue;

pub fn fetch(inspect_data: &Vec<serde_json::Value>, selector: &String) -> MetricValue {
    // TODO(cphoenix): Use Luke's selector crate.
    let parts: Vec<_> = selector.split(":").collect();
    if parts.len() != 3 {
        return MetricValue::Missing(format!("Bad selector '{}'", selector));
    }
    let empty = "".to_string();
    for entry in inspect_data.iter() {
        let path = match entry {
            serde_json::Value::Object(o) => match &o["path"] {
                serde_json::Value::String(s) => s,
                _ => &empty,
            },
            _ => &empty,
        };
        if path.contains(parts[0]) {
            let mut node = &entry["contents"];
            let mut selectors: Vec<_> = parts[1].split(".").collect();
            selectors.push(parts[2]);
            for name in selectors {
                if let serde_json::Value::Object(map) = node {
                    if let Some(entry) = map.get(name) {
                        node = entry;
                    } else {
                        return MetricValue::Missing(format!(
                            "'{}' not found in '{}'",
                            name, selector
                        ));
                    }
                } else {
                    return MetricValue::Missing(format!(
                        "Non-map JSON '{}' at '{}' in '{}'",
                        node, name, selector
                    ));
                }
            }
            return match node {
                serde_json::Value::Bool(b) => MetricValue::Bool(*b),
                serde_json::Value::String(s) => MetricValue::String(s.to_string()),
                serde_json::Value::Number(n) => {
                    if n.is_i64() {
                        MetricValue::Int(n.as_i64().unwrap())
                    } else if n.is_u64() {
                        MetricValue::Int(n.as_u64().unwrap() as i64)
                    } else {
                        MetricValue::Float(n.as_f64().unwrap())
                    }
                }
                bad => MetricValue::Missing(format!("Bad JSON type '{}' for '{}'", bad, selector)),
            };
        }
    }
    MetricValue::Missing(format!("Inspect data '{}' not found", parts[0]))
}

#[cfg(test)]
mod test {
    use {super::*, crate::config::parse_inspect, failure::Error};

    #[test]
    fn test_fetch() -> Result<(), Error> {
        let json = r#"[{"path":"asdf/foo/qwer",
                        "contents":{"root":{"dataInt":5, "child":{"dataFloat":2.3}}}},
                        {"path":"zxcv/bar/hjkl",
                        "contents":{"base":{"dataInt":42, "array":[2,3,4], "yes": true}}}
                        ]"#;
        let inspect = parse_inspect(json.to_string())?;
        macro_rules! assert_wrong {
            ($selector:expr, $error:expr) => {
                assert_eq!(
                    fetch(&inspect, &$selector.to_string()),
                    MetricValue::Missing($error.to_string())
                )
            };
        }
        assert_wrong!("foo:root.dataInt", "Bad selector 'foo:root.dataInt'");
        assert_wrong!("foo:root:data:Int", "Bad selector 'foo:root:data:Int'");
        assert_wrong!("fow:root:dataInt", "Inspect data 'fow' not found");
        assert_wrong!("foo:root.kid:dataInt", "'kid' not found in 'foo:root.kid:dataInt'");
        assert_wrong!(
            "bar:base.array:dataInt",
            "Non-map JSON '[2,3,4]' at 'dataInt' in 'bar:base.array:dataInt'"
        );
        assert_eq!(fetch(&inspect, &"foo:root:dataInt".to_string()), MetricValue::Int(5));
        assert_eq!(
            fetch(&inspect, &"foo:root.child:dataFloat".to_string()),
            MetricValue::Float(2.3)
        );
        assert_eq!(fetch(&inspect, &"bar:base:yes".to_string()), MetricValue::Bool(true));
        Ok(())
    }
}
