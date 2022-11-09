// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod args;
pub mod builtin;
pub mod error;

use {
    crate::shell::{
        args::args_to_json,
        builtin::{Builtin, BuiltinCommand},
        error::ShellError,
    },
    anyhow::{Error, Result},
    rustyline::{
        completion::{Completer, FilenameCompleter, Pair},
        error::ReadlineError,
        highlight::Highlighter,
        hint::Hinter,
        CompletionType, Config, Editor, Helper,
    },
    scrutiny::engine::{
        dispatcher::{ControllerDispatcher, DispatcherError},
        manager::{PluginManager, PluginState},
        plugin::PluginDescriptor,
    },
    serde_json::{self, json, Value},
    std::{
        borrow::Cow::{self, Borrowed},
        collections::{HashMap, VecDeque},
        fmt::Write,
        process,
        sync::{Arc, Mutex, RwLock},
    },
    termion::{self, clear, color, cursor, style},
    tracing::{error, info, warn},
};

/// A CommandResponse is an internal type to define whether a command was acted
/// on by a submodule or not.
#[derive(Eq, PartialEq)]
enum CommandResponse {
    Executed,
    NotFound,
}

#[allow(dead_code)]
struct ScrutinyHelper {
    dispatcher: Arc<RwLock<ControllerDispatcher>>,
    file_completer: FilenameCompleter,
}

impl ScrutinyHelper {
    fn new(dispatcher: Arc<RwLock<ControllerDispatcher>>) -> Self {
        Self { dispatcher, file_completer: FilenameCompleter::new() }
    }
}

impl Completer for ScrutinyHelper {
    type Candidate = Pair;
    fn complete(&self, line: &str, _pos: usize) -> Result<(usize, Vec<Pair>), ReadlineError> {
        let tokens = line.split(" ").collect::<Vec<&str>>();
        // If we have more than 1 token then just use the file completer.
        if tokens.len() <= 1 {
            let mut controllers = self.dispatcher.read().unwrap().controllers_all();
            controllers.append(&mut BuiltinCommand::commands());
            let mut matches = vec![];
            let search_term = tokens.first().unwrap();
            for controller in controllers.iter() {
                let mut command = str::replace(&controller, "/api/", "");
                command = str::replace(&command, "/", ".");
                if command.starts_with(search_term) {
                    matches.push(Pair {
                        display: command.clone(),
                        replacement: command.trim_start_matches(search_term).to_string(),
                    });
                }
            }
            return Ok((line.len(), matches));
        } else {
            let current_param = tokens.last().unwrap();
            if current_param.starts_with('-') {
                let mut namespace = tokens.first().unwrap().to_string();
                if !namespace.starts_with("/api") {
                    namespace = str::replace(&namespace, ".", "/");
                    namespace.insert_str(0, "/api/");
                    if let Ok(hints) = self.dispatcher.read().unwrap().hints(namespace) {
                        let mut matches = vec![];
                        for (param, _data_type) in hints.iter() {
                            if param.starts_with(current_param) {
                                matches.push(Pair {
                                    display: param.clone(),
                                    replacement: param[current_param.len()..].to_string(),
                                });
                            }
                        }
                        return Ok((line.len(), matches));
                    }
                }
            }
        }
        Ok((line.len(), vec![]))
    }
}

impl Hinter for ScrutinyHelper {
    fn hint(&self, _line: &str, _pos: usize) -> Option<String> {
        None
    }
}

impl Highlighter for ScrutinyHelper {
    fn highlight_prompt<'p>(&self, prompt: &'p str) -> Cow<'p, str> {
        Borrowed(prompt)
    }
}

impl Helper for ScrutinyHelper {}

pub struct Shell {
    manager: Arc<Mutex<PluginManager>>,
    dispatcher: Arc<RwLock<ControllerDispatcher>>,
    readline: Editor<ScrutinyHelper>,
    silent_mode: bool,
}

