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
    cfg_if::cfg_if,
    serde::{Deserialize, Serialize},
    std::io::{BufRead, BufReader, Read, Write},
    unicode_segmentation::UnicodeSegmentation,
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
        Progress { kind: "progress".to_string(), ..Default::default() }
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
#[derive(Debug, Default, Deserialize, Serialize)]
pub struct SimplePresentation {
    /// One of "string_prompt" or "alert".
    kind: String,

    /// A overall description. E.g. "Copying files".
    title: Option<String>,

    /// The body of the message to the user.
    message: Option<String>,

    /// The specific question or call-to-action to the user.
    prompt: String,
}

impl SimplePresentation {
    pub fn builder() -> Self {
        SimplePresentation { kind: "string_prompt".to_string(), ..Default::default() }
    }

    /// A label shown prominently, such as the dialog or window title in a GUI.
    pub fn title<'a, S>(&'a mut self, title: S) -> &'a mut Self
    where
        S: Into<String>,
    {
        self.title = Some(title.into());
        self
    }

    pub fn message<'a, S>(&'a mut self, message: S) -> &'a mut Self
    where
        S: Into<String>,
    {
        self.message = Some(message.into());
        self
    }

    pub fn prompt<'a>(&'a mut self, prompt: &'a str) -> &'a mut Self {
        self.prompt = prompt.to_string();
        self
    }
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

/// A message used for informing the user of important information.
///
/// A notice differs from an alert in that a notice does not ask for
/// acknowledgement. Instead the notice is presented until the action or state
/// described by the notice completes.
///
/// This is similar to a progress where the progress is unknown (like spinner).
/// The notice is shown until the user cancels the action or the action
/// completes.
#[derive(Debug, Default, Deserialize, Serialize)]
pub struct Notice {
    /// Always "notice".
    kind: String,

    /// A overall description. E.g. "Copying files".
    title: Option<String>,

    /// The body of the message to the user.
    message: Option<String>,
}

impl Notice {
    pub fn builder() -> Self {
        Notice { kind: "notice".to_string(), ..Default::default() }
    }

    /// A label shown prominently, such as the dialog or window title in a GUI.
    pub fn title<'a, S>(&'a mut self, title: S) -> &'a mut Self
    where
        S: Into<String>,
    {
        self.title = Some(title.into());
        self
    }

    pub fn message<'a, S>(&'a mut self, message: S) -> &'a mut Self
    where
        S: Into<String>,
    {
        self.message = Some(message.into());
        self
    }
}

#[derive(Debug, Deserialize, Serialize)]
pub enum Presentation {
    Notice(Notice),
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
    fn present(&self, output: &Presentation) -> Result<Response>;
}

/// A text based UI, likely a terminal.
pub struct InnerTextUi<'a> {
    /// E.g. stdin.
    #[allow(unused)]
    input: &'a mut (dyn Read + Send + Sync + 'a),

    /// E.g. stdout.
    output: &'a mut (dyn Write + Send + Sync + 'a),

    /// E.g. stderr.
    #[allow(unused)]
    error_output: &'a mut (dyn Write + Send + Sync + 'a),

    /// Some text UI overwrites itself at each iteration other than the first.
    /// Track how many lines to overwrite.
    overwrite_line_count: usize,
}

#[allow(dead_code)]
mod mock_atty {
    pub(super) fn always_a_tty(_: atty::Stream) -> bool {
        true
    }
}

cfg_if! {
    if #[cfg(test)] {
        use mock_atty::always_a_tty as is;
    } else {
        use atty::is;
    }
}

pub struct TextUi<'a> {
    inner: std::sync::Mutex<InnerTextUi<'a>>,
}

