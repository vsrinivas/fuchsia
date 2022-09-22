// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    argh::{FromArgs, SubCommand},
    ffx_core::ffx_command,
    shell_args::{derive_subcommand, valid_when},
    std::collections::HashMap,
};

// This file describes two argh-style argument parsing schemas. The first, `FuzzCommand`, is for
// commands provided at the command line. The second, `FuzzShellCommand`, is for commands provided
// via the interactive shell. This shell is stateful, and so many `FuzzShellSubcommand`s correspond
// to a `FuzzSubcommand` that includes explicit options that are implicitly provided by the shell.
//
// For example, the `TrySubcommand` struct has a `url` field for the fuzzer to attach to, but the
// `TryShellSubcommand` does not. In the latter case, the `url` is implied by the shell's state
// after running an `AttachShellSubcommand`. The standalone commands that correspond to shell
// commands are automatically generated via the `derive_subcommand` macro.
//
// Each shell command may only be valid for certain states, as enumerated by `FuzzerState`. These
// are enforced using the `valid_when` macro.
//
// The commands include additional "autocomplete" information to be used by the `rustyline`
// crate. This allows users to get suggestions for commands and parameters by entering <tab> when
// using the interactive shell.
//
// The file is structured as follows:
//  * Top-level command structs and subcommand enums for both standalone and shell commands.
//  * A `Session` enum that represents a list of shell commands and function to convert standalone
//    commands to sessions.
//  * An `Autocomplete` trait to represent what parameters and values the shell should suggest.
//  * Individual shell commands with `AutoComplete` implementations.

/// Command line arguments for a command run from the command-line.
#[ffx_command()]
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "fuzz", description = "Start and manage fuzzers.")]
pub struct FuzzCommand {
    /// command to execute
    #[argh(subcommand)]
    pub command: FuzzSubcommand,
}

/// Individual subcommands that can be run from the command line.
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand)]
pub enum FuzzSubcommand {
    Shell(ShellSubcommand),
    List(ListSubcommand),
    Get(GetSubcommand),
    Set(SetSubcommand),
    Add(AddSubcommand),
    Try(TrySubcommand),
    Run(RunSubcommand),
    Cleanse(CleanseSubcommand),
    Minimize(MinimizeSubcommand),
    Merge(MergeSubcommand),
    Status(StatusSubcommand),
    Fetch(FetchSubcommand),
    Stop(StopSubcommand),
}

/// Command line arguments for a command run as part of the fuzzer shell.
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(description = "Run interactive fuzzer shell commands.")]
pub struct FuzzShellCommand {
    /// command to execute
    #[argh(subcommand)]
    pub command: FuzzShellSubcommand,
}

/// Individual subcommands that can be run in the interactive shell.
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand)]
pub enum FuzzShellSubcommand {
    List(ListSubcommand),
    Attach(AttachShellSubcommand),
    Get(GetShellSubcommand),
    Set(SetShellSubcommand),
    Add(AddShellSubcommand),
    Try(TryShellSubcommand),
    Run(RunShellSubcommand),
    Cleanse(CleanseShellSubcommand),
    Minimize(MinimizeShellSubcommand),
    Merge(MergeShellSubcommand),
    Resume(ResumeShellSubcommand),
    Status(StatusShellSubcommand),
    Fetch(FetchShellSubcommand),
    Detach(DetachShellSubcommand),
    Stop(StopSubcommand),
    Exit(ExitShellSubcommand),
    Clear(ClearShellSubcommand),
    History(HistoryShellSubcommand),
}

/// Describes a sequence of commands to run consecutively.
///
/// These commands may be known in advance, or may be entered interactively by a user.
#[derive(Clone, Debug, PartialEq)]
pub enum Session {
    Interactive(Option<String>),
    Quiet(Vec<FuzzShellCommand>),
    Verbose(Vec<FuzzShellCommand>),
}

