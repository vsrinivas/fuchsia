// Copyright 2019 The Fuchsia Authors. All rights reserved.
// // Use of this source code is governed by a BSD-style license that can be
// // found in the LICENSE file.

#![allow(dead_code)]
use failure::{format_err, Error};
//use termion;
use difference::{
    self,
    Difference::{Add, Rem, Same},
};
use selectors;
use std::io::{stdin, stdout, Write};
use structopt::StructOpt;
use termion::cursor;
use termion::event::{Event, Key};
use termion::input::TermRead;
use termion::raw::IntoRawMode;

use fidl_fuchsia_diagnostics::Selector;
use regex::Regex;
use std::cmp::{max, min};
use std::fs::read_to_string;
use std::path::PathBuf;
use std::sync::Arc;

use lazy_static::lazy_static;

#[allow(unused_imports)]
use hoist;

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
        #[structopt(short, help = "Generate selectors for only this component")]
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

fn escape_path_segment(segment: &str) -> String {
    lazy_static! {
        static ref REGEX: Regex = Regex::new("([:/])").unwrap();
    }

    REGEX.replace_all(segment, r"\$1").into_owned()
}

fn join_and_escape_path(path: &Vec<impl AsRef<str>>) -> String {
    path.iter().map(|v| escape_path_segment(v.as_ref())).collect::<Vec<_>>().join("/")
}

fn filter_data_to_lines(selector_file: &str, data: &serde_json::Value) -> Result<Vec<Line>, Error> {
    let mut filtered_data = data.clone();

    let selector_vec: Vec<_> = selectors::parse_selector_file(&PathBuf::from(selector_file))?
        .into_iter()
        .map(|s| Arc::new(s))
        .collect();

    fn inner_filter_object(
        data: serde_json::Value,
        _selectors: &Vec<Arc<Selector>>,
        _path: &str,
    ) -> serde_json::Value {
        data
    }

    if let serde_json::Value::Array(arr) = filtered_data {
        filtered_data = serde_json::Value::Array(
            arr.into_iter()
                .filter_map(|mut value| {
                    if let serde_json::Value::Object(map) = &mut value {
                        if let Some(serde_json::Value::String(val)) = map.get("path") {
                            let selectors = selectors::match_component_moniker_against_selectors(
                                &parse_path_to_moniker(&val),
                                &selector_vec,
                            )
                            .unwrap();

                            if selectors.len() > 0 {
                                let contents =
                                    map.remove("contents").unwrap_or(serde_json::Value::Null);
                                map.insert(
                                    "contents".to_string(),
                                    inner_filter_object(contents, &selectors, ""),
                                );
                                return Some(value);
                            } else {
                                return None;
                            }
                        }
                    }
                    None
                })
                .collect(),
        );

        //    if let serde_json::Value::Object(map) = &mut value {
    }

    let orig_str = serde_json::to_string_pretty(&data).unwrap();
    let new_str = serde_json::to_string_pretty(&filtered_data).unwrap();

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

    struct MatchedComponent<'a> {
        moniker: Vec<String>,
        contents: &'a serde_json::Value,
    }

    let matching_hierarchies: Vec<_> = arr
        .iter()
        .filter_map(|value| {
            let moniker = parse_path_to_moniker(value["path"].as_str().unwrap_or(""));
            match (component_name.as_ref(), moniker.last()) {
                // User wanted only matching components.
                (Some(expected), Some(actual)) => {
                    if expected == actual {
                        Some(MatchedComponent { moniker, contents: &value["contents"] })
                    } else {
                        None
                    }
                }
                // User wanted any components.
                (_, Some(_)) => Some(MatchedComponent { moniker, contents: &value["contents"] }),
                // No component was found.
                _ => None,
            }
        })
        .collect();

    let mut output: Vec<String> = vec![];

    for component in &matching_hierarchies {
        // Work consists of tuples of (path, Value).
        let mut work_stack = vec![(Vec::<&str>::new(), component.contents)];
        while let Some(maybe_obj) = work_stack.pop() {
            if let (path, Some(obj)) = (maybe_obj.0, maybe_obj.1.as_object()) {
                for (name, val) in obj.iter() {
                    if val.is_object() {
                        let mut p2 = path.clone();
                        p2.push(name);
                        work_stack.push((p2, val));
                    } else if !val.is_null() {
                        output.push(
                            format!(
                                "{}:{}:{}",
                                join_and_escape_path(&component.moniker),
                                join_and_escape_path(&path),
                                escape_path_segment(&name),
                            )
                            .to_owned(),
                        );
                    }
                }
            }
        }
    }

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
    fn join_and_escape_path_test() {
        assert_eq!(join_and_escape_path(&vec!["abcd"]), r"abcd");
        assert_eq!(
            join_and_escape_path(&vec!["sys", "test", "ab:cd:ef", "ab/cd/ef"]),
            r"sys/test/ab\:cd\:ef/ab\/cd\/ef"
        );
    }
}