impl<'a> TextUi<'a> {
    pub fn new<R, W, E>(input: &'a mut R, output: &'a mut W, error_output: &'a mut E) -> Self
    where
        R: Read + Send + Sync + 'a,
        W: Write + Send + Sync + 'a,
        E: Write + Send + Sync + 'a,
    {
        Self {
            inner: std::sync::Mutex::new(InnerTextUi {
                input,
                output,
                error_output,
                overwrite_line_count: 0,
            }),
        }
    }

    fn present_progress(&self, progress: &Progress) -> Result<Response> {
        // We only print the progress text if it's going to a TTY terminal,
        // since the shell control sequences don't make sense otherwise.
        if !is(atty::Stream::Stdout) {
            return Ok(Response::Default);
        }
        let mut inner = self.inner.lock().expect("present_progress lock");
        // Move back to overwrite the previous progress rendering.
        let mut lines_to_overwrite = inner.overwrite_line_count;
        if lines_to_overwrite > 0 {
            cursor_move_up(inner.output, lines_to_overwrite)?;
        }
        inner.overwrite_line_count = 0;
        write!(inner.output, "Progress for \"{}\"{}\n", progress.title, CLEAR_TO_EOL)?;
        inner.overwrite_line_count += 1;
        let term_width = termion::terminal_size().unwrap_or((80, 40)).0 as usize;
        const MARGINS: usize = /*indent=*/ 2 + /*right_side=*/ 1;
        let limit = term_width.saturating_sub(MARGINS);
        for entry in &progress.entries {
            write!(
                inner.output,
                "  {}{}\n",
                ellipsis(&entry.name, limit, Some('/')),
                CLEAR_TO_EOL
            )?;
            write!(
                inner.output,
                "    {} of {} {} ({:.2}%){}\n",
                entry.at,
                entry.of,
                entry.units,
                progress_percentage(entry.at, entry.of),
                CLEAR_TO_EOL
            )?;
            inner.overwrite_line_count += 2;
        }
        while lines_to_overwrite > inner.overwrite_line_count {
            write!(inner.output, "{}\n", CLEAR_TO_EOL)?;
            lines_to_overwrite -= 1;
        }
        Ok(Response::Default)
    }

    fn present_notice(&self, element: &Notice) -> Result<Response> {
        let mut inner = self.inner.lock().expect("present_string_prompt lock");
        if let Some(title) = &element.title {
            writeln!(inner.output, "{}", title)?;
        }
        if let Some(message) = &element.message {
            writeln!(inner.output, "{}", message)?;
        }
        Ok(Response::Default)
    }

    fn present_string_prompt(&self, element: &SimplePresentation) -> Result<Response> {
        if !is(atty::Stream::Stdout) {
            // If the terminal is non-interactive, it's not reasonable to prompt
            // the user.
            return Ok(Response::NoChoice);
        }
        let mut inner = self.inner.lock().expect("present_string_prompt lock");
        if let Some(title) = &element.title {
            writeln!(inner.output, "{}", title)?;
        }
        if let Some(message) = &element.message {
            writeln!(inner.output, "{}", message)?;
        }
        writeln!(inner.output, "{}: ", element.prompt)?;
        let mut buf_reader = BufReader::new(&mut inner.input);
        let mut choice = String::new();
        buf_reader.read_line(&mut choice).expect("reading string input line");
        if choice.is_empty() {
            Ok(Response::Default)
        } else {
            Ok(Response::Choice(choice))
        }
    }

    fn present_table(&self, table: &TableRows) -> Result<Response> {
        let mut inner = self.inner.lock().expect("present_table lock");
        if let Some(title) = &table.title {
            writeln!(inner.output, "{}", title)?;
        }
        if let Some(message) = &table.message {
            inner.output.write_all(message.as_bytes())?;
        }
        for row in &table.rows {
            for column in &row.columns {
                write!(inner.output, "{} ", column)?;
            }
            writeln!(inner.output, "")?;
        }
        if let Some(note) = &table.note {
            inner.output.write_all(note.as_bytes())?;
        }
        Ok(Response::Default)
    }
}

impl<'a> Interface for TextUi<'a> {
    fn present(&self, presentation: &Presentation) -> Result<Response> {
        match presentation {
            Presentation::Notice(p) => self.present_notice(p),
            Presentation::Progress(p) => self.present_progress(p),
            Presentation::StringPrompt(p) => self.present_string_prompt(p),
            Presentation::Table(p) => self.present_table(p),
        }
    }
}

/// If the string is longer than `limit`, ellipsis the string in the middle so
/// that the overall len is `limit` in length.
fn ellipsis(s: &str, limit: usize, prefer: Option<char>) -> String {
    // UX has determined that this should be Chicago manual style "..." (without
    // extra spaces) rather than MLA style "[...]".
    const ELLIPSE: &str = "...";
    // Optimization: if the byte length is less than limit, it's very unlikely
    // that the grapheme count will be larger. (I think that's not possible.)
    // i.e. there would need to be a single byte utf8 which is rendered in
    // multiple fixed-width font cells.
    if s.len() <= limit {
        return s.to_string();
    }
    let total = s.graphemes(/*is_extended=*/ true).count();
    if total <= limit {
        return s.to_string();
    }
    if limit < ELLIPSE.len() {
        return ELLIPSE[..limit].to_string();
    }
    // Determine offsets (end of first piece and start of second piece).
    let mut first = (limit - ELLIPSE.len()) / 2;
    if let Some(ch) = prefer {
        if let Some(n) = s[..first].rfind(ch) {
            first = n + 1;
        }
    }
    let mut second = total - (limit - ELLIPSE.len() - first);
    if let Some(ch) = prefer {
        if let Some(n) = s[second..].find(ch) {
            second += n;
        }
    }
    // Build the new string.
    s.graphemes(/*is_extended=*/ true)
        .take(first)
        .chain(ELLIPSE.graphemes(/*is_extended=*/ true))
        .chain(s.graphemes(true).skip(second).take(total))
        .collect()
}

pub struct MockUi {}

impl MockUi {
    pub fn new() -> Self {
        Self {}
    }

    fn present_notice(&self, _notice: &Notice) -> Result<Response> {
        Ok(Response::Default)
    }

    fn present_progress(&self, _progress: &Progress) -> Result<Response> {
        Ok(Response::Default)
    }

    fn present_string_prompt(&self, _element: &SimplePresentation) -> Result<Response> {
        Ok(Response::Default)
    }

