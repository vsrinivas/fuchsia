// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{bail, Error},
    serde_json,
    std::{collections::HashMap, fs, path::Path},
    triage_lib::{ActionTagDirective, DiagnosticData, ParseResult, Source},
};

// Third field is whether the file is required.
const SNAPSHOT_FILES: [(&str, Source, bool); 5] = [
    ("inspect.json", Source::Inspect, true),
    ("log.kernel.txt", Source::Klog, false),
    ("log.system.txt", Source::Syslog, false),
    ("log.system.previous_boot.txt", Source::Bootlog, false),
    ("annotations.json", Source::Annotations, false),
];

pub fn diagnostics_from_directory(directory: &Path) -> Result<Vec<DiagnosticData>, Error> {
    SNAPSHOT_FILES
        .iter()
        .map(|(name, source, required)| diagnostic_from_file(directory, name, source, required))
        .collect::<Result<Vec<_>, _>>()
}

fn diagnostic_from_file(
    directory: &Path,
    file_name: &str,
    source: &Source,
    required: &bool,
) -> Result<DiagnosticData, Error> {
    let file_path = directory.join(file_name).into_os_string().to_string_lossy().to_string();
    let contents = match fs::read_to_string(&file_path) {
        Ok(contents) => contents,
        Err(_) => {
            if *required {
                bail!("Couldn't read file '{}' to string", file_path);
            } else {
                return Ok(DiagnosticData::new_empty(file_path, *source));
            }
        }
    };
    DiagnosticData::new(file_path, *source, contents)
}

pub fn config_from_files<T: AsRef<Path>>(
    config_files: &[T],
    action_tag_directive_from_tags: &ActionTagDirective,
) -> Result<ParseResult, Error> {
    let mut config_file_map: HashMap<String, String> = HashMap::new();
    for file_name in config_files {
        let file_name = file_name.as_ref();
        let file_data = match fs::read_to_string(file_name) {
            Ok(data) => data,
            Err(e) => {
                bail!("Couldn't read config file '{}' to string, {}", file_name.display(), e);
            }
        };

        match serde_json::from_str::<serde_json::Value>(&file_data) {
            // This file looks like a bundle.
            // Bundles are JSON with a "files" key containing name => content
            // mappings.
            Ok(serde_json::Value::Object(file_object)) if file_object.contains_key("files") => {
                match file_object.get("files").unwrap() {
                    serde_json::Value::Object(files) => {
                        for (name, content) in files {
                            match content {
                                serde_json::Value::String(content) => {
                                    config_file_map.insert(name.to_string(), content.to_string());
                                }
                                _ => {
                                    bail!("File {} looks like a bundle, but key {} must contain a string.", file_name.display(), name);
                                }
                            }
                        }
                    }
                    _ => {
                        bail!(
                            "File {} looks like a bundle, but key 'files' is not an object.",
                            file_name.display()
                        );
                    }
                }
            }
            // The file does not look like a bundle.
            // Get the base name of the file as the namesapce, and add it to the config file set.
            _ => {
                let namespace = base_name(file_name)?;
                config_file_map.insert(namespace, file_data);
            }
        }
    }

    ParseResult::new(&config_file_map, &action_tag_directive_from_tags)
}

fn base_name(path: &Path) -> Result<String, Error> {
    if let Some(s) = path.file_stem() {
        if let Some(s) = s.to_str() {
            return Ok(s.to_owned());
        }
    }
    bail!("Bad path {} - can't find file_stem", path.display())
}

#[cfg(test)]
mod test {
    use {super::*, anyhow::Error};

    #[fuchsia::test]
    fn base_name_works() -> Result<(), Error> {
        assert_eq!(base_name(Path::new("foo/bar/baz.ext"))?, "baz".to_string());
        Ok(())
    }
}
