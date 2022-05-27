// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{bail, format_err, Result},
    argh::FromArgs,
    serde::{de::DeserializeOwned, Deserialize},
    serde_yaml,
    std::{
        fs::{File, OpenOptions},
        io::{stdin, stdout, BufRead, BufReader, Read, Write},
        path::PathBuf,
        process::Command,
    },
};

#[derive(FromArgs, Debug, Default, PartialEq)]
#[argh(
    description = "Creates a new RFC prepopulated with common fields.
    The command attempts to auto-populate the author sections from your git configuration.",
    example = "To create a new RFC run the following command and enter the requested information.

    You can also use options to provide values for parameters that you don't wish to provide
    interactively.

    ```
    $ fx rfc --title 'Back to the Fuchsia'
    Description: Fuchsia is a modern OS
    ...
    Area(s): 1, 2
    Issues: 123, 456
    Reviewers: foo@google.com, bar@google.com
    ```

    Your RFC markdown file and its metadata are added to //docs/contribute/governance/rfcs/",
    note = "To learn more about writing RFCs, see
    https://fuchsia.dev/fuchsia-src/contribute/governance/rfcs"
)]
pub struct CreateRfcArgs {
    /// title to use for the RFC.
    #[argh(option)]
    pub title: Option<String>,

    /// short description to use for the RFC.
    #[argh(option)]
    pub short_description: Option<String>,

    /// emails of the RFC authors.
    #[argh(option, long = "author")]
    pub authors: Vec<String>,

    /// areas of the RFC.
    #[argh(option, long = "area")]
    pub areas: Vec<String>,

    /// monorail issues associated with the RFC.
    #[argh(option, long = "issue")]
    pub issues: Vec<String>,

    /// emails of the anticipated RFC reviewers.
    #[argh(option, long = "reviewer")]
    pub reviewers: Vec<String>,
}

const AREAS_FILE: &str = "_areas.yaml";
const FX_RFC_GENERATED: &str = "<!-- Generated with `fx rfc` -->";
const META_FILE: &str = "_rfcs.yaml";
const RFC_NAME_PLACEHOLDER: &str = "RFC-NNNN";
const RFCS_PATH: &str = "docs/contribute/governance/rfcs";
const STATUS_PLACEHOLDER: &str = "Pending";
const TEMPLATE_FILE: &str = "TEMPLATE.md";
const TOC_FILE: &str = "_toc.yaml";
const TODO_COMMENT: &str = "# TODO: DO NOT SUBMIT, update.";

fn main() -> Result<(), anyhow::Error> {
    let args: CreateRfcArgs = argh::from_env();
    let handle = stdin().lock();
    let cwd = std::env::current_dir()?;
    let author_email_provider = GitConfig::new();
    rfc_impl(
        CommandLine { args, writer: &mut stdout(), reader: handle, author_email_provider },
        cwd,
    )
}

fn rfc_impl<A, R, W>(mut cli: CommandLine<'_, A, R, W>, cwd: PathBuf) -> Result<()>
where
    A: AuthorEmailProvider,
    R: BufRead,
    W: Write,
{
    let fuchsia_dir_root = get_fuchsia_dir_root(cwd)?.ok_or(format_err!(
        "This command must be run from within the Fuchsia Platform Source Tree"
    ))?;
    let rfcs_dir = fuchsia_dir_root.join(RFCS_PATH);

    let title = cli.get_title()?;
    let short_description = cli.get_short_description()?;
    let authors = cli.get_authors()?;

    let valid_areas = load_yaml::<Vec<String>>(rfcs_dir.join(AREAS_FILE))?;
    let area = cli.get_areas(valid_areas)?;
    let issue = cli.get_issues()?;
    let reviewers = cli.get_reviewers()?;

    let file = filename_from_title(&title);

    let toc_meta = TocMetadata {
        title: format!("{}: {}", RFC_NAME_PLACEHOLDER, title),
        path: format!("/{}/{}", RFCS_PATH, file),
    };
    let mut rfc_meta = RfcMetadata {
        name: RFC_NAME_PLACEHOLDER.to_string(),
        title,
        short_description,
        authors,
        file,
        area,
        issue,
        reviewers,
        status: STATUS_PLACEHOLDER.to_string(),
        gerrit_change_id: vec![],
        submitted: String::new(),
        reviewed: String::new(),
    };

    rfc_meta.sort();

    create_rfc(&rfc_meta, &rfcs_dir)?;
    append_rfc_meta(rfc_meta, rfcs_dir.join(META_FILE))?;
    append_rfc_meta(toc_meta, rfcs_dir.join(TOC_FILE))?;

    Ok(())
}

struct CommandLine<'a, A, R, W> {
    args: CreateRfcArgs,
    author_email_provider: A,
    reader: R,
    writer: &'a mut W,
}

