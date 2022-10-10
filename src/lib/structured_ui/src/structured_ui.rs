// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Structured user interface (SUI).
//!
//! Provides a wrapper around a Text UI (TUI) to support terminal, GUI, and
//! machine wrappers.
//!
//! Note: this is being developed within pbms as a proof of concept. The intent
//!       is to move this code when it's further along. Potentially using it for
//!       all ffx UI.

use {
    anyhow::Result,
    atty,
    serde::{Deserialize, Serialize},
    std::io::{Read, Write},
};

// Magic terminal escape codes.
const CLEAR_TO_EOL: &'static str = "\x1b[J";

/// Move the terminal cursor 'up' N rows.
fn cursor_move_up<W: ?Sized>(output: &mut W, rows: usize) -> Result<()>
where
    W: Write + Send + Sync,
{
    write!(output, "\x1b[{}A", rows)?;
    Ok(())
}

/// Return the ratio of progress over total step in a user readable format.
fn progress_percentage(at_: u64, of_: u64) -> f32 {
    if of_ == 0 {
        return 100.0;
    }
    at_ as f32 / of_ as f32 * 100.0
}

/// A single topic of progress, which can be nested within other topics.
/// E.g. coping file 3 of 100, and being on byte 2000 of 9000 within that file
/// is two ProgressEntry records.
#[derive(Clone, Debug, Default, Deserialize, Serialize)]
pub struct ProgressEntry {
    /// The current task description.
    name: String,

    /// How far along the progress is, as compared to `of`.
    at: u64,

    /// The point at which `at` is 100% complete. E.g. "at 50 of 100 steps".
    of: u64,

    /// What is represented by `at` and `of`. E.g. "bytes", "seconds", "steps".
    units: String,
}

/// A wrapper around one or more ProgressEntry records.
#[derive(Clone, Debug, Default, Deserialize, Serialize)]
pub struct Progress {
    /// Always "progress".
    kind: String,

    /// A overall description. E.g. "Copying files".
    title: String,

    /// An ordered list of progress entries.
    entries: Vec<ProgressEntry>,
}

impl Progress {
    pub fn builder() -> Self {
        Progress::default()
    }

    /// A label shown prominently, such as the dialog or window title in a GUI.
    pub fn title<'a>(&'a mut self, title: &'a str) -> &'a mut Self {
        self.title = title.to_string();
        self
    }

    /// Push another `ProgressEntry` to show nested progress.
    pub fn entry<'a>(
        &'a mut self,
        name: &'a str,
        at: u64,
        of: u64,
        units: &'a str,
    ) -> &'a mut Self {
        let entry = ProgressEntry { name: name.to_string(), at, of, units: units.to_string() };
        self.entries.push(entry);
        self
    }
}

/// A basic presentation used for alerts and short prompts for data.
#[derive(Debug, Deserialize, Serialize)]
pub struct SimplePresentation {
    kind: String,
    title: String,
    message: String,
    prompt: String,
}

/// A single horizontal row of a table.
#[derive(Clone, Debug, Default, Deserialize, Serialize)]
pub struct RowPresentation {
    /// If the row is used in a menu or selectable list, the `id` is used to
    /// identify which selection was made. Note: if the `id` is not unique, it
    /// may be difficult to know which selection was made.
    id: Option<String>,

    /// The entries that make up the row.
    columns: Vec<String>,
}

/// A menu or table of one or more rows.
///
/// It's advisable, though not required, to set the `header` and each of the
/// `rows` with the same number of columns.
#[derive(Clone, Debug, Default, Deserialize, Serialize)]
pub struct TableRows {
    /// Always "table_rows".
    kind: String,

    /// A short label presented at the top (or prominently). In a GUI, often the
    /// title of the tab, dialog, or window displaying the table.
    title: Option<String>,

    /// Appears above the table. Often descriptive text.
    /// Compare to `note`.
    message: Option<String>,

    /// The header is the top row of the table. It's commonly used to label the
    /// columns in the table and normally doesn't contain data itself.
    header: RowPresentation,

