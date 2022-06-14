// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {regex::Regex, serde::Deserialize, std::collections::HashMap};

// TODO(fxbug.dev/102482): add documentation on fuchsia.dev.
#[derive(Deserialize, Debug)]
#[serde(rename_all = "camelCase")]
pub struct LogSpamList {
    log_spam: Vec<LogSpamEntry>,
}

#[derive(Deserialize, Debug)]
#[serde(rename_all = "camelCase")]
pub struct LogSpamEntry {
    file: Option<String>,
    line: Option<u64>,
    payload_regex: Option<String>,
}

// Required for implementing PartialEq trait for underlying Regex member
// See https://github.com/rust-lang/regex/issues/364
#[derive(Debug)]
struct LogSpamRegex {
    re: Regex,
}

impl PartialEq for LogSpamRegex {
    fn eq(&self, other: &LogSpamRegex) -> bool {
        self.re.as_str() == other.re.as_str()
    }
}

struct LogSpamCache {
    map: HashMap<String, HashMap<u64, Vec<LogSpamRegex>>>,
}

impl LogSpamCache {
    fn new(spam_list: LogSpamList) -> Self {
        let mut this = Self { map: HashMap::default() };
        for entry in spam_list.log_spam.iter() {
            // There are 3 types of Log Spam entries:
            //   1. Sourceless  - (regex) is provided
            //   2. SourceOnly  - (file, lineNum) are provided
            //   3. SourceRegex - (file, lineNum, regex) are provided
            //
            // Type-1 matches if file and line number do not exist, and regex matches log msg
            // Type-2 matches if file and line number match
            // Type-3 matches if file and line number match, and regex matches log msg
            match (entry.file.as_ref(), entry.line, entry.payload_regex.as_ref()) {
                (None, None, Some(payload_regex)) => {
                    // Type 1.
                    // Use ("", 0) for (file, line) for Sourceless regexes.
                    this.insert("".to_string(), 0, &payload_regex);
                }
                (Some(file), Some(line), payload_regex) => {
                    // Type 2 & Type 3.
                    // For Type 2 - SourceOnly, use "", as regex to match all strings as spam.
                    let match_all_regex = "".to_string();
                    let regex_str = payload_regex.unwrap_or(&match_all_regex);
                    this.insert(file.to_string(), line, &regex_str);
                }
                // All other combinations are considered invalid and are ignored.
                _ => (),
            }
        }
        this
    }

    fn insert(&mut self, filename: String, line_number: u64, payload_regex: &str) {
        let re_result = Regex::new(payload_regex);
        match re_result {
            Ok(re) => {
                let logspam_regex = LogSpamRegex { re };
                self.map
                    .entry(filename)
                    .or_insert(HashMap::new())
                    .entry(line_number)
                    .or_insert(Vec::new())
                    .push(logspam_regex);
            }
            Err(e) => eprintln!("got an error parsing regex {}: {:?}", payload_regex, e),
        }
    }

    fn get_spam_regex_list(
        &self,
        file: Option<&String>,
        line: Option<u64>,
    ) -> Option<&Vec<LogSpamRegex>> {
        let default_file = "".to_string();
        let file = file.unwrap_or(&default_file);
        let line = line.unwrap_or_default();

        let line_num_to_regex_vector_map = self.map.get(file)?;
        let regex_vector = line_num_to_regex_vector_map.get(&line)?;
        Some(regex_vector)
    }
}

pub struct LogSpamFilterImpl {
    cache: LogSpamCache,
}

impl LogSpamFilterImpl {
    pub fn new(spam_list: LogSpamList) -> Self {
        Self { cache: LogSpamCache::new(spam_list) }
    }
}

pub trait LogSpamFilter {
    fn is_spam(&self, file: Option<&String>, line: Option<u64>, msg: &str) -> bool;
}

impl LogSpamFilter for LogSpamFilterImpl {
    fn is_spam(&self, file: Option<&String>, line: Option<u64>, msg: &str) -> bool {
        let regex_list = self.cache.get_spam_regex_list(file, line);
        match regex_list {
            Some(l) => l.iter().any(|r| r.re.is_match(msg)),
            None => false,
        }
    }
}

#[cfg(test)]
mod test {
    use super::*;

    #[fuchsia::test]
    async fn test_spam_filter_new() {
        let spam_list = LogSpamList {
            log_spam: vec![
                // sourceRegex - included
                LogSpamEntry {
                    file: Some("file_a".to_string()),
                    line: Some(1),
                    payload_regex: Some("regex_a".to_string()),
                },
                LogSpamEntry {
                    file: Some("file_a".to_string()),
                    line: Some(1),
                    payload_regex: Some("regex_b".to_string()),
                },
                // sourceless - included
                LogSpamEntry {
                    file: None,
                    line: None,
                    payload_regex: Some("sourceless_regex".to_string()),
                },
                // sourceOnly - included
                LogSpamEntry {
                    file: Some("file_b".to_string()),
                    line: Some(1),
                    payload_regex: None,
                },
                // file exists but not line - ignored
                LogSpamEntry {
                    file: Some("file_a".to_string()),
                    line: None,
                    payload_regex: Some("ignored".to_string()),
                },
                // line exists but not file - ignored
                LogSpamEntry {
                    file: None,
                    line: Some(1),
                    payload_regex: Some("ignored".to_string()),
                },
                // invalid regex - ignored
                LogSpamEntry { file: None, line: Some(1), payload_regex: Some("[".to_string()) },
            ],
        };

        let want_cache = HashMap::from([
            (
                "file_a".to_string(),
                HashMap::from([(
                    1,
                    vec![
                        LogSpamRegex { re: Regex::new("regex_a").unwrap() },
                        LogSpamRegex { re: Regex::new("regex_b").unwrap() },
                    ],
                )]),
            ),
            (
                "file_b".to_string(),
                HashMap::from([(1, vec![LogSpamRegex { re: Regex::new("").unwrap() }])]),
            ),
            (
                "".to_string(),
                HashMap::from([(
                    0,
                    vec![LogSpamRegex { re: Regex::new("sourceless_regex").unwrap() }],
                )]),
            ),
        ]);

        let spam_filter = LogSpamFilterImpl::new(spam_list);
        assert_eq!(spam_filter.cache.map, want_cache);
    }

    #[fuchsia::test]
    async fn test_spam_filter_is_spam() {
        let spam_list = LogSpamList {
            log_spam: vec![
                // sourceRegex
                LogSpamEntry {
                    file: Some("file_a".to_string()),
                    line: Some(1),
                    payload_regex: Some("regex_a".to_string()),
                },
                LogSpamEntry {
                    file: Some("file_a".to_string()),
                    line: Some(1),
                    payload_regex: Some("regex_b".to_string()),
                },
                // sourceless
                LogSpamEntry {
                    file: None,
                    line: None,
                    payload_regex: Some("sourceless_regex".to_string()),
                },
                // sourceOnly
                LogSpamEntry {
                    file: Some("file_b".to_string()),
                    line: Some(1),
                    payload_regex: None,
                },
            ],
        };

        let filter = LogSpamFilterImpl::new(spam_list);

        assert!(filter.is_spam(Some("file_a".to_string()).as_ref(), Some(1), "regex_a"));
        assert!(filter.is_spam(Some("file_a".to_string()).as_ref(), Some(1), "regex_b"));
        assert!(filter.is_spam(Some("file_b".to_string()).as_ref(), Some(1), "arbitrary string"));
        assert!(filter.is_spam(None, None, "sourceless_regex"));
    }
}