impl<'a, A, R, W> CommandLine<'a, A, R, W>
where
    W: Write,
    R: BufRead,
    A: AuthorEmailProvider,
{
    pub fn get_title(&mut self) -> Result<String> {
        match &self.args.title {
            Some(title) => Ok(title.clone()),
            None => self.prompt("Title"),
        }
    }

    pub fn get_short_description(&mut self) -> Result<String> {
        match &self.args.short_description {
            Some(description) => Ok(description.clone()),
            None => self.prompt("Short description"),
        }
    }

    pub fn get_authors(&mut self) -> Result<Vec<String>> {
        let default_author = self.author_email_provider.get_email().ok();
        let mut authors = self.strings_or_prompt(
            self.args.authors.clone(),
            format!(
                "Authors (comma-separated){}",
                default_author.as_ref().map(|a| format!(" [default: {}]", a)).unwrap_or_default()
            ),
        )?;
        if authors.is_empty() && default_author.is_some() {
            authors.push(default_author.unwrap());
        }
        Ok(authors)
    }

    pub fn get_areas(&mut self, valid_areas: Vec<String>) -> Result<Vec<String>> {
        if self.args.areas.is_empty() {
            return self.request_areas(valid_areas);
        }
        let args_areas_len = self.args.areas.len();
        let input_areas =
            self.args.areas.clone().into_iter().map(|a| a.to_lowercase()).collect::<Vec<_>>();
        let areas_found = valid_areas
            .iter()
            .filter(|a| input_areas.contains(&a.to_lowercase()))
            .cloned()
            .collect::<Vec<_>>();
        if areas_found.len() == args_areas_len {
            Ok(areas_found)
        } else {
            self.println("Invalid areas provided. Please select valid ones.")?;
            self.request_areas(valid_areas)
        }
    }

    pub fn get_issues(&mut self) -> Result<Vec<String>> {
        self.strings_or_prompt(self.args.issues.clone(), "Monorail issue (comma-separated)")
    }

    pub fn get_reviewers(&mut self) -> Result<Vec<String>> {
        self.strings_or_prompt(self.args.reviewers.clone(), "Reviewers (comma-separated emails)")
    }

    fn strings_or_prompt(
        &mut self,
        list: Vec<String>,
        prompt: impl AsRef<str>,
    ) -> Result<Vec<String>> {
        if list.is_empty() {
            let input = self.prompt(prompt)?;
            if input.trim().is_empty() {
                return Ok(vec![]);
            }
            Ok(string_list(input))
        } else {
            Ok(list)
        }
    }

    fn request_areas(&mut self, valid_areas: Vec<String>) -> Result<Vec<String>> {
        for (i, area) in valid_areas.iter().enumerate() {
            self.println(format!("[{:>2}] {}", i, area))?;
        }
        let areas =
            areas_from_numbers(valid_areas, self.prompt("Area (comma-separated numbers)")?)?;
        Ok(areas)
    }

    fn prompt(&mut self, text: impl AsRef<str>) -> Result<String> {
        write!(&mut self.writer, "{}: ", text.as_ref())?;
        self.writer.flush().unwrap();
        let mut line = String::new();
        self.reader.read_line(&mut line)?;
        Ok(line.trim().to_string())
    }

    fn println(&mut self, text: impl AsRef<str>) -> Result<()> {
        writeln!(&mut self.writer, "{}", text.as_ref())?;
        Ok(())
    }
}

