// Copyright 2019 The Fuchsia Authors. All rights reserved.
// // Use of this source code is governed by a BSD-style license that can be
// // found in the LICENSE file.
use {
    anyhow::{format_err, Error},
    difference::{
        self,
        Difference::{Add, Rem, Same},
    },
    fidl_fuchsia_diagnostics::Selector,
    fuchsia_inspect_node_hierarchy::{
        self,
        serialization::{
            json::{JsonNodeHierarchySerializer, RawJsonNodeHierarchySerializer},
            HierarchyDeserializer, HierarchySerializer,
        },
        InspectHierarchyMatcher, NodeHierarchy,
    },
    regex::Regex,
    selectors,
    std::cmp::{max, min},
    std::convert::TryInto,
    std::fs::read_to_string,
    std::io::{stdin, stdout, Write},
    std::path::PathBuf,
    std::sync::Arc,
    structopt::StructOpt,
    termion::{
        cursor,
        event::{Event, Key},
        input::TermRead,
        raw::IntoRawMode,
    },
};

static JSON_MONIKER_KEY: &str = "path";
static JSON_PAYLOAD_KEY: &str = "contents";

#[derive(Debug, StructOpt)]
struct Options {
    #[structopt(short, long, help = "Inspect JSON file to read")]
    bugreport: String,

    #[structopt(subcommand)]
    command: Command,
}

#[derive(Debug, StructOpt)]
enum Command {
    #[structopt(name = "generate")]
    Generate {
        #[structopt(
            short,
            name = "component",
            help = "Generate selectors for only this component"
        )]
        component_name: Option<String>,
        #[structopt(help = "The output file to generate")]
        selector_file: String,
    },
    #[structopt(name = "apply")]
    Apply {
        #[structopt(help = "The selector file to apply to the bugreport")]
        selector_file: String,
    },
}

#[derive(Debug)]
struct Line {
    value: String,
    removed: bool,
}

impl Line {
    fn new(s: impl ToString) -> Self {
        Self { value: s.to_string(), removed: false }
    }

    fn removed(s: impl ToString) -> Self {
        Self { value: s.to_string(), removed: true }
    }

    fn len(&self) -> usize {
        self.value.len()
    }
}

struct Output {
    lines: Vec<Line>,
    offset_top: usize,
    offset_left: usize,
    max_line_len: usize,

    filter_removed: bool,
}

impl Output {
    fn new(lines: Vec<Line>) -> Self {
        let max_line_len = lines.iter().map(|l| l.len()).max().unwrap_or(0);
        Output { lines, offset_top: 0, offset_left: 0, max_line_len, filter_removed: false }
    }

    fn set_lines(&mut self, lines: Vec<Line>) {
        self.max_line_len = lines.iter().map(|l| l.len()).max().unwrap_or(0);
        self.lines = lines;
        self.scroll(0, 0);
    }

    fn refresh(&self, stdout: &mut impl Write) {
        let (w, h) = termion::terminal_size().unwrap();
        let max_lines = h as usize - 2; // Leave 2 lines for info.

        self.lines
            .iter()
            .filter(|l| !self.filter_removed || !l.removed)
            .skip(self.offset_top)
            .take(max_lines)
            .enumerate()
            .for_each(|(i, line)| {
                if self.offset_left >= line.value.len() {
                    return;
                }
                let end = min(line.value.len(), self.offset_left + w as usize);

                if line.removed {
                    write!(stdout, "{}", termion::color::Fg(termion::color::Red)).unwrap();
                }
                write!(
                    stdout,
                    "{}{}{}",
                    termion::cursor::Goto(1, (i + 1) as u16),
                    line.value[self.offset_left..end].to_string(),
                    termion::color::Fg(termion::color::Reset),
                )
                .unwrap();
            });

        write!(
            stdout,
            "{}------------------- T: {}/{}, L: {}/{}{}Controls: [Q]uit. [R]efresh. {} filtered data. Arrow keys scroll.",
            termion::cursor::Goto(1, h - 1),
            self.offset_top, self.visible_line_count(), self.offset_left, self.max_line_len,
            termion::cursor::Goto(1, h),
            if self.filter_removed { "S[h]ow" } else { "[H]ide" },
        )
        .unwrap();
    }

    fn visible_line_count(&self) -> usize {
        self.lines.iter().filter(|l| !self.filter_removed || !l.removed).count()
    }