impl FuzzCommand {
    /// Converts a standalone command into an equivalent `Session` of shell commands.
    pub fn as_session(&self) -> Session {
        let mut commands = Vec::new();
        if let Some(attach) = AttachShellSubcommand::from(self) {
            commands.push(FuzzShellSubcommand::Attach(attach));
        }
        let (quiet, command) = match self.command.clone() {
            FuzzSubcommand::Shell(cmd) => return Session::Interactive(cmd.json_file),
            FuzzSubcommand::List(cmd) => (false, FuzzShellSubcommand::List(cmd)),
            FuzzSubcommand::Get(cmd) => (cmd.quiet, FuzzShellSubcommand::Get(cmd.into())),
            FuzzSubcommand::Set(cmd) => (cmd.quiet, FuzzShellSubcommand::Set(cmd.into())),
            FuzzSubcommand::Add(cmd) => (cmd.quiet, FuzzShellSubcommand::Add(cmd.into())),
            FuzzSubcommand::Try(cmd) => (cmd.quiet, FuzzShellSubcommand::Try(cmd.into())),
            FuzzSubcommand::Run(cmd) => (cmd.quiet, FuzzShellSubcommand::Run(cmd.into())),
            FuzzSubcommand::Cleanse(cmd) => (cmd.quiet, FuzzShellSubcommand::Cleanse(cmd.into())),
            FuzzSubcommand::Minimize(cmd) => (cmd.quiet, FuzzShellSubcommand::Minimize(cmd.into())),
            FuzzSubcommand::Merge(cmd) => (cmd.quiet, FuzzShellSubcommand::Merge(cmd.into())),
            FuzzSubcommand::Status(cmd) => (cmd.quiet, FuzzShellSubcommand::Status(cmd.into())),
            FuzzSubcommand::Fetch(cmd) => (cmd.quiet, FuzzShellSubcommand::Fetch(cmd.into())),
            FuzzSubcommand::Stop(cmd) => (cmd.quiet, FuzzShellSubcommand::Stop(cmd.into())),
        };
        commands.push(command);
        commands.push(FuzzShellSubcommand::Exit(ExitShellSubcommand {}));
        let commands: Vec<FuzzShellCommand> =
            commands.into_iter().map(|command| FuzzShellCommand { command }).collect();
        match quiet {
            true => Session::Quiet(commands),
            false => Session::Verbose(commands),
        }
    }
}

/// Command to start a fuzzer shell.
///
/// This command is a bit special, as it is the only one unique to `FuzzCommand`. All other
/// subcommands can be converted to `FuzzShellCommand`s using `FuzzCommand::as_shell`.
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "shell", description = "Starts an interactive fuzzing session.")]
pub struct ShellSubcommand {
    /// path to JSON file describing fuzzers; looks for tests.json under $FUCHSIA_DIR by default
    #[argh(option, short = 'j')]
    pub json_file: Option<String>,
}

/// Types of parameters that can be auto-completed.
///
/// `argh` does not provide quite enough information for `rustyline` to perform semantically aware
/// autocompletion of flags and parameters. The following enum and trait provide a way to specify
/// that information while limiting duplication and keeping the locality of what is duplicated as
/// high as possible. See also autocomplete.rs, which uses and tests this enum and trait.
#[derive(Clone, Copy, Debug)]
pub enum ParameterType {
    Any,
    Input,
    Opt,
    Path,
    Url,
}

/// Specifies how to suggest autocompletions for a given command.
pub trait Autocomplete: SubCommand {
    const POSITIONAL_TYPES: &'static [ParameterType];
    const OPTION_TYPES: &'static [(&'static str, Option<ParameterType>)];

    /// Provides argument types that can be used to generate suggestiong when auto-completing.
    ///
    /// Adds this objects positional parameter types and options types to `positional_types` and
    /// `option_types`, respectively, if and only if the given `command` matches this objects
    /// command name.
    fn autocomplete(
        command: &str,
        positional_types: &mut Vec<ParameterType>,
        option_types: &mut HashMap<String, Option<ParameterType>>,
    ) {
        if command == Self::COMMAND.name {
            positional_types.extend(Self::POSITIONAL_TYPES.iter().copied());
            for (option, option_type) in Self::OPTION_TYPES.iter() {
                option_types.insert(option.to_string(), option_type.clone());
            }
        }
    }
}

/// Indicates the state of the fuzzer that the shell is connected to, if any.
///
/// See also the `valid_when` macro.
#[derive(Copy, Clone, Debug, PartialEq)]
pub enum FuzzerState {
    // No fuzzer is connected.
    Detached,

    // A fuzzer is connected and waiting for next command.
    Idle,

    // A fuzzer is performing a long-running workflow, e.g. fuzzing.
    Running,
}

/// Command to list available fuzzers.
///
/// 'list' lacks a URL parameter, so it can be interpreted by either a `FuzzCommand` or a
/// `FuzzShellCommand` without modification.
#[valid_when(FuzzerState::Detached, FuzzerState::Idle, FuzzerState::Running)]
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "list", description = "Lists available fuzzers.")]
pub struct ListSubcommand {
    /// path to JSON file describing fuzzers; looks for tests.json under $FUCHSIA_DIR by default
    #[argh(option, short = 'j')]
    pub json_file: Option<String>,