// Note: not using serde_yaml since we have a bit of a special formatting for arrays. It doesn't
// seem that serde_yaml supports formatting arrays as `[]` like we do in our YAML files. Using
// serde_yaml would require updating the format of our YAML files, in particular _rfcs.yaml.
trait AppendYaml {
    fn append_yaml(&self, w: impl Write) -> Result<()>;
}

struct RfcMetadata {
    name: String,
    title: String,
    short_description: String,
    authors: Vec<String>,
    file: String,
    area: Vec<String>,
    issue: Vec<String>,
    gerrit_change_id: Vec<String>,
    status: String,
    reviewers: Vec<String>,
    submitted: String,
    reviewed: String,
}

impl RfcMetadata {
    fn sort(&mut self) {
        self.authors.sort();
        self.area.sort();
        self.issue.sort();
        self.gerrit_change_id.sort();
        self.reviewers.sort();
    }
}

impl AppendYaml for RfcMetadata {
    fn append_yaml(&self, mut w: impl Write) -> Result<()> {
        w.write_all(b"\n")?;
        write_one(&mut w, 0, "- name", &self.name, "'", true)?;
        write_one(&mut w, 2, "title", &self.title, "'", self.title.is_empty())?;
        write_one(
            &mut w,
            2,
            "short_description",
            &self.short_description,
            "'",
            self.short_description.is_empty(),
        )?;
        write_list(&mut w, 2, "authors", &self.authors)?;
        write_one(&mut w, 2, "file", &self.file, "'", false)?;
        write_list(&mut w, 2, "area", &self.area)?;
        write_list(&mut w, 2, "issue", &self.issue)?;
        write_list(&mut w, 2, "gerrit_change_id", &self.gerrit_change_id)?;
        write_one(&mut w, 2, "status", &self.status, "'", true)?;
        write_list(&mut w, 2, "reviewers", &self.reviewers)?;
        write_one(&mut w, 2, "submitted", &self.submitted, "'", true)?;
        write_one(&mut w, 2, "reviewed", &self.reviewed, "'", true)?;
        Ok(())
    }
}

#[derive(Deserialize)]
struct TocMetadata {
    title: String,
    path: String,
}

impl AppendYaml for TocMetadata {
    fn append_yaml(&self, mut w: impl Write) -> Result<()> {
        write_one(&mut w, 2, "- title", &self.title, "\"", true)?;
        write_one(&mut w, 4, "path", &self.path, "", true)?;
        Ok(())
    }
}

trait AuthorEmailProvider {
    fn get_email(&self) -> Result<String>;
}

struct GitConfig {}
impl GitConfig {
    fn new() -> Self {
        Self {}
    }
}

impl AuthorEmailProvider for GitConfig {
    fn get_email(&self) -> Result<String> {
        let result = Command::new("sh").arg("-c").arg("git config --get user.email").output()?;
        Ok(String::from_utf8(result.stdout)?.trim().to_string())
    }
}

fn write_one<T, W>(
    w: &mut W,
    spaces: usize,
    prefix: &str,
    value: T,
    quote_char: &str,
    comment: bool,
) -> Result<()>
where
    W: Write,
    T: std::fmt::Display,
{
    write!(w, "{}{}: {}{}{}", " ".repeat(spaces), prefix, quote_char, value, quote_char)?;
    if comment {
        writeln!(w, "  {}", TODO_COMMENT)?;
    } else {
        write!(w, "\n")?;
    }
    Ok(())
}

