// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Result},
    std::{
        collections::HashSet,
        fs::File,
        io::{BufRead, BufReader, Cursor, Read},
    },
};

/// Compares either match or they have a set of mismatch errors.
#[derive(Debug, Eq, PartialEq, Clone)]
pub enum CompareResult {
    Matches,
    Mismatch { errors: Vec<String> },
}

/// A golden file consists of lines of input that annotate expected data. Each
/// line may contain a '?' prefix which indicates an optional entry or a '#'
/// prefix which indicates the entry is a comment and should be ignored.
pub struct GoldenFile {
    path: String,
    required: HashSet<String>,
    optional: HashSet<String>,
}

impl GoldenFile {
    pub fn open(path: String) -> Result<Self> {
        let golden_file = File::open(&path).context("failed to open golden file")?;
        Self::parse(path, BufReader::new(golden_file))
    }

    pub fn from_contents(path: String, contents: Vec<u8>) -> Result<Self> {
        Self::parse(path, BufReader::new(Cursor::new(contents)))
    }

    fn parse<R: Read>(path: String, reader: BufReader<R>) -> Result<Self> {
        let mut required: HashSet<String> = HashSet::new();
        let mut optional: HashSet<String> = HashSet::new();

        for line in reader.lines() {
            let line = line?;
            match line.chars().next() {
                // Skip comment lines.
                Some('#') => {}
                // Optional entry
                Some('?') => {
                    let mut stripped = line.chars();
                    stripped.next();
                    optional.insert(String::from(stripped.as_str()));
                }
                // Normal entry
                Some(_) => {
                    required.insert(line.clone());
                }
                // Skip empty lines
                None => {}
            };
        }

        Ok(Self { path, required, optional })
    }

    /// Returns the set of differences between the golden file and the content
    /// provided. This should be an empty vector unless there is a mismatch.
    pub fn compare(&self, content: Vec<String>) -> CompareResult {
        let mut not_permitted: Vec<String> = Vec::new();
        let mut observed: Vec<String> = Vec::new();

        for line in content.iter() {
            if !self.required.contains(line) && !self.optional.contains(line) {
                not_permitted.push(line.clone());
            } else {
                observed.push(line.clone());
            }
        }

        not_permitted.sort();
        observed.sort();

        let mut errors: Vec<String> = Vec::new();
        for entry in not_permitted.iter() {
            errors.push(format!("{0} is not listed in {1} but was found in the build. If the addition to the build was intended, add a line '{0}' to {1}.", entry, self.path));
        }
        let diff: Vec<String> =
            self.required.clone().into_iter().filter(|e| !observed.contains(e)).collect();
        for entry in diff.iter() {
            errors.push(format!("{0} was declared as required in {1} but was not found in the build. If the removal from the build was intended, update {1} to remove the line '{0}'.", entry, self.path));
        }

        if errors.is_empty() {
            CompareResult::Matches
        } else {
            CompareResult::Mismatch { errors }
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        std::{fs::File, io::Write},
        tempfile::tempdir,
    };

    #[test]
    fn test_required_golden_files() {
        let golden_path = tempdir().unwrap().into_path().join("golden.txt");
        let mut golden_file = File::create(&golden_path).expect("failed to create golden");
        writeln!(golden_file, "foo").expect("failed to write");
        writeln!(golden_file, "bar").expect("failed to write");
        writeln!(golden_file, "baz").expect("failed to write");
        drop(golden_file);

        let golden = GoldenFile::open(golden_path.to_string_lossy().to_string())
            .expect("failed to open golden");
        let result = golden.compare(vec!["foo".to_string(), "bar".to_string(), "baz".to_string()]);
        assert_eq!(result, CompareResult::Matches);

        let extra_entry_result = golden.compare(vec![
            "foo".to_string(),
            "bar".to_string(),
            "baz".to_string(),
            "extra".to_string(),
        ]);
        assert_ne!(extra_entry_result, CompareResult::Matches);

        let omitted_entry_result = golden.compare(vec!["foo".to_string(), "bar".to_string()]);
        assert_ne!(omitted_entry_result, CompareResult::Matches);
    }

    #[test]
    fn test_optional_golden_files() {
        let golden_path = tempdir().unwrap().into_path().join("golden.txt");
        let mut golden_file = File::create(&golden_path).expect("failed to create golden");
        writeln!(golden_file, "foo").expect("failed to write");
        writeln!(golden_file, "bar").expect("failed to write");
        writeln!(golden_file, "?baz").expect("failed to write");
        drop(golden_file);

        let golden = GoldenFile::open(golden_path.to_string_lossy().to_string())
            .expect("failed to open golden");
        let result = golden.compare(vec!["foo".to_string(), "bar".to_string(), "baz".to_string()]);
        assert_eq!(result, CompareResult::Matches);

        let omitted_entry_result = golden.compare(vec!["foo".to_string(), "bar".to_string()]);
        assert_eq!(omitted_entry_result, CompareResult::Matches);

        let extra_entry_result = golden.compare(vec![
            "foo".to_string(),
            "bar".to_string(),
            "baz".to_string(),
            "extra".to_string(),
        ]);
        assert_ne!(extra_entry_result, CompareResult::Matches);
    }
}