    /// Rows are the body of the table. This is where the actual data in the
    /// table is presented.
    rows: Vec<RowPresentation>,

    /// Appears below the table. Often a list and description of special
    /// symbols used in the table. Common examples are asterisk,
    /// double-asterisk, dagger, etc.
    /// Compare to 'message'.
    note: Option<String>,

    /// The `id` of the `rows` entry which is selected by default.
    default: Option<String>,

    /// Often a question for the user, especially if the table shows a menu of
    /// options to choose from.
    prompt: Option<String>,
}

impl TableRows {
    pub fn builder() -> Self {
        TableRows::default()
    }

    /// Add a title.
    pub fn title<'a, S>(&'a mut self, title: S) -> &'a mut Self
    where
        S: Into<String>,
    {
        self.title = Some(title.into());
        self
    }

    /// Add a header to the top of the table.
    ///
    /// It's advisable, though not required, to set the `header` and each `row`
    /// with the same number of columns.
    pub fn header<'a, S>(&'a mut self, columns: Vec<S>) -> &'a mut Self
    where
        S: AsRef<str>,
    {
        let columns = columns.iter().map(|s| s.as_ref().to_string()).collect::<Vec<String>>();
        self.header = RowPresentation { id: None, columns };
        self
    }

    /// Append a row.
    ///
    /// This call may be repeated. The rows will be displayed in the order they
    /// are added with this call.
    ///
    /// It's advisable, though not required, to set the `header` and each `row`
    /// with the same number of columns.
    pub fn row<'a, S>(&'a mut self, columns: Vec<S>) -> &'a mut Self
    where
        S: AsRef<str>,
    {
        let columns = columns.iter().map(|s| s.as_ref().to_string()).collect::<Vec<String>>();
        self.rows.push(RowPresentation { id: None, columns });
        self
    }

    /// Append a row with an id value.
    ///
    /// The `id` should be unique and provides a way to refer to a row.
    ///
    /// This call may be repeated. The rows will be displayed in the order they
    /// are added with this call.
    ///
    /// It's advisable, though not required, to set the `header` and each `row`
    /// with the same number of columns.
    pub fn row_with_id<'a, S>(&'a mut self, id: S, columns: Vec<S>) -> &'a mut Self
    where
        S: AsRef<str>,
    {
        let columns = columns.iter().map(|s| s.as_ref().to_string()).collect::<Vec<String>>();
        self.rows.push(RowPresentation { id: Some(id.as_ref().to_string()), columns });
        self
    }

    /// Add a note.
    pub fn note<'a, S>(&'a mut self, note: S) -> &'a mut Self
    where
        S: Into<String>,
    {
        self.note = Some(note.into());
        self
    }
}

#[derive(Debug, Deserialize, Serialize)]
pub enum Presentation {
    Progress(Progress),
    StringPrompt(SimplePresentation),
    Table(TableRows),
}

/// User response to a request.
#[derive(Debug, Deserialize, Serialize)]
pub enum Response {
    /// The default action was chosen, i.e. pressed "Enter" or "Return".
    Default,

    /// Leave/close without making a choice, i.e. "Cancel" or "Esc".
    NoChoice,

    /// One of the choice keys passed into the presentation.
    Choice(String),

    /// All further steps are to be skipped (aka abort or terminate).
    Quit,
}

pub trait Interface: Send + Sync {
    fn present(&mut self, output: &Presentation) -> Result<Response>;
}

/// A text based UI, likely a terminal.
pub struct TextUi<'a> {
    /// E.g. stdin.
    #[allow(unused)]
    input: &'a mut (dyn Read + Send + Sync + 'a),

    /// E.g. stdout.
    output: &'a mut (dyn Write + Send + Sync + 'a),

    /// E.g. stderr.
    #[allow(unused)]
    error_output: &'a mut (dyn Write + Send + Sync + 'a),

    /// Progress output overwrites itself at each iteration, but the first iteration needs to
    /// not overwrite the existing text on the terminal.
    progress_needs_to_scroll: bool,
}