    fn present_table(&self, _table: &TableRows) -> Result<Response> {
        Ok(Response::Default)
    }
}

impl Interface for MockUi {
    fn present(&self, presentation: &Presentation) -> Result<Response> {
        match presentation {
            Presentation::Notice(p) => self.present_notice(p),
            Presentation::Progress(p) => self.present_progress(p),
            Presentation::StringPrompt(p) => self.present_string_prompt(p),
            Presentation::Table(p) => self.present_table(p),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_notice() {
        let mut input = "".as_bytes();
        let mut output: Vec<u8> = Vec::new();
        let mut err_out: Vec<u8> = Vec::new();
        let ui = TextUi::new(&mut input, &mut output, &mut err_out);
        let mut notice = Notice::builder();
        notice.title("foo");
        notice.message("Test message for notice.");
        ui.present(&Presentation::Notice(notice)).expect("present notice");
        let output = String::from_utf8(output).expect("string form utf8");
        assert!(output.contains("foo"));
        assert!(output.contains("Test message for notice"));
        assert!(output.contains("notice"));
    }

    #[test]
    fn test_progress() {
        let mut input = "".as_bytes();
        let mut output: Vec<u8> = Vec::new();
        let mut err_out: Vec<u8> = Vec::new();
        let ui = TextUi::new(&mut input, &mut output, &mut err_out);
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
        let ui = TextUi::new(&mut input, &mut output, &mut err_out);
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

    #[test]
    fn test_ellipsis() {
        assert_eq!(ellipsis("cake/drought/fins", 100, /*prefer=*/ None), "cake/drought/fins");
        assert_eq!(ellipsis("cake/drought/fins", 17, /*prefer=*/ None), "cake/drought/fins");
        assert_eq!(ellipsis("cake/drought/fins", 16, /*prefer=*/ None), "cake/d...ht/fins");
        assert_eq!(ellipsis("cake/drought/fins", 15, /*prefer=*/ None), "cake/d...t/fins");
        assert_eq!(ellipsis("cake/drought/fins", 14, /*prefer=*/ None), "cake/...t/fins");
        assert_eq!(ellipsis("cake/drought/fins", 12, /*prefer=*/ None), "cake.../fins");
        assert_eq!(ellipsis("cake/drought/fins", 11, /*prefer=*/ None), "cake...fins");
        assert_eq!(ellipsis("cake/drought/fins", 5, /*prefer=*/ None), "c...s");
        assert_eq!(ellipsis("cake/drought/fins", 4, /*prefer=*/ None), "...s");
        assert_eq!(ellipsis("cake/drought/fins", 3, /*prefer=*/ None), "...");
        assert_eq!(ellipsis("cake/drought/fins", 1, /*prefer=*/ None), ".");
        assert_eq!(ellipsis("cake/drought/fins", 0, /*prefer=*/ None), "");

        assert_eq!(ellipsis("cake/drought/fins", 100, Some('/')), "cake/drought/fins");
        assert_eq!(ellipsis("cake/drought/fins", 17, Some('/')), "cake/drought/fins");
        assert_eq!(ellipsis("cake/drought/fins", 16, Some('/')), "cake/.../fins");
        assert_eq!(ellipsis("cake/drought/fins", 15, Some('/')), "cake/.../fins");
        assert_eq!(ellipsis("cake/drought/fins", 14, Some('/')), "cake/.../fins");
        assert_eq!(ellipsis("cake/drought/fins", 12, Some('/')), "cake.../fins");
        assert_eq!(ellipsis("cake/drought/fins", 11, Some('/')), "cake...fins");
        assert_eq!(ellipsis("cake/drought/fins", 5, Some('/')), "c...s");
        assert_eq!(ellipsis("cake/drought/fins", 4, Some('/')), "...s");
        assert_eq!(ellipsis("cake/drought/fins", 3, Some('/')), "...");
        assert_eq!(ellipsis("cake/drought/fins", 1, Some('/')), ".");
        assert_eq!(ellipsis("cake/drought/fins", 0, Some('/')), "");

        assert_eq!(ellipsis("/x/cake/drought_fins", 20, Some('/')), "/x/cake/drought_fins");
        assert_eq!(ellipsis("/x/cake/drought_fins", 19, Some('/')), "/x/cake/...ght_fins");
        assert_eq!(ellipsis("/x/cake/drought_fins", 18, Some('/')), "/x/...drought_fins");
        assert_eq!(ellipsis("/x/cake/drought_fins", 17, Some('/')), "/x/...rought_fins");
        assert_eq!(ellipsis("/x/cake/drought_fins", 16, Some('/')), "/x/...ought_fins");
        assert_eq!(ellipsis("/x/cake/drought_fins", 15, Some('/')), "/x/...ught_fins");
        assert_eq!(ellipsis("/x/cake/drought_fins", 14, Some('/')), "/x/...ght_fins");
        assert_eq!(ellipsis("/x/cake/drought_fins", 13, Some('/')), "/x/...ht_fins");
        assert_eq!(ellipsis("/x/cake/drought_fins", 12, Some('/')), "/x/...t_fins");
        assert_eq!(ellipsis("/x/cake/drought_fins", 11, Some('/')), "/x/..._fins");
    }
}