impl Shell {
    pub fn new(
        manager: Arc<Mutex<PluginManager>>,
        dispatcher: Arc<RwLock<ControllerDispatcher>>,
        silent_mode: bool,
    ) -> Self {
        let config = Config::builder()
            .history_ignore_space(true)
            .completion_type(CompletionType::List)
            .build();
        let mut readline = Editor::with_config(config);
        let helper = ScrutinyHelper::new(dispatcher.clone());
        readline.set_helper(Some(helper));
        if let Err(_) = readline.load_history("/tmp/scrutiny_history") {
            warn!("No shell history available");
        }
        Self { manager, dispatcher, readline, silent_mode }
    }

    fn prompt(&mut self) -> Option<String> {
        let prompt = format!(
            "{reset}{bold}{yellow_fg}scrutiny »{reset} ",
            yellow_fg = color::Fg(color::Yellow),
            bold = style::Bold,
            reset = style::Reset,
        );

        let readline = self.readline.readline(&prompt);
        if let Err(err) = self.readline.save_history("/tmp/scrutiny_history") {
            warn!(?err, "Failed to save scrutiny shell history");
        }

        match readline {
            Ok(line) => {
                self.readline.add_history_entry(line.as_str());
                Some(line)
            }
            _ => None,
        }
    }

    fn builtin(
        &mut self,
        command: String,
        out_buffer: &mut impl std::fmt::Write,
    ) -> Result<CommandResponse> {
        if let Some(builtin) = BuiltinCommand::parse(command) {
            match builtin.program {
                Builtin::PluginLoad => {
                    if builtin.args.len() != 1 {
                        writeln!(out_buffer, "Error: Provide a single plugin to load.")?;
                        return Ok(CommandResponse::Executed);
                    }
                    let desc = PluginDescriptor::new(builtin.args.first().unwrap());
                    if let Err(e) = self.manager.lock().unwrap().load(&desc) {
                        writeln!(out_buffer, "Error: {:?}", e)?;
                    }
                }
                Builtin::PluginUnload => {
                    if builtin.args.len() != 1 {
                        writeln!(out_buffer, "Error: Provide a single plugin to unload.")?;
                        return Ok(CommandResponse::Executed);
                    }
                    let desc = PluginDescriptor::new(builtin.args.first().unwrap());
                    if let Err(e) = self.manager.lock().unwrap().unload(&desc) {
                        writeln!(out_buffer, "Error: {:?}", e)?;
                    }
                }
                Builtin::Print => {
                    let message = builtin.args.join(" ");
                    writeln!(out_buffer, "{}", message)?;
                }
                Builtin::Help => {
                    // Provide usage for a specific command.
                    if builtin.args.len() == 1 {
                        let mut command = builtin.args.first().unwrap().clone();
                        command = str::replace(&command, ".", "/");
                        if !command.starts_with("/api/") {
                            command.insert_str(0, "/api/");
                        }
                        let result = self.dispatcher.read().unwrap().usage(command);
                        if let Ok(usage) = result {
                            writeln!(out_buffer, "{}", usage)?;
                        } else {
                            writeln!(out_buffer, "Error: Command does not exist.")?;
                        }
                        return Ok(CommandResponse::Executed);
                    }
                    // Provide usage for a general command.
                    BuiltinCommand::usage();
                    writeln!(out_buffer, "")?;
                    let manager = self.manager.lock().unwrap();
                    let plugins = manager.plugins();
                    for plugin in plugins.iter() {
                        let state = manager.state(plugin).unwrap();
                        if state == PluginState::Loaded {
                            let instance_id = manager.instance_id(plugin).unwrap();
                            let mut controllers =
                                self.dispatcher.read().unwrap().controllers(instance_id);
                            controllers.sort();

                            // Add spacing for the plugin names.
                            let mut plugin_name = format!("{}", plugin);
                            let mut formatted: Vec<char> = Vec::new();
                            for c in plugin_name.chars() {
                                if c.is_uppercase() && !formatted.is_empty() {
                                    formatted.push(' ');
                                }
                                formatted.push(c);
                            }
                            plugin_name = formatted.into_iter().collect();
                            writeln!(out_buffer, "{} Commands:", plugin_name)?;

                            // Print out all the plugin specific commands
                            for controller in controllers.iter() {
                                let description = self
                                    .dispatcher
                                    .read()
                                    .unwrap()
                                    .description(controller.to_string())
                                    .unwrap();
                                let command = str::replace(&controller, "/api/", "");
                                let shell_command = str::replace(&command, "/", ".");
                                if description.is_empty() {
                                    writeln!(out_buffer, "  {}", shell_command)?;
                                } else {
                                    writeln!(
                                        out_buffer,
                                        "  {:<25} - {}",
                                        shell_command, description
                                    )?;
                                }
                            }
                            writeln!(out_buffer, "")?;
                        }
                    }
                }
                Builtin::History => {
                    let mut count = 1;
                    for entry in self.readline.history().iter() {
                        writeln!(out_buffer, "{} {}", count, entry)?;
                        count += 1;
                    }
                }
                Builtin::Clear => {
                    write!(out_buffer, "{}{}", clear::All, cursor::Goto(1, 1))?;
                }
                Builtin::Exit => {
                    process::exit(0);
                }
            };
            return Ok(CommandResponse::Executed);
        }
        Ok(CommandResponse::NotFound)
    }