    /// list all fuzzers matching shell-style glob pattern; default is to list all fuzzers
    #[argh(option, short = 'p')]
    pub pattern: Option<String>,
}

impl Autocomplete for ListSubcommand {
    const POSITIONAL_TYPES: &'static [ParameterType] = &[];
    const OPTION_TYPES: &'static [(&'static str, Option<ParameterType>)] =
        &[("--json-file", Some(ParameterType::Path)), ("--pattern", Some(ParameterType::Any))];
}

/// Command to start and/or attach to a fuzzer on a device.
///
/// The parameters for 'attach' MUST be a subset of those generated by the `derive_subcommand`
/// proc_macro in order to be able to extract an `AttachShellSubcommand` from a `FuzzCommand` using
/// its custom `From` trait implementation below. See also `FuzzCommand::as_session`.
#[valid_when(FuzzerState::Detached)]
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "attach", description = "Connects to a fuzzer, starting it if needed.")]
pub struct AttachShellSubcommand {
    /// package URL for the fuzzer
    #[argh(positional)]
    pub url: String,

    /// where to send fuzzer logs and artifacts; overrides `fuzzer.output` config value if present
    #[argh(option, short = 'o')]
    pub output: Option<String>,
}

impl Autocomplete for AttachShellSubcommand {
    const POSITIONAL_TYPES: &'static [ParameterType] = &[ParameterType::Url];
    const OPTION_TYPES: &'static [(&'static str, Option<ParameterType>)] =
        &[("--output", Some(ParameterType::Path))];
}

impl AttachShellSubcommand {
    fn from(args: &FuzzCommand) -> Option<Self> {
        let (url, output) = match &args.command {
            FuzzSubcommand::Get(GetSubcommand { url, output, .. }) => (url, output),
            FuzzSubcommand::Set(SetSubcommand { url, output, .. }) => (url, output),
            FuzzSubcommand::Add(AddSubcommand { url, output, .. }) => (url, output),
            FuzzSubcommand::Try(TrySubcommand { url, output, .. }) => (url, output),
            FuzzSubcommand::Run(RunSubcommand { url, output, .. }) => (url, output),
            FuzzSubcommand::Cleanse(CleanseSubcommand { url, output, .. }) => (url, output),
            FuzzSubcommand::Minimize(MinimizeSubcommand { url, output, .. }) => (url, output),
            FuzzSubcommand::Merge(MergeSubcommand { url, output, .. }) => (url, output),
            FuzzSubcommand::Status(StatusSubcommand { url, output, .. }) => (url, output),
            FuzzSubcommand::Fetch(FetchSubcommand { url, output, .. }) => (url, output),
            _ => return None,
        };
        Some(Self { url: url.to_string(), output: output.clone() })
    }
}

/// Command to get a configuration option from a fuzzer.
#[valid_when(FuzzerState::Idle, FuzzerState::Running)]
#[derive_subcommand]
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "get", description = "Gets option(s) from a fuzzer.")]
pub struct GetShellSubcommand {
    /// option name; default is to display all values
    #[argh(positional)]
    pub name: Option<String>,
}

impl Autocomplete for GetShellSubcommand {
    const POSITIONAL_TYPES: &'static [ParameterType] = &[ParameterType::Opt];
    const OPTION_TYPES: &'static [(&'static str, Option<ParameterType>)] = &[];
}

/// Command to set a configuration option on a fuzzer.
///
/// Values for options which take a size can be written as <N>[units], where N is a number and
/// units is one of 'gb', 'mb' (the default), 'kb', or 'b'.
///
/// Values for options which take a time can be written as <N>[units], where N is a number and
/// units is one of 'd', 'h', 's' (the default), 'ms', 'us' or 'ns'.
///
#[valid_when(FuzzerState::Idle)]
#[derive_subcommand]
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "set", description = "Sets options on a fuzzer.")]
pub struct SetShellSubcommand {
    /// option name
    #[argh(positional)]
    pub name: String,

    /// option value
    #[argh(positional)]
    pub value: String,
}

impl Autocomplete for SetShellSubcommand {
    const POSITIONAL_TYPES: &'static [ParameterType] = &[ParameterType::Opt, ParameterType::Any];
    const OPTION_TYPES: &'static [(&'static str, Option<ParameterType>)] = &[];
}

/// Command to add a test input to a fuzzer's corpus.
#[valid_when(FuzzerState::Idle, FuzzerState::Running)]
#[derive_subcommand]
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "add", description = "Adds an input to a fuzzer's corpus.")]
pub struct AddShellSubcommand {
    /// input(s) to add; may be a filename, a directory path or a hex string
    #[argh(positional)]
    pub input: String,

