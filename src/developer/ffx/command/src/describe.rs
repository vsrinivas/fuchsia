// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Utilities for describing subcommands. The following code is derived directly from
/// and intended to follow the format of argh's command descriptions.

// The following code might be a little unusual for our coding styles in fuchsia because
// it's been deliberately kept similar to the original code in argh_shared.
pub const INDENT: &str = "  ";
const DESCRIPTION_INDENT: usize = 20;
const WRAP_WIDTH: usize = 80;

/// Write command names and descriptions to an output string.
pub(crate) fn write_description(out: &mut String, name: &str, description: &str) {
    let mut current_line = INDENT.to_string();
    current_line.push_str(&name);

    if description.is_empty() {
        new_line(&mut current_line, out);
        return;
    }

    if !indent_description(&mut current_line) {
        // Start the description on a new line if the flag names already
        // add up to more than DESCRIPTION_INDENT.
        new_line(&mut current_line, out);
    }

    let mut words = description.split(' ').peekable();
    while let Some(first_word) = words.next() {
        indent_description(&mut current_line);
        current_line.push_str(first_word);

        'inner: while let Some(&word) = words.peek() {
            if (char_len(&current_line) + char_len(word) + 1) > WRAP_WIDTH {
                new_line(&mut current_line, out);
                break 'inner;
            } else {
                // advance the iterator
                let _ = words.next();
                current_line.push(' ');
                current_line.push_str(word);
            }
        }
    }
    new_line(&mut current_line, out);
}

// Indent the current line in to DESCRIPTION_INDENT chars.
// Returns a boolean indicating whether or not spacing was added.
fn indent_description(line: &mut String) -> bool {
    let cur_len = char_len(line);
    if cur_len < DESCRIPTION_INDENT {
        let num_spaces = DESCRIPTION_INDENT - cur_len;
        line.extend(std::iter::repeat(' ').take(num_spaces));
        true
    } else {
        false
    }
}

fn char_len(s: &str) -> usize {
    s.chars().count()
}

// Append a newline and the current line to the output,
// clearing the current line.
fn new_line(current_line: &mut String, out: &mut String) {
    out.push('\n');
    out.push_str(current_line);
    current_line.truncate(0);
}

#[cfg(test)]
mod test {
    use super::*;
    use argh::{FromArgs, SubCommands};

    #[derive(FromArgs, Debug)]
    #[argh(subcommand, name = "first", description = "the first subcommand")]
    struct SomeSubCommand {}

    #[derive(FromArgs, Debug)]
    #[argh(subcommand, name = "second", description = "the second subcommand")]
    struct OtherSubCommand {}

    #[derive(FromArgs, Debug)]
    #[argh(subcommand)]
    enum MySubCommands {
        First(SomeSubCommand),
        Second(OtherSubCommand),
    }

    #[derive(FromArgs, Debug)]
    #[argh(description = "my app")]
    struct MyApp {
        #[argh(subcommand)]
        _subcommand: MySubCommands,
    }

    #[test]
    fn output_is_like_argh() {
        let argh_help =
            MyApp::from_args(&["myapp"], &["help"]).expect_err("Expected early exit error").output;
        for cmd in MySubCommands::COMMANDS {
            let mut our_version = String::new();
            write_description(&mut our_version, cmd.name, cmd.description);
            assert!(argh_help.contains(&our_version))
        }
    }
}