    fn scroll(&mut self, down: i64, right: i64) {
        let (w, h) = termion::terminal_size().unwrap();
        self.offset_top = max(0, self.offset_top as i64 + down) as usize;
        self.offset_left = max(0, self.offset_left as i64 + right) as usize;
        self.offset_top =
            min(self.offset_top as i64, max(0, self.visible_line_count() as i64 - h as i64))
                as usize;
        self.offset_left =
            min(self.offset_left as i64, max(0, self.max_line_len as i64 - w as i64)) as usize;
    }

    fn set_filter_removed(&mut self, val: bool) {
        if self.filter_removed == val {
            return;
        }

        self.filter_removed = val;

        if self.filter_removed {
            // Starting to filter, tweak offset_top to remove offsets from newly filtered lines.
            self.offset_top -= self.lines.iter().take(self.offset_top).filter(|l| l.removed).count()
        } else {
            // TODO: Fix this
        }
    }
}

fn parse_path_to_moniker(path: &str) -> Vec<String> {
    Regex::new(r"/[cr]/([^/]*)/\d+")
        .unwrap()
        .captures_iter(path)
        .map(|cap| cap.get(1).unwrap().as_str().to_owned())
        .collect()
}

fn filter_json_schema_by_selectors(
    mut value: serde_json::Value,
    selector_vec: &Vec<Arc<Selector>>,
) -> Option<serde_json::Value> {
    let moniker_string_opt = value[JSON_MONIKER_KEY].as_str();
    let deserialized_hierarchy =
        RawJsonNodeHierarchySerializer::deserialize(value[JSON_PAYLOAD_KEY].clone());

    match (moniker_string_opt, deserialized_hierarchy) {
        (Some(moniker_path), Ok(hierarchy)) => {
            let moniker = parse_path_to_moniker(moniker_path);

            match selectors::match_component_moniker_against_selectors(&moniker, &selector_vec) {
                Ok(matched_selectors) => {
                    if matched_selectors.is_empty() {
                        return None;
                    }

                    let inspect_matcher: InspectHierarchyMatcher =
                        (&matched_selectors).try_into().unwrap();

                    match fuchsia_inspect_node_hierarchy::filter_inspect_snapshot(
                        hierarchy,
                        &inspect_matcher,
                    ) {
                        Ok(Some(filtered)) => {
                            let serialized_hierarchy =
                                RawJsonNodeHierarchySerializer::serialize(filtered);
                            value[JSON_PAYLOAD_KEY] = serialized_hierarchy;
                            Some(value)
                        }
                        Ok(None) => {
                            // Ok(None) implies the tree was fully filtered. This means that
                            // it genuinely should not be included in the output.
                            None
                        }
                        Err(e) => {
                            value[JSON_PAYLOAD_KEY] = serde_json::json!(format!(
                                "Filtering the hierarchy of {}, an error occurred: {:?}",
                                moniker_path, e
                            ));
                            Some(value)
                        }
                    }
                }
                Err(e) => {
                    value[JSON_PAYLOAD_KEY] = serde_json::json!(format!(
                        "Evaulating selectors for {} met an unexpected error condition: {:?}",
                        moniker_path, e
                    ));
                    Some(value)
                }
            }
        }
        (potential_errorful_moniker, potential_errorful_hierarchy) => {
            let mut errorful_report = String::new();
            if potential_errorful_moniker.is_none() {
                errorful_report.push_str(
                    "The moniker entry in the provided schema was missing or an incorrect type. \n",
                );
            }

            if potential_errorful_hierarchy.is_err() {
                errorful_report.push_str(&format!(
                    "The hierarchy entry was missing or failed to deserialize: {:?}",
                    potential_errorful_hierarchy
                        .err()
                        .expect("We've already verified that the deserialization failed.")
                ))
            }
            value[JSON_PAYLOAD_KEY] = serde_json::json!(errorful_report);
            Some(value)
        }
    }
}

/// Consumes a file containing Inspect selectors and applies them to an array of node hierarchies
/// which had previously been serialized to their json schema.
///
/// Returns a vector of Line printed diffs between the unfiltered and filtered hierarchies,
/// or an Error.
fn filter_data_to_lines(selector_file: &str, data: &serde_json::Value) -> Result<Vec<Line>, Error> {
    let selector_vec: Vec<Arc<Selector>> =
        selectors::parse_selector_file(&PathBuf::from(selector_file))?
            .into_iter()
            .map(Arc::new)
            .collect();

    let arr: Vec<serde_json::Value> = match data {
        serde_json::Value::Array(arr) => arr.clone(),
        _ => return Err(format_err!("Input Inspect JSON must be an array.")),
    };

    let filtered_node_hierarchies: Vec<serde_json::Value> = arr
        .into_iter()
        .filter_map(|value| filter_json_schema_by_selectors(value, &selector_vec))
        .collect();

    // TODO(43937): Move inspect formatting utilities to the hierarchy library.
    let filtered_collection_array = serde_json::Value::Array(filtered_node_hierarchies);

    let orig_str = serde_json::to_string_pretty(&data).unwrap();
    let new_str = serde_json::to_string_pretty(&filtered_collection_array).unwrap();

    let cs = difference::Changeset::new(&orig_str, &new_str, "\n");

    Ok(cs
        .diffs
        .into_iter()
        .map(|change| match change {
            Same(val) | Add(val) => val.split("\n").map(|l| Line::new(l)).collect::<Vec<Line>>(),
            Rem(val) => val.split("\n").map(|l| Line::removed(l)).collect::<Vec<Line>>(),
        })
        .flatten()
        .collect())
}

