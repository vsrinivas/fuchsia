// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{anyhow, Error};
use rustfix::{Filter, Suggestion};

use std::{
    collections::{HashMap, HashSet},
    fs,
    io::BufRead,
};

pub fn fix<R: BufRead>(lints: &mut R, filter: &[String], dryrun: bool) -> Result<(), Error> {
    let mut all_lints = String::new();
    lints.read_to_string(&mut all_lints)?;
    let categories = crate::lint::get_categories();
    // If a lint category is given, add all lints in that category to the filter
    let mut filter_lints: HashSet<String> = HashSet::new();
    for f in filter {
        if let Some(lints) = categories.get(f) {
            filter_lints.extend(lints.iter().cloned());
        } else {
            filter_lints.insert(f.to_owned());
        }
    }

    let suggestions = rustfix::get_suggestions_from_json(
        &all_lints,
        &filter_lints,
        Filter::MachineApplicableOnly,
    )?;
    if suggestions.is_empty() {
        return Err(anyhow!("Couldn't find any fixable occurances of those lints"));
    }

    let mut source_files: HashMap<String, Vec<Suggestion>> = Default::default();
    for suggestion in suggestions {
        // there should be only one file per suggestion
        debug_assert_eq!(
            suggestion
                .solutions
                .iter()
                .flat_map(|s| s.replacements.iter().map(|r| r.snippet.file_name.clone()))
                .collect::<HashSet::<_>>()
                .len(),
            1,
        );
        let file = suggestion.solutions[0].replacements[0].snippet.file_name.clone();
        source_files.entry(file).or_insert_with(Vec::new).push(suggestion);
    }

    for (source_file, suggestions) in &source_files {
        let source = fs::read_to_string(source_file)?;
        let mut fix = rustfix::CodeFix::new(&source);
        for suggestion in suggestions.iter().rev() {
            if let Err(e) = fix.apply(suggestion) {
                eprintln!("Failed to apply suggestion to {}: {}", source_file, e);
            }
        }
        let fixes = fix.finish()?;
        println!("{} fixes in {}", suggestions.len(), source_file);
        if !dryrun {
            fs::write(source_file, fixes)?;
        }
    }
    Ok(())
}