fn write_list<T, W>(w: &mut W, spaces: usize, prefix: &str, list: &[T]) -> Result<()>
where
    W: Write,
    T: std::fmt::Display,
{
    write!(w, "{}{}: [", " ".repeat(spaces), prefix)?;
    for (i, item) in list.iter().enumerate() {
        write!(w, "'{}'", item)?;
        if i < list.len() - 1 {
            write!(w, ", ")?;
        }
    }
    if list.is_empty() {
        writeln!(w, "'']  {}", TODO_COMMENT)?;
    } else {
        writeln!(w, "]")?;
    }
    Ok(())
}

fn filename_from_title(title: &str) -> String {
    format!(
        "NNNN_{}.md",
        title.to_lowercase().split(' ').map(|s| s.trim()).collect::<Vec<_>>().join("_"),
    )
}

fn areas_from_numbers(valid_areas: Vec<String>, input_numbers: String) -> Result<Vec<String>> {
    let indexes = input_numbers
        .split(',')
        .map(|s| s.trim().parse::<usize>())
        .collect::<Result<Vec<usize>, _>>()
        .map_err(|_| format_err!("Invalid area index found"))?;
    if !indexes.iter().all(|i| *i < valid_areas.len()) {
        bail!("Invalid area indexes");
    }
    Ok(indexes.into_iter().map(|i| valid_areas[i].clone()).collect())
}

fn string_list(input_authors: String) -> Vec<String> {
    input_authors.split(',').map(|s| s.trim().to_string()).collect()
}

fn get_fuchsia_dir_root(current_dir: PathBuf) -> Result<Option<PathBuf>> {
    let mut current_dir = current_dir;
    loop {
        let jiri_root_path = current_dir.join(".jiri_root/");
        if jiri_root_path.exists() {
            return Ok(Some(current_dir));
        }
        if !current_dir.pop() {
            break;
        }
    }
    Ok(None)
}

fn load_yaml<T>(path: PathBuf) -> Result<T>
where
    T: DeserializeOwned,
{
    let file = File::open(path)?;
    let reader = BufReader::new(file);
    Ok(serde_yaml::from_reader(reader).expect("can read yaml"))
}

fn append_rfc_meta<T>(data: T, path: PathBuf) -> Result<()>
where
    T: AppendYaml,
{
    let file = OpenOptions::new().write(true).append(true).open(path)?;
    data.append_yaml(file)?;
    Ok(())
}

fn create_rfc(meta: &RfcMetadata, path: &PathBuf) -> Result<()> {
    let mut template_file = File::open(path.join(TEMPLATE_FILE))?;
    let mut template = String::new();
    template_file.read_to_string(&mut template)?;

    let template = template.replace(
        r#"{% set rfcid = "RFC-0000" %}"#,
        r#"{% set rfcid = "RFC-NNNN" %} <!-- TODO: DO NOT SUBMIT, update number -->"#,
    );

    let reviewers =
        meta.reviewers.iter().map(|r| format!("- {}", r)).collect::<Vec<_>>().join("\n");

    let reviewers_index = template.find("_Reviewers:_").unwrap();
    let consulted_index = template.find("_Consulted:_").unwrap();
    let template = format!(
        "{}\n{}_Reviewers:_\n\n{}\n\n{}",
        FX_RFC_GENERATED,
        &template[..reviewers_index],
        reviewers,
        &template[consulted_index..]
    );

    let mut rfc_file = File::create(path.join(&meta.file))?;
    rfc_file.write_all(template.as_bytes())?;
    Ok(())
}

#[cfg(test)]
mod test {
    use super::*;
    use pretty_assertions::assert_eq;
    use std::fs;
    use tempfile::{tempdir, TempDir};