    /// Parses a command returning the namespace and arguments as a json value.
    /// This can fail if any of the command arguments are invalid. This function
    /// does not check whether the command exists just verifies the input is
    /// sanitized.
    fn parse_command(command: impl Into<String>) -> Result<(String, Value)> {
        let mut tokens: VecDeque<String> =
            command.into().split_whitespace().map(|s| String::from(s)).collect();
        if tokens.len() == 0 {
            return Err(Error::new(ShellError::empty_command()));
        }
        // Transform foo.bar to /foo/bar
        let head = tokens.pop_front().unwrap();
        let namespace = if head.starts_with("/") {
            head.to_string()
        } else {
            "/api/".to_owned() + &str::replace(&head, ".", "/")
        };

        // Parse the command arguments.
        let empty_command: HashMap<String, String> = HashMap::new();
        let mut query = json!(empty_command);
        if !tokens.is_empty() {
            if tokens.front().unwrap().starts_with("`") {
                let mut body = Vec::from(tokens).join(" ");
                if body.len() > 2 && body.ends_with("`") {
                    body.remove(0);
                    body.pop();
                    match serde_json::from_str(&body) {
                        Ok(expr) => {
                            query = expr;
                        }
                        Err(err) => {
                            return Err(Error::new(err));
                        }
                    }
                } else {
                    return Err(Error::new(ShellError::unescaped_json_string(body)));
                }
            } else if tokens.front().unwrap().starts_with("--") {
                query = args_to_json(&tokens);
            }
        }
        Ok((namespace, query))
    }

    /// Attempts to transform the command into a dispatcher command to see if
    /// any plugin services that url. Two syntaxes are supported /foo/bar or
    /// foo.bar both will be translated into /foo/bar before being sent to the
    /// dispatcher.
    fn plugin(
        &mut self,
        command: String,
        out_buffer: &mut impl std::fmt::Write,
    ) -> Result<CommandResponse> {
        let command_result = Self::parse_command(command);
        if let Err(err) = command_result {
            if let Some(shell_error) = err.downcast_ref::<ShellError>() {
                if let ShellError::EmptyCommand = shell_error {
                    return Ok(CommandResponse::Executed);
                }
            }
            writeln!(out_buffer, "Error: {:?}", err)?;
            return Ok(CommandResponse::Executed);
        }
        let (namespace, query) = command_result.unwrap();

        let result = self.dispatcher.read().unwrap().query(namespace, query);
        match result {
            Err(e) => {
                if let Some(dispatch_error) = e.downcast_ref::<DispatcherError>() {
                    match dispatch_error {
                        DispatcherError::NamespaceDoesNotExist(_) => {
                            return Ok(CommandResponse::NotFound);
                        }
                        _ => {}
                    }
                }
                writeln!(out_buffer, "Error: {:?}", e)?;
                Ok(CommandResponse::Executed)
            }
            Ok(result) => {
                writeln!(out_buffer, "{}", serde_json::to_string_pretty(&result).unwrap())?;
                Ok(CommandResponse::Executed)
            }
        }
    }