    /// add to the seed corpus; default is to add to live corpus
    #[argh(switch, short = 's')]
    pub seed: bool,
}

impl Autocomplete for AddShellSubcommand {
    const POSITIONAL_TYPES: &'static [ParameterType] = &[ParameterType::Input];
    const OPTION_TYPES: &'static [(&'static str, Option<ParameterType>)] = &[("--seed", None)];
}

/// Command to try a test input using the fuzzer.
///
/// This is a long-running workflow and may take an indefinite amount of time to complete.
#[valid_when(FuzzerState::Idle)]
#[derive_subcommand]
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "try", description = "Tests a specific input with a fuzzer.")]
pub struct TryShellSubcommand {
    /// input to execute; may be a filename or hex string
    #[argh(positional)]
    pub input: String,
}

impl Autocomplete for TryShellSubcommand {
    const POSITIONAL_TYPES: &'static [ParameterType] = &[ParameterType::Input];
    const OPTION_TYPES: &'static [(&'static str, Option<ParameterType>)] = &[];
}

/// Command to generate and test inputs using the fuzzer.
///
/// This is a long-running workflow and may take an indefinite amount of time to complete.
#[valid_when(FuzzerState::Idle)]
#[derive_subcommand]
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "run", description = "Generates inputs and fuzz the target.")]
pub struct RunShellSubcommand {
    /// convenient shortcut for "set runs <n>"
    #[argh(option, short = 'r')]
    pub runs: Option<String>,

    /// convenient shortcut for "set max_total_time <n>"
    #[argh(option, short = 't')]
    pub time: Option<String>,
}

impl Autocomplete for RunShellSubcommand {
    const POSITIONAL_TYPES: &'static [ParameterType] = &[];
    const OPTION_TYPES: &'static [(&'static str, Option<ParameterType>)] =
        &[("--runs", Some(ParameterType::Any)), ("--time", Some(ParameterType::Any))];
}

/// Command to try and clear bytes from a given input without changing the fuzzer error it causes.
///
/// This is a long-running workflow and may take an indefinite amount of time to complete.
#[valid_when(FuzzerState::Idle)]
#[derive_subcommand]
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "cleanse", description = "Clears extraneous bytes from an error input.")]
pub struct CleanseShellSubcommand {
    /// input to cleanse.
    #[argh(positional)]
    pub input: String,
}

impl Autocomplete for CleanseShellSubcommand {
    const POSITIONAL_TYPES: &'static [ParameterType] = &[ParameterType::Input];
    const OPTION_TYPES: &'static [(&'static str, Option<ParameterType>)] = &[];
}

/// Command to try and remove bytes from a given input without changing the fuzzer error it causes.
///
/// This is a long-running workflow and may take an indefinite amount of time to complete.
#[valid_when(FuzzerState::Idle)]
#[derive_subcommand]
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "minimize", description = "Reduce the size of an error input.")]
pub struct MinimizeShellSubcommand {
    /// input to minimize.
    #[argh(positional)]
    pub input: String,

    /// convenient shortcut for "set runs <n>"
    #[argh(option, short = 'r')]
    pub runs: Option<String>,

    /// convenient shortcut for "set max_total_time <n>"
    #[argh(option, short = 't')]
    pub time: Option<String>,
}

impl Autocomplete for MinimizeShellSubcommand {
    const POSITIONAL_TYPES: &'static [ParameterType] = &[ParameterType::Input];
    const OPTION_TYPES: &'static [(&'static str, Option<ParameterType>)] =
        &[("--runs", Some(ParameterType::Any)), ("--time", Some(ParameterType::Any))];
}

/// Command to try and combine corpus inputs while preserving fuzzer coverage.
///
/// This is a long-running workflow and may take an indefinite amount of time to complete.
#[valid_when(FuzzerState::Idle)]
#[derive_subcommand]
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "merge", description = "Compacts the attached fuzzer's corpus.")]
pub struct MergeShellSubcommand {}

impl Autocomplete for MergeShellSubcommand {
    const POSITIONAL_TYPES: &'static [ParameterType] = &[];
    const OPTION_TYPES: &'static [(&'static str, Option<ParameterType>)] = &[];
}

/// Command to resume paused fuzzer output.
///
/// Fuzzer output may be paused for a running fuzzer when re-attaching or by the user.
#[valid_when(FuzzerState::Running)]
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "resume", description = "Resumes a fuzzer's output.")]
pub struct ResumeShellSubcommand {}

impl Autocomplete for ResumeShellSubcommand {
    const POSITIONAL_TYPES: &'static [ParameterType] = &[];
    const OPTION_TYPES: &'static [(&'static str, Option<ParameterType>)] = &[];
}