    const CMD_NAME: &'static [&'static str] = &["rfc"];

    struct FakeEmailProvider {}

    impl AuthorEmailProvider for FakeEmailProvider {
        fn get_email(&self) -> Result<String> {
            Ok("fuchsia-hacker@google.com".to_string())
        }
    }

    struct FakeDir {
        root: TempDir,
        rfcs_path: PathBuf,
    }

    impl FakeDir {
        pub fn new() -> Self {
            let root = tempdir().expect("create tempdir");
            let rfcs_path = root.path().join(RFCS_PATH);
            fs::create_dir_all(&rfcs_path).expect("create root");

            fs::create_dir_all(root.path().join(".jiri_root")).expect("create .jiri_root");
            init_file(rfcs_path.join(META_FILE), include_str!("../test_data/rfcs.before.yaml"));
            init_file(rfcs_path.join(TOC_FILE), include_str!("../test_data/toc.before.yaml"));
            init_file(rfcs_path.join(AREAS_FILE), include_str!("../test_data/areas.yaml"));
            init_file(
                rfcs_path.join(TEMPLATE_FILE),
                include_str!("../../../docs/contribute/governance/rfcs/TEMPLATE.md"),
            );
            Self { root, rfcs_path }
        }

        pub fn some_path(&self) -> PathBuf {
            let path = self.root.path().join("some/path");
            fs::create_dir_all(&path).expect("create some path");
            path
        }

        pub fn validate_rfcs_file(&self, file: &str, expected: &str) {
            let path = self.rfcs_path.join(file);
            let mut file = File::open(&path).expect(&format!("open: {}", path.display()));
            let mut contents = String::new();
            file.read_to_string(&mut contents).expect(&format!("read file: {}", path.display()));
            assert_eq!(contents, expected);
        }
    }

    fn init_file(path: PathBuf, contents: &str) {
        let mut file = File::create(path).expect("created file");
        file.write_all(contents.as_bytes()).expect("wrote file");
    }

    #[test]
    fn empty_args() {
        assert_eq!(CreateRfcArgs::from_args(CMD_NAME, &[]), Ok(CreateRfcArgs::default()))
    }

    #[test]
    fn args_with_options() {
        assert_eq!(
            CreateRfcArgs::from_args(
                CMD_NAME,
                &[
                    "--title",
                    "Back to the Fuchsia",
                    "--short-description",
                    "Fuchsia is a modern OS",
                    "--author",
                    "foo@google.com",
                    "--author",
                    "bar@google.com",
                    "--area",
                    "Pink",
                    "--issue",
                    "1234",
                    "--issue",
                    "5678",
                    "--reviewer",
                    "baz@google.com",
                ]
            ),
            Ok(CreateRfcArgs {
                title: Some("Back to the Fuchsia".to_string()),
                short_description: Some("Fuchsia is a modern OS".to_string()),
                authors: vec!["foo@google.com".to_string(), "bar@google.com".to_string(),],
                areas: vec!["Pink".to_string(),],
                issues: vec!["1234".to_string(), "5678".to_string(),],
                reviewers: vec!["baz@google.com".to_string()]
            })
        );
    }

    #[test]
    fn cli_with_all_optional_args() {
        let dir = FakeDir::new();

        // When the CLI receives all parameters through arguments, we don't expect any input.
        let stdin = b"";
        let mut stdout = Vec::<u8>::new();
        let args = CreateRfcArgs {
            title: Some("Back to the Fuchsia".to_string()),
            short_description: Some("Fuchsia is a modern OS".to_string()),
            authors: vec!["foo@google.com".to_string(), "bar@google.com".to_string()],
            areas: vec!["Pink".to_string()],
            issues: vec!["5678".to_string(), "1234".to_string()],
            reviewers: vec!["quux@google.com".to_string(), "baz@google.com".to_string()],
        };
        rfc_impl(
            CommandLine {
                args,
                writer: &mut stdout,
                reader: &stdin[..],
                author_email_provider: FakeEmailProvider {},
            },
            // Call this command from somewhere inside the fake fuchsia platform tree.
            dir.some_path(),
        )
        .expect("succeeds");

        // We don't expect any output when the CLI receives all parameters through arguments.
        assert!(stdout.is_empty());

        dir.validate_rfcs_file(META_FILE, include_str!("../test_data/rfcs.golden.yaml"));
        dir.validate_rfcs_file(TOC_FILE, include_str!("../test_data/toc.golden.yaml"));
        dir.validate_rfcs_file(
            "NNNN_back_to_the_fuchsia.md",
            include_str!("../test_data/rfc.golden.md"),
        );
    }

    #[test]
    fn cli_with_no_optional_args() {
        let dir = FakeDir::new();

        let stdin = vec![
            "Back to the Fuchsia",
            "Fuchsia is a modern OS",
            "foo@google.com,bar@google.com",
            "1",
            "5678, 1234",
            "quux@google.com, baz@google.com",
        ]
        .join("\n")
        .into_bytes();

        let mut stdout = Vec::<u8>::new();
        let args = CreateRfcArgs::default();
        rfc_impl(
            CommandLine {
                args,
                writer: &mut stdout,
                reader: &stdin[..],
                author_email_provider: FakeEmailProvider {},
            },
            dir.root.path().to_path_buf(),
        )
        .expect("succeeds");

        assert_eq!(
            std::str::from_utf8(&stdout).expect("stdout bytes"),
            vec![
                "Title: ",
                "Short description: ",
                "Authors (comma-separated) [default: fuchsia-hacker@google.com]: ",
                "[ 0] Magenta\n[ 1] Pink\n[ 2] Purple\n",
                "Area (comma-separated numbers): ",
                "Monorail issue (comma-separated): ",
                "Reviewers (comma-separated emails): "
            ]
            .join("")
        );

        dir.validate_rfcs_file(META_FILE, include_str!("../test_data/rfcs.golden.yaml"));
        dir.validate_rfcs_file(TOC_FILE, include_str!("../test_data/toc.golden.yaml"));
        dir.validate_rfcs_file(
            "NNNN_back_to_the_fuchsia.md",
            include_str!("../test_data/rfc.golden.md"),
        );
    }

    #[test]
    fn areas_are_validated_from_stdin() {
        let stdin = b"2";
        let mut stdout = Vec::<u8>::new();
        let args = CreateRfcArgs::default();
        let mut cli = CommandLine {
            args,
            writer: &mut stdout,
            reader: &stdin[..],
            author_email_provider: FakeEmailProvider {},
        };
        assert!(cli.get_areas(vec!["A".to_string(), "B".to_string()]).is_err());
    }

    #[test]
    fn areas_are_validated_from_args() {
        let stdin = b"1";
        let mut stdout = Vec::<u8>::new();
        let mut args = CreateRfcArgs::default();
        args.areas = vec!["C".to_string()];
        let mut cli = CommandLine {
            args,
            writer: &mut stdout,
            reader: &stdin[..],
            author_email_provider: FakeEmailProvider {},
        };
        assert_eq!(
            cli.get_areas(vec!["A".to_string(), "B".to_string()]).expect("got areas"),
            vec!["B".to_string()]
        );
        assert_eq!(
            std::str::from_utf8(&stdout).expect("valid stdout"),
            vec![
                "Invalid areas provided. Please select valid ones.",
                "[ 0] A",
                "[ 1] B",
                "Area (comma-separated numbers): ",
            ]
            .join("\n")
        );
    }

    #[test]
    fn default_author_is_set_when_no_author_given() {
        let stdin = b"";
        let mut stdout = Vec::<u8>::new();
        let mut cli = CommandLine {
            args: CreateRfcArgs::default(),
            writer: &mut stdout,
            reader: &stdin[..],
            author_email_provider: FakeEmailProvider {},
        };
        assert_eq!(
            cli.get_authors().expect("got authors"),
            vec!["fuchsia-hacker@google.com".to_string()]
        );
        assert_eq!(
            std::str::from_utf8(&stdout).expect("valid stdout"),
            "Authors (comma-separated) [default: fuchsia-hacker@google.com]: "
        );
    }
}