    /// Executes a single command.
    pub fn execute(&mut self, command: String) -> Result<String> {
        if command.is_empty() || command.starts_with("#") {
            return Ok(String::new());
        }
        info!(%command);

        let mut output = String::new();
        #[allow(clippy::if_same_then_else)] // TODO(fxbug.dev/95035)
        if self.builtin(command.clone(), &mut output)? == CommandResponse::Executed {
        } else if self.plugin(command.clone(), &mut output)? == CommandResponse::Executed {
        } else {
            writeln!(output, "scrutiny: command not found: {}", command)?;
        }
        if !self.silent_mode {
            print!("{}", output);
        }
        Ok(output)
    }

    /// Runs a blocking loop executing commands from standard input.
    pub fn run(&mut self) {
        loop {
            if let Some(command) = self.prompt() {
                if let Err(err) = self.execute(command) {
                    error!(?err, "Execute failed");
                }
            } else {
                break;
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        scrutiny::model::{
            controller::{DataController, HintDataType},
            model::DataModel,
        },
        scrutiny_testing::fake::*,
        uuid::Uuid,
    };

    #[derive(Default)]
    struct FakeController {}
    impl DataController for FakeController {
        fn query(&self, _: Arc<DataModel>, _: Value) -> Result<Value> {
            Ok(json!(""))
        }

        fn hints(&self) -> Vec<(String, HintDataType)> {
            vec![("--baz".to_string(), HintDataType::NoType)]
        }
    }

    fn test_model() -> Arc<DataModel> {
        fake_data_model()
    }

    #[test]
    fn test_parse_command_errors() {
        assert_eq!(Shell::parse_command("foo").is_ok(), true);
        assert_eq!(Shell::parse_command("foo `{}`").is_ok(), true);
        assert_eq!(Shell::parse_command("foo `{\"abc\": 1}`").is_ok(), true);
        assert_eq!(Shell::parse_command("foo `{aaa}`").is_ok(), false);
        assert_eq!(Shell::parse_command("foo `{").is_ok(), false);
        assert_eq!(Shell::parse_command("foo `").is_ok(), false);
    }
    #[test]
    fn test_parse_command_tokens() {
        assert_eq!(Shell::parse_command("foo").unwrap().0, "/api/foo");
        assert_eq!(Shell::parse_command("foo `{\"a\": 1}`").unwrap().1, json!({"a": 1}));
    }

    #[test]
    fn test_help_ok() {
        assert_eq!(Shell::parse_command("help").is_ok(), true);
        assert_eq!(Shell::parse_command("help zbi.bootfs").is_ok(), true);
    }

    #[test]
    fn test_hinter_empty() {
        let data_model = test_model();
        let dispatcher = Arc::new(RwLock::new(ControllerDispatcher::new(data_model)));
        let helper = ScrutinyHelper::new(dispatcher);
        let result = helper.complete("", 0).unwrap();
        assert_eq!(result.1.len(), BuiltinCommand::commands().len());
    }

    #[test]
    fn test_hinter() {
        let data_model = test_model();
        let dispatcher = Arc::new(RwLock::new(ControllerDispatcher::new(data_model)));
        let fake = Arc::new(FakeController::default());
        let namespace = "/api/test/foo/bar".to_string();
        dispatcher.write().unwrap().add(Uuid::new_v4(), namespace.clone(), fake).unwrap();
        let helper = ScrutinyHelper::new(dispatcher);
        let result = helper.complete("", 0).unwrap();
        assert_eq!(result.1.len(), BuiltinCommand::commands().len() + 1);
        let result = helper.complete("test.foo.ba", 0).unwrap();
        assert_eq!(result.1.len(), 1);
        let result = helper.complete("test.foo.bar -", 0).unwrap();
        assert_eq!(result.1.len(), 1);
        let result = helper.complete("test.foo.bar --a", 0).unwrap();
        assert_eq!(result.1.len(), 0);
        let result = helper.complete("test.foo.bar --b", 0).unwrap();
        assert_eq!(result.1.len(), 1);
    }
}