/// Command to retrieve fuzzer status.
#[valid_when(FuzzerState::Detached, FuzzerState::Idle, FuzzerState::Running)]
#[derive_subcommand]
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "status", description = "Gets a fuzzer's execution status.")]
pub struct StatusShellSubcommand {}

impl Autocomplete for StatusShellSubcommand {
    const POSITIONAL_TYPES: &'static [ParameterType] = &[];
    const OPTION_TYPES: &'static [(&'static str, Option<ParameterType>)] = &[];
}

/// Command to retrieve a test input from a fuzzer's corpus.
#[valid_when(FuzzerState::Idle, FuzzerState::Running)]
#[derive_subcommand]
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "fetch", description = "Retrieves the attached fuzzer's corpus.")]
pub struct FetchShellSubcommand {
    /// fetch the seed corpus; default is to fetch the live corpus
    #[argh(switch, short = 's')]
    pub seed: bool,
}

impl Autocomplete for FetchShellSubcommand {
    const POSITIONAL_TYPES: &'static [ParameterType] = &[];
    const OPTION_TYPES: &'static [(&'static str, Option<ParameterType>)] = &[("--seed", None)];
}

/// Command to detach from a fuzzer without stopping it.
#[valid_when(FuzzerState::Idle, FuzzerState::Running)]
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "detach", description = "Disconnects from a fuzzer without stopping it.")]
pub struct DetachShellSubcommand {}

impl Autocomplete for DetachShellSubcommand {
    const POSITIONAL_TYPES: &'static [ParameterType] = &[];
    const OPTION_TYPES: &'static [(&'static str, Option<ParameterType>)] = &[];
}

/// Command to stop a fuzzer.
#[valid_when(FuzzerState::Detached, FuzzerState::Idle, FuzzerState::Running)]
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "stop", description = "Stops a fuzzer.")]
pub struct StopSubcommand {
    /// package URL for the fuzzer. The attached fuzzer is stopped by default.
    #[argh(positional)]
    pub url: Option<String>,

    /// if present, suppress non-error output from the ffx tool itself
    #[argh(switch, short = 'q')]
    pub quiet: bool,
}

impl Autocomplete for StopSubcommand {
    const POSITIONAL_TYPES: &'static [ParameterType] = &[ParameterType::Url];
    const OPTION_TYPES: &'static [(&'static str, Option<ParameterType>)] = &[("--quiet", None)];
}

/// Command to stop the fuzzer shell.
#[valid_when(FuzzerState::Detached, FuzzerState::Idle, FuzzerState::Running)]
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "exit", description = "Disconnects from a fuzzer and exits the shell.")]
pub struct ExitShellSubcommand {}

impl Autocomplete for ExitShellSubcommand {
    const POSITIONAL_TYPES: &'static [ParameterType] = &[];
    const OPTION_TYPES: &'static [(&'static str, Option<ParameterType>)] = &[];
}

/// Command to clear the fuzzer shell's screen.
#[valid_when(FuzzerState::Detached, FuzzerState::Idle, FuzzerState::Running)]
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "clear", description = "Clears the screen.")]
pub struct ClearShellSubcommand {}

impl Autocomplete for ClearShellSubcommand {
    const POSITIONAL_TYPES: &'static [ParameterType] = &[];
    const OPTION_TYPES: &'static [(&'static str, Option<ParameterType>)] = &[];
}

/// Command to list recent commands to the fuzzer shell.
#[valid_when(FuzzerState::Detached, FuzzerState::Idle, FuzzerState::Running)]
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "history", description = "Prints the command history for the shell.")]
pub struct HistoryShellSubcommand {}

impl Autocomplete for HistoryShellSubcommand {
    const POSITIONAL_TYPES: &'static [ParameterType] = &[];
    const OPTION_TYPES: &'static [(&'static str, Option<ParameterType>)] = &[];
}

#[cfg(test)]
mod test_fixtures {
    use super::*;

