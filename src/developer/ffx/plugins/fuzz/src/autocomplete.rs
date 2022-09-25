// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::options,
    crate::util::get_fuzzer_urls,
    anyhow::Result,
    ffx_fuzz_args::*,
    rustyline::completion::{Completer, FilenameCompleter, Pair},
    rustyline::error::ReadlineError,
    rustyline::highlight::Highlighter,
    rustyline::hint::Hinter,
    rustyline::Helper,
    std::borrow::Cow::{self, Borrowed},
    std::cell::RefCell,
    std::collections::BTreeMap,
    std::collections::{HashMap, VecDeque},
    std::sync::{Arc, Mutex},
    std::vec::IntoIter,
};

/// Performs `rustyline`-style autocompletion for the `ffx fuzz` plugin.
pub struct FuzzHelper {
    state: Arc<Mutex<FuzzerState>>,
    tests_json: Option<String>,
    file_completer: FilenameCompleter,
    tokens: RefCell<VecDeque<String>>,
    positional: RefCell<IntoIter<ParameterType>>,
    options: RefCell<HashMap<String, Option<ParameterType>>>,
}

impl Completer for FuzzHelper {
    type Candidate = Pair;
    fn complete(&self, line: &str, _pos: usize) -> Result<(usize, Vec<Pair>), ReadlineError> {
        // First, complete commands.
        let (command, token) = self.split(line);
        let command = match command {
            Some(command) => command,
            None => {
                let mut commands = self.make_commands();
                commands.sort();
                let pairs = find_matching(&token, commands);
                return Ok((line.len(), trim_replacements(&token, pairs)));
            }
        };

        // Next, consume tokens to get the expected type and/or possible completions.
        let expected = self.parse_completed_tokens(&command);
        let (expected, mut completions) = self.parse_incomplete_token(&token, expected);
        completions.extend(self.get_parameter_completions(&expected));
        let mut pairs = find_matching(&token, completions);

        // If the token is expected to be a file or path, use the filename completer.
        match expected {
            Some(ParameterType::Input) | Some(ParameterType::Path) => {
                if let Ok((_, paths)) = self.file_completer.complete(line, line.len()) {
                    pairs.extend(paths);
                }
            }
            _ => {}
        };

        Ok((line.len(), trim_replacements(&token, pairs)))
    }
}

impl FuzzHelper {
    /// Creates a new `FuzzHelper`.
    ///
    /// This helper will use the given `tests_json` to look for fuzzer URLs when providing
    /// suggestions for URL parameters. It will also use the shared fuzzer `state` to provide
    /// suggestions for valid commands.
    pub fn new(tests_json: Option<String>, state: Arc<Mutex<FuzzerState>>) -> Self {
        Self {
            tests_json,
            state,
            file_completer: FilenameCompleter::new(),
            tokens: RefCell::new(VecDeque::new()),
            positional: RefCell::new(Vec::new().into_iter()),
            options: RefCell::new(HashMap::new()),
        }
    }

    // Splits the given line into tokens and returns the command, if complete, and the final,
    // incomplete token, if any.
    fn split(&self, line: &str) -> (Option<String>, Option<String>) {
        let trimmed = line.trim_end();
        let partial = !trimmed.is_empty() && line == trimmed;
        let mut tokens = self.tokens.borrow_mut();
        *tokens = line.split_whitespace().map(|token| token.to_string()).collect();
        let (command, token) = match (tokens.len(), partial) {
            // Incomplete command.
            (0, _) | (1, true) => (None, tokens.pop_front()),

            // Last token is incomplete.
            (_, true) => (tokens.pop_front(), tokens.pop_back()),

            // All tokens are complete.
            (_, false) => (tokens.pop_front(), None),
        };
        (command, token)
    }