fn generate_selectors<'a>(
    data: &'a serde_json::Value,
    component_name: Option<String>,
) -> Result<String, Error> {
    let arr = match data {
        serde_json::Value::Array(arr) => arr,
        _ => return Err(format_err!("Input Inspect JSON must be an array.")),
    };

    struct MatchedHierarchy {
        moniker: Vec<String>,
        hierarchy: NodeHierarchy,
    }

    let matching_hierarchies: Vec<MatchedHierarchy> = arr
        .iter()
        .filter_map(|value| {
            let moniker = parse_path_to_moniker(value[JSON_MONIKER_KEY].as_str().unwrap_or(""));

            let component_name_matches = component_name.is_none()
                || component_name.as_ref().unwrap()
                    == moniker
                        .last()
                        .expect("Monikers in provided data dumps are required to be non-empty.");

            if component_name_matches {
                let hierarchy =
                    JsonNodeHierarchySerializer::deserialize(value[JSON_PAYLOAD_KEY].to_string())
                        .unwrap();
                Some(MatchedHierarchy { moniker, hierarchy: hierarchy })
            } else {
                None
            }
        })
        .collect();

    let mut output: Vec<String> = vec![];

    for matching_hierarchy in matching_hierarchies {
        let sanitized_moniker = matching_hierarchy
            .moniker
            .iter()
            .map(|s| selectors::sanitize_string_for_selectors(s))
            .collect::<Vec<String>>()
            .join("/");

        for (node_path, property) in matching_hierarchy.hierarchy.property_iter() {
            let formatted_node_path = node_path
                .iter()
                .map(|s| selectors::sanitize_string_for_selectors(s))
                .collect::<Vec<String>>()
                .join("/");
            let sanitized_property = selectors::sanitize_string_for_selectors(property.name());
            output.push(format!(
                "{}:{}:{}",
                sanitized_moniker.clone(),
                formatted_node_path,
                sanitized_property
            ));
        }
    }

    // NodeHierarchy has an intentionally non-deterministic iteration order, but for client
    // facing tools we'll want to sort the outputs.
    output.sort();

    Ok(output.join("\n"))
}

fn interactive_apply(data: &serde_json::Value, selector_file: &str) -> Result<(), Error> {
    let stdin = stdin();
    let mut stdout = stdout().into_raw_mode().unwrap();

    let mut output = Output::new(filter_data_to_lines(&selector_file, &data)?);

    write!(stdout, "{}{}{}", cursor::Restore, cursor::Hide, termion::clear::All).unwrap();

    output.refresh(&mut stdout);

    stdout.flush().unwrap();

    for c in stdin.events() {
        let evt = c.unwrap();
        match evt {
            Event::Key(Key::Char('q')) => break,
            Event::Key(Key::Char('h')) => output.set_filter_removed(!output.filter_removed),
            Event::Key(Key::Char('r')) => {
                output.set_lines(vec![Line::new("Refreshing filtered hierarchies...")]);
                write!(stdout, "{}", termion::clear::All).unwrap();
                output.refresh(&mut stdout);
                stdout.flush().unwrap();

                output.set_lines(filter_data_to_lines(&selector_file, &data)?)
            }
            Event::Key(Key::Up) => {
                output.scroll(-1, 0);
            }
            Event::Key(Key::Down) => {
                output.scroll(1, 0);
            }
            Event::Key(Key::Left) => {
                output.scroll(0, -1);
            }
            Event::Key(Key::Right) => {
                output.scroll(0, 1);
            }
            e => {
                println!("{:?}", e);
            }
        }
        write!(stdout, "{}", termion::clear::All).unwrap();
        output.refresh(&mut stdout);
        stdout.flush().unwrap();
    }

    write!(stdout, "{}{}{}", cursor::Restore, cursor::Show, termion::clear::All,).unwrap();
    stdout.flush().unwrap();

    Ok(())
}