    const CMD_NAME: &'static [&'static str] = &["fuzz"];
    pub const TEST_URL: &str = "fuchsia-pkg://fuchsia.com/fake#meta/foo-fuzzer.cm";

    /// Creates a sequence of commands like "attach", `cmd`, "exit".
    pub fn create_session_commands(
        cmd: &FuzzShellSubcommand,
        output: Option<&str>,
    ) -> Vec<FuzzShellCommand> {
        shell_cmds(vec![
            FuzzShellSubcommand::Attach(AttachShellSubcommand {
                url: TEST_URL.to_string(),
                output: output.and_then(|s| Some(s.to_string())),
            }),
            cmd.clone(),
            FuzzShellSubcommand::Exit(ExitShellSubcommand {}),
        ])
    }

    /// Converts a vector of `FuzzShellSubcommand`s to a vector of `FuzzShellCommand`s.
    pub fn shell_cmds(shell_subcommands: Vec<FuzzShellSubcommand>) -> Vec<FuzzShellCommand> {
        shell_subcommands.into_iter().map(|cmd| FuzzShellCommand { command: cmd }).collect()
    }

    /// Parses a command line into a session of `FuzzShellCommands`.
    pub fn get_session<S: AsRef<str>>(cmdline: S) -> Session {
        let args: Vec<&str> = cmdline.as_ref().split(" ").collect();
        let command = FuzzCommand::from_args(CMD_NAME, &args).expect("invalid command line");
        command.as_session()
    }
}

#[cfg(test)]
mod tests {
    use {super::test_fixtures::*, super::*};

    #[fuchsia::test]
    async fn test_shell() {
        assert_eq!(get_session("shell"), Session::Interactive(None));
        assert_eq!(get_session("shell -j foo"), Session::Interactive(Some("foo".to_string())));
        assert_eq!(
            get_session("shell --json-file bar"),
            Session::Interactive(Some("bar".to_string()))
        );
    }

    #[fuchsia::test]
    async fn test_list() {
        let expected = shell_cmds(vec![
            FuzzShellSubcommand::List(ListSubcommand { json_file: None, pattern: None }),
            FuzzShellSubcommand::Exit(ExitShellSubcommand {}),
        ]);
        assert_eq!(get_session("list"), Session::Verbose(expected));

        let expected = shell_cmds(vec![
            FuzzShellSubcommand::List(ListSubcommand {
                json_file: Some("foo".to_string()),
                pattern: None,
            }),
            FuzzShellSubcommand::Exit(ExitShellSubcommand {}),
        ]);
        assert_eq!(get_session("list -j foo"), Session::Verbose(expected));

        let expected = shell_cmds(vec![
            FuzzShellSubcommand::List(ListSubcommand {
                json_file: Some("bar".to_string()),
                pattern: None,
            }),
            FuzzShellSubcommand::Exit(ExitShellSubcommand {}),
        ]);
        assert_eq!(get_session("list --json-file bar"), Session::Verbose(expected));

        let expected = shell_cmds(vec![
            FuzzShellSubcommand::List(ListSubcommand {
                json_file: None,
                pattern: Some("foo".to_string()),
            }),
            FuzzShellSubcommand::Exit(ExitShellSubcommand {}),
        ]);
        assert_eq!(get_session("list -p foo"), Session::Verbose(expected));

        let expected = shell_cmds(vec![
            FuzzShellSubcommand::List(ListSubcommand {
                json_file: None,
                pattern: Some("bar".to_string()),
            }),
            FuzzShellSubcommand::Exit(ExitShellSubcommand {}),
        ]);
        assert_eq!(get_session("list --pattern bar"), Session::Verbose(expected));
    }

    #[fuchsia::test]
    async fn test_get() {
        let cmdline = format!("get {}", TEST_URL);
        let cmd = FuzzShellSubcommand::Get(GetShellSubcommand { name: None });
        let expected = create_session_commands(&cmd, None);
        assert_eq!(get_session(cmdline), Session::Verbose(expected));

        let cmdline = format!("get --output path -q {} key", TEST_URL);
        let cmd = FuzzShellSubcommand::Get(GetShellSubcommand { name: Some("key".to_string()) });
        let expected = create_session_commands(&cmd, Some("path"));
        assert_eq!(get_session(cmdline), Session::Quiet(expected.clone()));

        let cmdline = format!("get {} --quiet key -o path", TEST_URL);
        assert_eq!(get_session(cmdline), Session::Quiet(expected));
    }

    #[fuchsia::test]
    async fn test_set() {
        let cmdline = format!("set {} foo bar", TEST_URL);
        let cmd = FuzzShellSubcommand::Set(SetShellSubcommand {
            name: "foo".to_string(),
            value: "bar".to_string(),
        });
        let expected = create_session_commands(&cmd, None);
        assert_eq!(get_session(cmdline), Session::Verbose(expected));

        let cmdline = format!("set --output path -q {} foo bar", TEST_URL);
        let expected = create_session_commands(&cmd, Some("path"));
        assert_eq!(get_session(cmdline), Session::Quiet(expected.clone()));

        let cmdline = format!("set {} --quiet foo -o path bar", TEST_URL);
        assert_eq!(get_session(cmdline), Session::Quiet(expected));
    }

