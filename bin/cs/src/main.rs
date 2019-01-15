// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::{err_msg, Error};
use std::fmt;
use std::fs;
use std::path::Path;

type ComponentsResult = Result<Vec<Component>, Error>;
type RealmsResult = Result<Vec<Realm>, Error>;
type TraversalResult = Result<(), Error>;
type DirEntryResult = Result<fs::DirEntry, Error>;

struct Realm {
    job_id: u32,
    name: String,
    child_realms: Vec<Realm>,
    child_components: Vec<Component>,
}

struct Component {
    job_id: u32,
    name: String,
    url: String,
    child_components: Vec<Component>,
}

impl Realm {
    fn create(realm_path: &Path) -> Result<Realm, Error> {
        let job_id = fs::read_to_string(&realm_path.join("job-id"))?;
        let name = fs::read_to_string(&realm_path.join("name"))?;
        let realm = Realm {
            job_id: job_id.parse::<u32>()?,
            name: name,
            child_realms: visit_child_realms(&realm_path)?,
            child_components: visit_child_components(&realm_path)?,
        };
        Ok(realm)
    }

    fn write_indented(&self, f: &mut fmt::Formatter, indent: usize) -> fmt::Result {
        writeln!(
            f,
            "{}Realm[{}]: {}",
            " ".repeat(indent),
            self.job_id,
            self.name,
        )?;

        for comp in &self.child_components {
            comp.write_indented(f, indent + 2)?;
        }

        for realm in &self.child_realms {
            realm.write_indented(f, indent + 2)?;
        }

        Ok(())
    }
}

impl fmt::Display for Realm {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        self.write_indented(f, 0)
    }
}

impl Component {
    fn create(component_path: &Path) -> Result<Component, Error> {
        let job_id = fs::read_to_string(&component_path.join("job-id"))?;
        let url = fs::read_to_string(&component_path.join("url"))?;
        let name = fs::read_to_string(&component_path.join("name"))?;
        let component = Component {
            job_id: job_id.parse::<u32>()?,
            name: name,
            url: url,
            child_components: visit_child_components(&component_path)?,
        };
        Ok(component)
    }

    fn write_indented(&self, f: &mut fmt::Formatter, indent: usize) -> fmt::Result {
        writeln!(
            f,
            "{}{}[{}]: {}",
            " ".repeat(indent),
            self.name,
            self.job_id,
            self.url,
        )?;

        for child in &self.child_components {
            child.write_indented(f, indent + 2)?;
        }

        Ok(())
    }
}

impl fmt::Display for Component {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        self.write_indented(f, 0)
    }
}

fn visit_child_realms(realm_path: &Path) -> RealmsResult {
    let child_realms_path = realm_path.join("r");
    let entries = fs::read_dir(child_realms_path)?;
    let mut child_realms: Vec<Realm> = Vec::new();

    // visit all entries within <realm id>/r/
    for entry in entries {
        let entry = entry?;
        // visit <realm id>/r/<child realm name>/<child realm id>/
        let child_realm_id_dir_entry = find_id_directory(&entry.path())?;
        let path = child_realm_id_dir_entry.path();
        child_realms.push(Realm::create(&path)?);
    }
    Ok(child_realms)
}

/// Used as a helper function to traverse <realm id>/r/, <realm id>/c/,
/// <component instance id>/c/, following through into their id subdirectories.
fn find_id_directory(dir: &Path) -> DirEntryResult {
    let entries = fs::read_dir(dir)?;
    for entry in entries {
        let entry = entry?;
        let path = entry.path();
        let id = {
            let name = path.file_name().ok_or_else(|| err_msg("no filename"))?;
            name.to_string_lossy()
        };

        // Find the first numeric directory name and return its directory entry.
        if id.chars().all(char::is_numeric) {
            return Ok(entry);
        }
    }
    return Err(err_msg("Directory not found"));
}

/// Traverses a directory of named components, and recurses into each component directory.
/// Each component visited is added to the |child_components| vector.
fn visit_child_components(parent_path: &Path) -> ComponentsResult {
    let child_components_path = parent_path.join("c");
    if !child_components_path.is_dir() {
        return Ok(vec![]);
    }

    let mut child_components: Vec<Component> = Vec::new();
    let entries = fs::read_dir(&child_components_path)?;
    for entry in entries {
        let entry = entry?;
        // Visits */c/<component name>/<component instance id>.
        let component_instance_id_dir_entry = find_id_directory(&entry.path())?;
        let path = component_instance_id_dir_entry.path();
        child_components.push(Component::create(&path)?);
    }
    Ok(child_components)
}

fn main() -> TraversalResult {
    // Visit the directory /hub and recursively traverse it, outputting
    // information about the component hierarchy.
    // See https://fuchsia.googlesource.com/docs/+/master/the-book/hub.md for
    // more information on the Hub directory structure.
    let root_realm = Realm::create(&Path::new("/hub"))?;
    println!("{}", root_realm);
    Ok(())
}