fn main() -> Result<(), Error> {
    let opts = Options::from_args();

    let filename = &opts.bugreport;

    let data: serde_json::Value = serde_json::from_str(
        &read_to_string(filename).expect(&format!("Failed to read {} ", filename)),
    )
    .expect(&format!("Failed to parse {} as JSON", filename));

    match opts.command {
        Command::Generate { selector_file, component_name } => {
            std::fs::write(
                &selector_file,
                generate_selectors(&data, component_name)
                    .expect(&format!("failed to generate selectors")),
            )?;
        }
        Command::Apply { selector_file } => {
            interactive_apply(&data, &selector_file)?;
        }
    }

    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use tempfile;

    #[test]
    fn parse_path_to_moniker_test() {
        assert_eq!(
            parse_path_to_moniker("/hub/r/sys/1234/c/my.cmx/1/out/diagnostics"),
            vec!["sys", "my.cmx"]
        );
        assert_eq!(
            parse_path_to_moniker("/hub/r/12345/28464/c/account_handler.cmx/29647/out/diagnostics"),
            vec!["12345", "account_handler.cmx"]
        );
    }

    #[test]
    fn generate_selectors_test() {
        let json_dump = get_json_dump();

        eprintln!("json dump: {}", json_dump);
        let named_selector_string =
            generate_selectors(&json_dump, Some("account_manager.cmx".to_string()))
                .expect("Generating selectors with matching name should succeed.");

        let expected_named_selector_string = "account_manager.cmx:root/accounts:active
account_manager.cmx:root/accounts:total
account_manager.cmx:root/auth_providers:types
account_manager.cmx:root/listeners:active
account_manager.cmx:root/listeners:events
account_manager.cmx:root/listeners:total_opened";

        assert_eq!(named_selector_string, expected_named_selector_string);

        assert_eq!(
            generate_selectors(&json_dump, Some("bloop.cmx".to_string()))
                .expect("Generating selectors with unmatching name should succeed"),
            ""
        );

        assert_eq!(
            generate_selectors(&json_dump, None)
                .expect("Generating selectors with no name should succeed"),
            expected_named_selector_string
        );
    }

    fn setup_and_run_selector_filtering(
        selector_string: &str,
        source_hierarchy: serde_json::Value,
        golden_json: serde_json::Value,
    ) {
        let mut selector_path =
            tempfile::NamedTempFile::new().expect("Creating tmp selector file should succeed.");

        selector_path
            .write_all(selector_string.as_bytes())
            .expect("writing selectors to file should be fine...");

        let filtered_data_string =
            filter_data_to_lines(&selector_path.path().to_string_lossy(), &source_hierarchy)
                .expect("filtering hierarchy should have succeeded.")
                .into_iter()
                .filter(|line| !line.removed)
                .fold(String::new(), |mut acc, line| {
                    acc.push_str(&line.value);
                    acc
                });
        let filtered_json_value: serde_json::Value = serde_json::from_str(&filtered_data_string)
            .expect(&format!(
                "Resultant json dump should be parsable json: {}",
                filtered_data_string
            ));

        assert_eq!(filtered_json_value, golden_json);
    }

    #[test]
    fn filter_data_to_lines_test() {
        let full_tree_selector = "account_manager.cmx:root/accounts:active
account_manager.cmx:root/accounts:total
account_manager.cmx:root/auth_providers:types
account_manager.cmx:root/listeners:active
account_manager.cmx:root/listeners:events
account_manager.cmx:root/listeners:total_opened";

        setup_and_run_selector_filtering(full_tree_selector, get_json_dump(), get_json_dump());

        let single_value_selector = "account_manager.cmx:root/accounts:active";

        setup_and_run_selector_filtering(
            single_value_selector,
            get_json_dump(),
            get_single_value_json(),
        );
    }

    fn get_json_dump() -> serde_json::Value {
        serde_json::json!(
            [
                {
                    "contents": {
                        "root": {
                            "accounts": {
                                "active": 0,
                                "total": 0
                            },
                            "auth_providers": {
                                "types": "google"
                            },
                            "listeners": {
                                "active": 1,
                                "events": 0,
                                "total_opened": 1
                            }
                        }
                    },
                    "path": "/hub/c/account_manager.cmx/25181/out/diagnostics/root.inspect"
                }
            ]
        )
    }

    fn get_single_value_json() -> serde_json::Value {
        serde_json::json!(
            [
                {
                    "contents": {
                        "root": {
                            "accounts": {
                                "active": 0
                            }
                        }
                    },
                    "path": "/hub/c/account_manager.cmx/25181/out/diagnostics/root.inspect"
                }
            ]
        )
    }
}