    #[fuchsia::test]
    async fn test_add() {
        let cmdline = format!("add {} foo", TEST_URL);
        let cmd =
            FuzzShellSubcommand::Add(AddShellSubcommand { input: "foo".to_string(), seed: false });
        let expected = create_session_commands(&cmd, None);
        assert_eq!(get_session(cmdline), Session::Verbose(expected));

        let cmdline = format!("add --output path -q --seed {} bar", TEST_URL);
        let cmd =
            FuzzShellSubcommand::Add(AddShellSubcommand { input: "bar".to_string(), seed: true });
        let expected = create_session_commands(&cmd, Some("path"));
        assert_eq!(get_session(cmdline), Session::Quiet(expected.clone()));

        let cmdline = format!("add {} -s bar --quiet -o path", TEST_URL);
        assert_eq!(get_session(cmdline), Session::Quiet(expected));
    }

    #[fuchsia::test]
    async fn test_try() {
        let cmdline = format!("try {} foo", TEST_URL);
        let cmd = FuzzShellSubcommand::Try(TryShellSubcommand { input: "foo".to_string() });
        let expected = create_session_commands(&cmd, None);
        assert_eq!(get_session(cmdline), Session::Verbose(expected));

        let cmdline = format!("try --output path -q {} foo", TEST_URL);
        let expected = create_session_commands(&cmd, Some("path"));
        assert_eq!(get_session(cmdline), Session::Quiet(expected.clone()));

        let cmdline = format!("try {} --quiet foo -o path", TEST_URL);
        assert_eq!(get_session(cmdline), Session::Quiet(expected));
    }

    #[fuchsia::test]
    async fn test_run() {
        let cmdline = format!("run {}", TEST_URL);
        let cmd = FuzzShellSubcommand::Run(RunShellSubcommand { runs: None, time: None });
        let expected = create_session_commands(&cmd, None);
        assert_eq!(get_session(cmdline), Session::Verbose(expected));

        let cmdline = format!("run --output path -q --runs 10 -t 10s {}", TEST_URL);
        let cmd = FuzzShellSubcommand::Run(RunShellSubcommand {
            runs: Some("10".to_string()),
            time: Some("10s".to_string()),
        });
        let expected = create_session_commands(&cmd, Some("path"));
        assert_eq!(get_session(cmdline), Session::Quiet(expected.clone()));

        let cmdline = format!("run {} --time 10s -r 10 --quiet -o path", TEST_URL);
        assert_eq!(get_session(cmdline), Session::Quiet(expected));
    }

    #[fuchsia::test]
    async fn test_cleanse() {
        let cmdline = format!("cleanse {} foo", TEST_URL);
        let cmd = FuzzShellSubcommand::Cleanse(CleanseShellSubcommand { input: "foo".to_string() });
        let expected = create_session_commands(&cmd, None);
        assert_eq!(get_session(cmdline), Session::Verbose(expected));

        let cmdline = format!("cleanse --output path -q {} foo", TEST_URL);
        let expected = create_session_commands(&cmd, Some("path"));
        assert_eq!(get_session(cmdline), Session::Quiet(expected.clone()));

        let cmdline = format!("cleanse {} --quiet foo -o path", TEST_URL);
        assert_eq!(get_session(cmdline), Session::Quiet(expected));
    }

    #[fuchsia::test]
    async fn test_minimize() {
        let cmdline = format!("minimize {} foo", TEST_URL);
        let cmd = FuzzShellSubcommand::Minimize(MinimizeShellSubcommand {
            input: "foo".to_string(),
            runs: None,
            time: None,
        });
        let expected = create_session_commands(&cmd, None);
        assert_eq!(get_session(cmdline), Session::Verbose(expected));

        let cmdline = format!("minimize --output path -q --runs 10 -t 10s {} bar", TEST_URL);
        let cmd = FuzzShellSubcommand::Minimize(MinimizeShellSubcommand {
            input: "bar".to_string(),
            runs: Some("10".to_string()),
            time: Some("10s".to_string()),
        });
        let expected = create_session_commands(&cmd, Some("path"));
        assert_eq!(get_session(cmdline), Session::Quiet(expected.clone()));

        let cmdline = format!("minimize {} --time 10s bar -r 10 --quiet -o path", TEST_URL);
        assert_eq!(get_session(cmdline), Session::Quiet(expected));
    }