impl<'a> TextUi<'a> {
    pub fn new<R, W, E>(input: &'a mut R, output: &'a mut W, error_output: &'a mut E) -> Self
    where
        R: Read + Send + Sync + 'a,
        W: Write + Send + Sync + 'a,
        E: Write + Send + Sync + 'a,
    {
        Self { input, output, error_output, progress_needs_to_scroll: true }
    }

    fn present_progress(&mut self, progress: &Progress) -> Result<Response> {
        // We only print the progress text if it's going to a TTY terminal, since the shell
        // control sequences don't make sense otherwise.
        if atty::is(atty::Stream::Stdout) {
            // Move back to overwrite the previous progress rendering.
            if !self.progress_needs_to_scroll {
                cursor_move_up(self.output, 1 + progress.entries.len() * 2)?;
            } else {
                self.progress_needs_to_scroll = false;
            }
            write!(self.output, "Progress for \"{}\"{}\n", progress.title, CLEAR_TO_EOL)?;
            for entry in &progress.entries {
                write!(self.output, "  {}{}\n", entry.name, CLEAR_TO_EOL)?;
                write!(
                    self.output,
                    "    {} of {} {} ({:.2}%){}\n",
                    entry.at,
                    entry.of,
                    entry.units,
                    progress_percentage(entry.at, entry.of),
                    CLEAR_TO_EOL
                )?;
            }
        }
        Ok(Response::Default)
    }

    fn present_table(&mut self, table: &TableRows) -> Result<Response> {
        if let Some(title) = &table.title {
            writeln!(self.output, "{}", title)?;
        }
        if let Some(message) = &table.message {
            self.output.write_all(message.as_bytes())?;
        }
        for row in &table.rows {
            for column in &row.columns {
                write!(self.output, "{} ", column)?;
            }
            writeln!(self.output, "")?;
        }
        if let Some(note) = &table.note {
            self.output.write_all(note.as_bytes())?;
        }
        Ok(Response::Default)
    }
}

impl<'a> Interface for TextUi<'a> {
    fn present(&mut self, presentation: &Presentation) -> Result<Response> {
        match presentation {
            Presentation::Progress(p) => return self.present_progress(p),
            Presentation::StringPrompt(_) => (),
            Presentation::Table(p) => return self.present_table(p),
        }
        Ok(Response::Default)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_progress() {
        let mut input = "".as_bytes();
        let mut output: Vec<u8> = Vec::new();
        let mut err_out: Vec<u8> = Vec::new();
        let mut ui = TextUi::new(&mut input, &mut output, &mut err_out);
        let mut progress = Progress::builder();
        progress.title("foo");
        progress.entry("bushel", /*at=*/ 20, /*of=*/ 100, "pieces");
        progress.entry("apple", /*at=*/ 5, /*of=*/ 10, "bites");
        ui.present(&Presentation::Progress(progress)).expect("present progress");
        let output = String::from_utf8(output).expect("string form utf8");
        assert!(output.contains("foo"));
        assert!(output.contains("bushel"));
        assert!(output.contains("apple"));
        assert!(output.contains("pieces"));
        assert!(output.contains("bites"));
    }

    #[test]
    fn test_table() {
        let mut input = "".as_bytes();
        let mut output: Vec<u8> = Vec::new();
        let mut err_out: Vec<u8> = Vec::new();
        let mut ui = TextUi::new(&mut input, &mut output, &mut err_out);
        let mut table = TableRows::builder();
        table.title("foo");
        table.header(vec!["type", "count", "notes"]);
        table.row(vec!["fruit", "5", "orange"]);
        table.row_with_id(/*id=*/ "a", vec!["car", "10", "red"]);
        table.note("bar");
        ui.present(&Presentation::Table(table)).expect("present table");
        let output = String::from_utf8(output).expect("string form utf8");
        println!("{}", output);
        assert!(output.contains("foo"));
        assert!(output.contains("bar"));
        assert!(output.contains("fruit"));
        assert!(output.contains("orange"));
        assert!(output.contains("car"));
        assert!(output.contains("red"));
    }
}