    fn make_commands(&self) -> Vec<String> {
        // "help" is added automatically by argh.
        let mut commands = vec!["help".to_string()];
        let state = self.state.lock().unwrap();
        ListSubcommand::add_if_valid(&mut commands, *state);
        AttachShellSubcommand::add_if_valid(&mut commands, *state);
        GetShellSubcommand::add_if_valid(&mut commands, *state);
        SetShellSubcommand::add_if_valid(&mut commands, *state);
        AddShellSubcommand::add_if_valid(&mut commands, *state);
        TryShellSubcommand::add_if_valid(&mut commands, *state);
        RunShellSubcommand::add_if_valid(&mut commands, *state);
        CleanseShellSubcommand::add_if_valid(&mut commands, *state);
        MinimizeShellSubcommand::add_if_valid(&mut commands, *state);
        MergeShellSubcommand::add_if_valid(&mut commands, *state);
        StatusShellSubcommand::add_if_valid(&mut commands, *state);
        FetchShellSubcommand::add_if_valid(&mut commands, *state);
        DetachShellSubcommand::add_if_valid(&mut commands, *state);
        StopSubcommand::add_if_valid(&mut commands, *state);
        ExitShellSubcommand::add_if_valid(&mut commands, *state);
        ClearShellSubcommand::add_if_valid(&mut commands, *state);
        HistoryShellSubcommand::add_if_valid(&mut commands, *state);
        commands
    }

    // Compares completed tokens with a commands positional arguments and options. Matching
    // positional arguments and options are removed, and the expected parameter type, if any, for the
    // incomplete token is returned.
    fn parse_completed_tokens(&self, command: &str) -> Option<ParameterType> {
        let mut expected = None;
        let (positional, mut options) = get_parameter_types(command);
        let mut positional = positional.into_iter();
        let mut tokens = self.tokens.borrow_mut();
        while tokens.len() > 1 {
            let token = tokens.pop_front().unwrap();
            expected = match (options.remove(&token), expected) {
                // Last token was an option that expected a parameter.
                (_, Some(_)) => None,

                // This is an option, which may or may not expect a parameter.
                (Some(completion), _) => completion,

                // Otherwise, this is a positional argument.
                (None, None) => {
                    positional.next();
                    None
                }
            }
        }
        let mut positional_mut = self.positional.borrow_mut();
        let mut options_mut = self.options.borrow_mut();
        *positional_mut = positional;
        *options_mut = options;
        expected
    }

    // Examines the last token and returns an expected parameter type and/or any additional
    // completions.
    fn parse_incomplete_token(
        &self,
        token: &Option<String>,
        expected: Option<ParameterType>,
    ) -> (Option<ParameterType>, Vec<String>) {
        let mut positional = self.positional.borrow_mut();
        let options = self.options.borrow();
        match (token, expected) {
            // No token or hint from a previous token; this could be any valid token.
            (None, None) => (positional.next(), options.keys().map(|s| s.to_string()).collect()),

            // Previous token expected something to follow it.
            (_, Some(expected)) => (Some(expected), Vec::new()),

            // Token starts like an option.
            (Some(token), None) if token.starts_with('-') => {
                (None, options.keys().map(|s| s.to_string()).collect())
            }

            // Not an option; it must be a positional argument if any remain.
            (_, None) => (positional.next(), Vec::new()),
        }
    }

    // Returns completions for different expected parameter types.
    fn get_parameter_completions(&self, expected: &Option<ParameterType>) -> Vec<String> {
        match expected {
            Some(ParameterType::Opt) => options::NAMES.iter().map(|s| s.to_string()).collect(),
            Some(ParameterType::Url) => get_fuzzer_urls(&self.tests_json)
                .unwrap_or(Vec::new())
                .into_iter()
                .map(|s| s.to_string())
                .collect(),
            _ => return Vec::new(),
        }
    }
}

fn find_matching(token: &Option<String>, mut candidates: Vec<String>) -> Vec<Pair> {
    if let Some(token) = token {
        candidates.retain(|command| command.starts_with(token));
    }
    candidates
        .into_iter()
        .map(|command| Pair { display: command.clone(), replacement: command.clone() })
        .collect()
}

fn trim_replacements(token: &Option<String>, pairs: Vec<Pair>) -> Vec<Pair> {
    let mut sorted = BTreeMap::new();
    let token = token.as_ref().map_or("", |t| t);
    for pair in pairs.into_iter() {
        sorted.insert(pair.display, pair.replacement.trim_start_matches(token).to_string());
    }
    sorted.into_iter().map(|(display, replacement)| Pair { display, replacement }).collect()
}

impl Hinter for FuzzHelper {
    fn hint(&self, _line: &str, _pos: usize) -> Option<String> {
        None
    }
}

impl Highlighter for FuzzHelper {
    fn highlight_prompt<'p>(&self, prompt: &'p str) -> Cow<'p, str> {
        Borrowed(prompt)
    }
}

impl Helper for FuzzHelper {}