    #[fuchsia::test]
    async fn test_merge() {
        let cmdline = format!("merge {}", TEST_URL);
        let cmd = FuzzShellSubcommand::Merge(MergeShellSubcommand {});
        let expected = create_session_commands(&cmd, None);
        assert_eq!(get_session(cmdline), Session::Verbose(expected));

        let cmdline = format!("merge --output path -q {}", TEST_URL);
        let cmd = FuzzShellSubcommand::Merge(MergeShellSubcommand {});
        let expected = create_session_commands(&cmd, Some("path"));
        assert_eq!(get_session(cmdline), Session::Quiet(expected.clone()));

        let cmdline = format!("merge {} --quiet -o path", TEST_URL);
        assert_eq!(get_session(cmdline), Session::Quiet(expected));
    }

    #[fuchsia::test]
    async fn test_resume() {
        let cmd = FuzzShellCommand::from_args(&["fuzz"], &["resume"]).expect("failed to parse");
        assert_eq!(cmd.command, FuzzShellSubcommand::Resume(ResumeShellSubcommand {}));
    }

    #[fuchsia::test]
    async fn test_status() {
        let cmdline = format!("status {}", TEST_URL);
        let cmd = FuzzShellSubcommand::Status(StatusShellSubcommand {});
        let expected = create_session_commands(&cmd, None);
        assert_eq!(get_session(cmdline), Session::Verbose(expected));

        let cmdline = format!("status --output path -q {}", TEST_URL);
        let expected = create_session_commands(&cmd, Some("path"));
        assert_eq!(get_session(cmdline), Session::Quiet(expected.clone()));

        let cmdline = format!("status {} --quiet -o path", TEST_URL);
        assert_eq!(get_session(cmdline), Session::Quiet(expected));
    }

    #[fuchsia::test]
    async fn test_fetch() {
        let cmdline = format!("fetch {}", TEST_URL);
        let cmd = FuzzShellSubcommand::Fetch(FetchShellSubcommand { seed: false });
        let expected = create_session_commands(&cmd, None);
        assert_eq!(get_session(cmdline), Session::Verbose(expected));

        let cmdline = format!("fetch --output path -q --seed {}", TEST_URL);
        let cmd = FuzzShellSubcommand::Fetch(FetchShellSubcommand { seed: true });
        let expected = create_session_commands(&cmd, Some("path"));
        assert_eq!(get_session(cmdline), Session::Quiet(expected.clone()));

        let cmdline = format!("fetch {} -s --quiet -o path", TEST_URL);
        assert_eq!(get_session(cmdline), Session::Quiet(expected));
    }

    #[fuchsia::test]
    async fn test_detach() {
        let cmd = FuzzShellCommand::from_args(&["fuzz"], &["detach"]).expect("failed to parse");
        assert_eq!(cmd.command, FuzzShellSubcommand::Detach(DetachShellSubcommand {}));
    }

    #[fuchsia::test]
    async fn test_stop() {
        let cmdline = format!("stop {}", TEST_URL);
        let mut stop = StopSubcommand { url: Some(TEST_URL.to_string()), quiet: false };
        let exit = FuzzShellSubcommand::Exit(ExitShellSubcommand {});
        let expected = shell_cmds(vec![FuzzShellSubcommand::Stop(stop.clone()), exit.clone()]);
        assert_eq!(get_session(cmdline), Session::Verbose(expected));

        let cmdline = format!("stop -q {}", TEST_URL);
        stop.quiet = true;
        let expected = shell_cmds(vec![FuzzShellSubcommand::Stop(stop.clone()), exit.clone()]);
        assert_eq!(get_session(cmdline), Session::Quiet(expected.clone()));

        let cmdline = "stop --quiet";
        stop.url = None;
        let expected = shell_cmds(vec![FuzzShellSubcommand::Stop(stop.clone()), exit.clone()]);
        assert_eq!(get_session(cmdline), Session::Quiet(expected));
    }

    #[fuchsia::test]
    async fn test_exit() {
        let cmd = FuzzShellCommand::from_args(&["fuzz"], &["exit"]).expect("failed to parse");
        assert_eq!(cmd.command, FuzzShellSubcommand::Exit(ExitShellSubcommand {}));
    }

    #[fuchsia::test]
    async fn test_clear() {
        let cmd = FuzzShellCommand::from_args(&["fuzz"], &["clear"]).expect("failed to parse");
        assert_eq!(cmd.command, FuzzShellSubcommand::Clear(ClearShellSubcommand {}));
    }

    #[fuchsia::test]
    async fn test_history() {
        let cmd = FuzzShellCommand::from_args(&["fuzz"], &["history"]).expect("failed to parse");
        assert_eq!(cmd.command, FuzzShellSubcommand::History(HistoryShellSubcommand {}));
    }
}