// Keep this function in sync with the arg types, and verify through tests!
fn get_parameter_types(
    command: &str,
) -> (Vec<ParameterType>, HashMap<String, Option<ParameterType>>) {
    let mut positional = Vec::new();
    let mut longopts = HashMap::new();
    ListSubcommand::autocomplete(command, &mut positional, &mut longopts);
    AttachShellSubcommand::autocomplete(command, &mut positional, &mut longopts);
    GetShellSubcommand::autocomplete(command, &mut positional, &mut longopts);
    SetShellSubcommand::autocomplete(command, &mut positional, &mut longopts);
    AddShellSubcommand::autocomplete(command, &mut positional, &mut longopts);
    TryShellSubcommand::autocomplete(command, &mut positional, &mut longopts);
    RunShellSubcommand::autocomplete(command, &mut positional, &mut longopts);
    CleanseShellSubcommand::autocomplete(command, &mut positional, &mut longopts);
    MinimizeShellSubcommand::autocomplete(command, &mut positional, &mut longopts);
    MergeShellSubcommand::autocomplete(command, &mut positional, &mut longopts);
    StatusShellSubcommand::autocomplete(command, &mut positional, &mut longopts);
    FetchShellSubcommand::autocomplete(command, &mut positional, &mut longopts);
    DetachShellSubcommand::autocomplete(command, &mut positional, &mut longopts);
    StopSubcommand::autocomplete(command, &mut positional, &mut longopts);
    ExitShellSubcommand::autocomplete(command, &mut positional, &mut longopts);
    ClearShellSubcommand::autocomplete(command, &mut positional, &mut longopts);
    HistoryShellSubcommand::autocomplete(command, &mut positional, &mut longopts);
    (positional, longopts)
}

#[cfg(test)]
mod test_fixtures {
    use {
        super::FuzzHelper,
        crate::test_fixtures::Test,
        anyhow::{Context as _, Result},
        ffx_fuzz_args::FuzzerState,
        rustyline::completion::{Completer, Pair},
        std::sync::{Arc, Mutex},
    };

    /// Represents replacement strings when auto-completing. Typically, the replacement is just
    /// an auto-completion candidate with the prefix that the user has already entered removed, but
    /// it may be something completely different, e.g. an absolute path for a relative path
    /// candidate.
    pub enum Replacements {
        Exact,
        ExceptPrefix(String),
    }

    impl Replacements {
        pub fn except<S: AsRef<str>>(prefix: S) -> Replacements {
            Replacements::ExceptPrefix(prefix.as_ref().to_string())
        }
    }

    /// Sets the `state` shared with the `FuzzHelper` to the `desired` valie.
    pub fn set_state(state: Arc<Mutex<FuzzerState>>, desired: FuzzerState) {
        let mut state_mut = state.lock().unwrap();
        *state_mut = desired;
    }

    /// Verifies a list of auto-completion candidates matches expectations.
    ///
    /// The auto-completion suggestions are represented by a `Pair` consisting of a string to
    /// display and a string to use as the replacement. The `actual` pairs by `FuzzHelper::complete`
    /// are matched against `expected_displays` and `expected_replacements`, respectively.
    pub fn verify_pairs(
        actual: Vec<Pair>,
        expected_displays: Vec<&str>,
        expected_replacements: Replacements,
    ) {
        let (mut actual_displays, mut actual_replacements): (Vec<String>, Vec<String>) =
            actual.into_iter().map(|actual| (actual.display, actual.replacement)).unzip();
        actual_displays.sort();
        actual_replacements.sort();

        let mut expected_displays: Vec<String> =
            expected_displays.iter().map(|&s| s.to_string()).collect();
        expected_displays.sort();

        let mut expected_replacements: Vec<String> = match expected_replacements {
            Replacements::Exact => expected_displays.to_vec(),
            Replacements::ExceptPrefix(common) => expected_displays
                .iter()
                .map(|s| s.trim_start_matches(&common).to_string())
                .collect(),
        };
        expected_replacements.sort();

        assert_eq!(actual_displays, expected_displays);
        assert_eq!(actual_replacements, expected_replacements);
    }

    pub fn verify_files(test: &Test, helper: &FuzzHelper, cmd: &str) -> Result<()> {
        let test_dir = test
            .create_dir("test")
            .context("failed to create 'test' directory for file verification")?;
        let test_files = vec!["test1", "test2"];
        test.create_test_files(&test_dir, test_files.iter())
            .context("failed to create test files")?;

        let cmdline = format!("{} {}/t", cmd, test_dir.to_string_lossy());
        let result = helper.complete(&cmdline, 0).context("failed to complete cmdline")?;
        verify_pairs(result.1, test_files, Replacements::except("t"));
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use {
        super::test_fixtures::{set_state, verify_files, verify_pairs, Replacements},
        super::FuzzHelper,
        crate::test_fixtures::Test,
        anyhow::Result,
        ffx_fuzz_args::FuzzerState,
        rustyline::completion::Completer,
        std::sync::{Arc, Mutex},
    };

    #[fuchsia::test]
    async fn test_empty() -> Result<()> {
        let state = Arc::new(Mutex::new(FuzzerState::Detached));
        let helper = FuzzHelper::new(None, Arc::clone(&state));

        // Not attached.
        let result = helper.complete("", 0)?;
        let candidates =
            vec!["list", "attach", "status", "stop", "exit", "clear", "help", "history"];
        verify_pairs(result.1, candidates, Replacements::Exact);

        // Not already running.
        set_state(Arc::clone(&state), FuzzerState::Idle);
        let result = helper.complete("", 0)?;
        let candidates = vec![
            "list", "get", "set", "add", "try", "run", "cleanse", "minimize", "merge", "status",
            "fetch", "detach", "stop", "exit", "clear", "help", "history",
        ];
        verify_pairs(result.1, candidates, Replacements::Exact);

        // Interrupting an attached fuzzer.
        set_state(Arc::clone(&state), FuzzerState::Running);
        let result = helper.complete("", 0)?;
        let candidates = vec![
            "list", "get", "add", "status", "fetch", "detach", "stop", "exit", "clear", "help",
            "history",
        ];
        verify_pairs(result.1, candidates, Replacements::Exact);

        Ok(())
    }

    #[fuchsia::test]
    async fn test_list() -> Result<()> {
        let state = Arc::new(Mutex::new(FuzzerState::Detached));
        let helper = FuzzHelper::new(None, Arc::clone(&state));

        // 'list' is always available.
        let result = helper.complete("l", 0)?;
        verify_pairs(result.1, vec!["list"], Replacements::except("l"));

        set_state(Arc::clone(&state), FuzzerState::Idle);
        let result = helper.complete("l", 0)?;
        verify_pairs(result.1, vec!["list"], Replacements::except("l"));

        set_state(Arc::clone(&state), FuzzerState::Running);
        let result = helper.complete("l", 0)?;
        verify_pairs(result.1, vec!["list"], Replacements::except("l"));

        // 'list' takes flags as arguments.
        let result = helper.complete("list -", 0)?;
        verify_pairs(result.1, vec!["--json-file", "--pattern"], Replacements::except("-"));

        let result = helper.complete("list --invalid", 0)?;
        verify_pairs(result.1, Vec::new(), Replacements::Exact);

        Ok(())
    }

    // This test is larger than some of the others as it is used to test autocomplation of commands,
    // options, files, and URLs.
    #[fuchsia::test]
    async fn test_attach() -> Result<()> {
        let test = Test::try_new()?;
        let state = Arc::new(Mutex::new(FuzzerState::Detached));
        let urls = vec![
            "fuchsia-pkg://fuchsia.com/fake#meta/foo-fuzzer.cm",
            "fuchsia-pkg://fuchsia.com/fake#meta/bar-fuzzer.cm",
            "fuchsia-pkg://fuchsia.com/fake#meta/baz-fuzzer.cm",
        ];
        let tests_json = test.create_tests_json(urls.iter())?;
        let tests_json = Some(tests_json.to_string_lossy().to_string());
        let helper = FuzzHelper::new(tests_json, Arc::clone(&state));

        // 'attach' is only suggested when detached.
        let result = helper.complete("a", 0)?;
        verify_pairs(result.1, vec!["attach"], Replacements::except("a"));

        set_state(Arc::clone(&state), FuzzerState::Idle);
        let result = helper.complete("at", 0)?;
        assert!(result.1.is_empty());

        set_state(Arc::clone(&state), FuzzerState::Running);
        let result = helper.complete("at", 0)?;
        assert!(result.1.is_empty());

        set_state(Arc::clone(&state), FuzzerState::Detached);

        // Last token already complete.
        let result = helper.complete("attach", 0)?;
        verify_pairs(result.1, vec!["attach"], Replacements::except("attach"));

        // Completes to URL.
        let result = helper.complete("attach f", 0)?;
        verify_pairs(result.1, urls.clone(), Replacements::except("f"));

        let result = helper.complete("attach fuchsia-pkg://fuchsia.com/fake#meta/b", 0)?;
        let candidates = vec![urls[1], urls[2]];
        verify_pairs(
            result.1,
            candidates,
            Replacements::except("fuchsia-pkg://fuchsia.com/fake#meta/b"),
        );

        // 'attach' takes a flag as an argument.
        let result = helper.complete("attach -", 0)?;
        verify_pairs(
            result.1,
            vec!["--output", "--no-stdout", "--no-stderr", "--no-syslog"],
            Replacements::except("-"),
        );

        Ok(())
    }

    #[fuchsia::test]
    async fn test_get() -> Result<()> {
        let state = Arc::new(Mutex::new(FuzzerState::Detached));
        let helper = FuzzHelper::new(None, Arc::clone(&state));

        // 'get' is only suggested if attached.
        let result = helper.complete("g", 0)?;
        assert!(result.1.is_empty());

        set_state(Arc::clone(&state), FuzzerState::Idle);
        let result = helper.complete("g", 0)?;
        verify_pairs(result.1, vec!["get"], Replacements::except("g"));

        set_state(Arc::clone(&state), FuzzerState::Running);
        let result = helper.complete("g", 0)?;
        verify_pairs(result.1, vec!["get"], Replacements::except("g"));

        let result = helper.complete("get ma", 0)?;
        let candidates =
            vec!["max_total_time", "max_input_size", "malloc_limit", "malloc_exitcode"];
        verify_pairs(result.1, candidates, Replacements::except("ma"));

        Ok(())
    }

    #[fuchsia::test]
    async fn test_set() -> Result<()> {
        let state = Arc::new(Mutex::new(FuzzerState::Detached));
        let helper = FuzzHelper::new(None, Arc::clone(&state));

        // 'set' is only suggested if attached but not running.
        let result = helper.complete("se", 0)?;
        assert!(result.1.is_empty());

        set_state(Arc::clone(&state), FuzzerState::Running);
        let result = helper.complete("se", 0)?;
        assert!(result.1.is_empty());

        set_state(Arc::clone(&state), FuzzerState::Idle);
        let result = helper.complete("se", 0)?;
        verify_pairs(result.1, vec!["set"], Replacements::except("se"));

        let result = helper.complete("set det", 0)?;
        let candidates = vec!["detect_leaks", "detect_exits"];
        verify_pairs(result.1, candidates, Replacements::except("det"));

        Ok(())
    }

    #[fuchsia::test]
    async fn test_add() -> Result<()> {
        let test = Test::try_new()?;
        let state = Arc::new(Mutex::new(FuzzerState::Detached));
        let helper = FuzzHelper::new(None, Arc::clone(&state));

        // 'add' is only suggested when attached.
        let result = helper.complete("a", 0)?;
        verify_pairs(result.1, vec!["attach"], Replacements::except("a"));

        set_state(Arc::clone(&state), FuzzerState::Idle);
        let result = helper.complete("a", 0)?;
        verify_pairs(result.1, vec!["add"], Replacements::except("a"));

        set_state(Arc::clone(&state), FuzzerState::Running);
        let result = helper.complete("a", 0)?;
        verify_pairs(result.1, vec!["add"], Replacements::except("a"));

        // 'add' can take files as arguments.
        verify_files(&test, &helper, "add")
    }

    #[fuchsia::test]
    async fn test_try() -> Result<()> {
        let test = Test::try_new()?;
        let state = Arc::new(Mutex::new(FuzzerState::Detached));
        let helper = FuzzHelper::new(None, Arc::clone(&state));

        // 'try' is only suggested when attached and not running.
        let result = helper.complete("t", 0)?;
        assert!(result.1.is_empty());

        set_state(Arc::clone(&state), FuzzerState::Running);
        let result = helper.complete("t", 0)?;
        assert!(result.1.is_empty());

        set_state(Arc::clone(&state), FuzzerState::Idle);
        let result = helper.complete("t", 0)?;
        verify_pairs(result.1, vec!["try"], Replacements::except("t"));

        // 'try' can take a file as an argument.
        verify_files(&test, &helper, "try")
    }

    #[fuchsia::test]
    async fn test_run() -> Result<()> {
        let state = Arc::new(Mutex::new(FuzzerState::Detached));
        let helper = FuzzHelper::new(None, Arc::clone(&state));

        // 'run' is only suggested when attached and not running.
        let result = helper.complete("r", 0)?;
        assert!(result.1.is_empty());

        set_state(Arc::clone(&state), FuzzerState::Running);
        let result = helper.complete("r", 0)?;
        assert!(result.1.is_empty());

        set_state(Arc::clone(&state), FuzzerState::Idle);
        let result = helper.complete("r", 0)?;
        verify_pairs(result.1, vec!["run"], Replacements::except("r"));

        // 'run' takes flags as arguments.
        let result = helper.complete("run -", 0)?;
        let candidates = vec!["--time", "--runs"];
        verify_pairs(result.1, candidates, Replacements::except("-"));

        Ok(())
    }

    #[fuchsia::test]
    async fn test_cleanse() -> Result<()> {
        let state = Arc::new(Mutex::new(FuzzerState::Detached));
        let helper = FuzzHelper::new(None, Arc::clone(&state));

        // 'cleanse' is only suggested when attached and not running.
        let result = helper.complete("c", 0)?;
        verify_pairs(result.1, vec!["clear"], Replacements::except("c"));

        set_state(Arc::clone(&state), FuzzerState::Running);
        let result = helper.complete("c", 0)?;
        verify_pairs(result.1, vec!["clear"], Replacements::except("c"));

        set_state(Arc::clone(&state), FuzzerState::Idle);
        let result = helper.complete("clean", 0)?;
        verify_pairs(result.1, vec!["cleanse"], Replacements::except("clean"));

        Ok(())
    }

    #[fuchsia::test]
    async fn test_minimize() -> Result<()> {
        let state = Arc::new(Mutex::new(FuzzerState::Detached));
        let helper = FuzzHelper::new(None, Arc::clone(&state));

        // 'minimize' is only suggested when attached and not running.
        let result = helper.complete("m", 0)?;
        assert!(result.1.is_empty());

        set_state(Arc::clone(&state), FuzzerState::Running);
        let result = helper.complete("m", 0)?;
        assert!(result.1.is_empty());

        set_state(Arc::clone(&state), FuzzerState::Idle);
        let result = helper.complete("mi", 0)?;
        verify_pairs(result.1, vec!["minimize"], Replacements::except("mi"));

        Ok(())
    }

    #[fuchsia::test]
    async fn test_merge() -> Result<()> {
        let state = Arc::new(Mutex::new(FuzzerState::Detached));
        let helper = FuzzHelper::new(None, Arc::clone(&state));

        // 'minimize' is only suggested when attached and not running.
        let result = helper.complete("m", 0)?;
        assert!(result.1.is_empty());

        set_state(Arc::clone(&state), FuzzerState::Running);
        let result = helper.complete("m", 0)?;
        assert!(result.1.is_empty());

        set_state(Arc::clone(&state), FuzzerState::Idle);
        let result = helper.complete("me", 0)?;
        verify_pairs(result.1, vec!["merge"], Replacements::except("me"));

        Ok(())
    }

    #[fuchsia::test]
    async fn test_status() -> Result<()> {
        let state = Arc::new(Mutex::new(FuzzerState::Detached));
        let helper = FuzzHelper::new(None, Arc::clone(&state));

        // 'status' is always available.
        let result = helper.complete("sta", 0)?;
        verify_pairs(result.1, vec!["status"], Replacements::except("sta"));

        set_state(Arc::clone(&state), FuzzerState::Idle);
        let result = helper.complete("sta", 0)?;
        verify_pairs(result.1, vec!["status"], Replacements::except("sta"));

        set_state(Arc::clone(&state), FuzzerState::Running);
        let result = helper.complete("sta", 0)?;
        verify_pairs(result.1, vec!["status"], Replacements::except("sta"));

        Ok(())
    }

    #[fuchsia::test]
    async fn test_fetch() -> Result<()> {
        let state = Arc::new(Mutex::new(FuzzerState::Detached));
        let helper = FuzzHelper::new(None, Arc::clone(&state));

        // 'fetch' is only suggested when attached.
        let result = helper.complete("f", 0)?;
        assert!(result.1.is_empty());

        set_state(Arc::clone(&state), FuzzerState::Idle);
        let result = helper.complete("f", 0)?;
        verify_pairs(result.1, vec!["fetch"], Replacements::except("f"));

        set_state(Arc::clone(&state), FuzzerState::Running);
        let result = helper.complete("f", 0)?;
        verify_pairs(result.1, vec!["fetch"], Replacements::except("f"));

        // 'fetch' can take flags as arguments.
        let result = helper.complete("fetch -", 0)?;
        let candidates = vec!["--seed"];
        verify_pairs(result.1, candidates, Replacements::except("-"));

        Ok(())
    }

    #[fuchsia::test]
    async fn test_detach() -> Result<()> {
        let state = Arc::new(Mutex::new(FuzzerState::Detached));
        let helper = FuzzHelper::new(None, Arc::clone(&state));

        // 'detach' is only suggested when attached.
        let result = helper.complete("d", 0)?;
        assert!(result.1.is_empty());

        set_state(Arc::clone(&state), FuzzerState::Idle);
        let result = helper.complete("d", 0)?;
        verify_pairs(result.1, vec!["detach"], Replacements::except("d"));

        set_state(Arc::clone(&state), FuzzerState::Running);
        let result = helper.complete("d", 0)?;
        verify_pairs(result.1, vec!["detach"], Replacements::except("d"));

        Ok(())
    }

    #[fuchsia::test]
    async fn test_stop() -> Result<()> {
        let state = Arc::new(Mutex::new(FuzzerState::Detached));
        let helper = FuzzHelper::new(None, Arc::clone(&state));

        // 'stop' is always available.
        let result = helper.complete("sto", 0)?;
        verify_pairs(result.1, vec!["stop"], Replacements::except("sto"));

        set_state(Arc::clone(&state), FuzzerState::Idle);
        let result = helper.complete("sto", 0)?;
        verify_pairs(result.1, vec!["stop"], Replacements::except("sto"));

        set_state(Arc::clone(&state), FuzzerState::Running);
        let result = helper.complete("sto", 0)?;
        verify_pairs(result.1, vec!["stop"], Replacements::except("sto"));

        Ok(())
    }

    #[fuchsia::test]
    async fn test_exit() -> Result<()> {
        let state = Arc::new(Mutex::new(FuzzerState::Detached));
        let helper = FuzzHelper::new(None, Arc::clone(&state));

        // 'exit' is always available.
        let result = helper.complete("e", 0)?;
        verify_pairs(result.1, vec!["exit"], Replacements::except("e"));

        set_state(Arc::clone(&state), FuzzerState::Idle);
        let result = helper.complete("e", 0)?;
        verify_pairs(result.1, vec!["exit"], Replacements::except("e"));

        set_state(Arc::clone(&state), FuzzerState::Running);
        let result = helper.complete("e", 0)?;
        verify_pairs(result.1, vec!["exit"], Replacements::except("e"));

        Ok(())
    }

    #[fuchsia::test]
    async fn test_clear() -> Result<()> {
        let state = Arc::new(Mutex::new(FuzzerState::Detached));
        let helper = FuzzHelper::new(None, Arc::clone(&state));

        // 'clear' is always available.
        let result = helper.complete("c", 0)?;
        verify_pairs(result.1, vec!["clear"], Replacements::except("c"));

        set_state(Arc::clone(&state), FuzzerState::Idle);
        let result = helper.complete("clear", 0)?;
        verify_pairs(result.1, vec!["clear"], Replacements::except("clear"));

        set_state(Arc::clone(&state), FuzzerState::Running);
        let result = helper.complete("c", 0)?;
        verify_pairs(result.1, vec!["clear"], Replacements::except("c"));

        Ok(())
    }

    #[fuchsia::test]
    async fn test_history() -> Result<()> {
        let state = Arc::new(Mutex::new(FuzzerState::Detached));
        let helper = FuzzHelper::new(None, Arc::clone(&state));

        // 'history' is always available.
        let result = helper.complete("hi", 0)?;
        verify_pairs(result.1, vec!["history"], Replacements::except("hi"));

        set_state(Arc::clone(&state), FuzzerState::Idle);
        let result = helper.complete("hi", 0)?;
        verify_pairs(result.1, vec!["history"], Replacements::except("hi"));

        set_state(Arc::clone(&state), FuzzerState::Running);
        let result = helper.complete("hi", 0)?;
        verify_pairs(result.1, vec!["history"], Replacements::except("hi"));

        Ok(())
    }
}
