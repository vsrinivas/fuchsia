// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::{
        error::ShellError,
        builtin::{Builtin, BuiltinCommand},
    },
    log::info,
    scrutiny::engine::{
        dispatcher::{ControllerDispatcher, DispatcherError},
        manager::{PluginManager, PluginState},
        plugin::PluginDescriptor,
        scheduler::CollectorScheduler,
    },
    serde_json::{self, json, Value},
    std::collections::VecDeque,
    std::io::{stdin, stdout, Write},
    std::process,
    std::sync::{Arc, Mutex, RwLock},
    termion::{self, clear, color, cursor, style},
    anyhow::{Error, Result},
};

pub struct Shell {
    manager: Arc<Mutex<PluginManager>>,
    scheduler: Arc<Mutex<CollectorScheduler>>,
    dispatcher: Arc<RwLock<ControllerDispatcher>>,
    history: VecDeque<String>,
}

impl Shell {
    pub fn new(
        manager: Arc<Mutex<PluginManager>>,
        scheduler: Arc<Mutex<CollectorScheduler>>,
        dispatcher: Arc<RwLock<ControllerDispatcher>>,
    ) -> Self {
        Self { manager, scheduler, dispatcher, history: VecDeque::with_capacity(2048) }
    }

    fn prompt(&mut self) -> Option<String> {
        let stdin = stdin();
        let mut stdout = stdout();

        write!(stdout, "{reset}{yellow_bg}{black_fg}{arrow}{reset}{black_fg}{yellow_bg} scrutiny⚫{reset}{yellow_fg}{arrow}{reset} ",
            black_fg = color::Fg(color::Black),
            yellow_fg = color::Fg(color::Yellow),
            yellow_bg = color::Bg(color::Yellow),
            arrow = "\u{E0B0}",
            reset = style::Reset,
        ).unwrap();
        stdout.flush().unwrap();
        let mut buffer = String::new();
        stdin.read_line(&mut buffer).unwrap();
        Some(buffer)
    }

    fn builtin(&mut self, command: String) -> bool {
        if let Some(builtin) = BuiltinCommand::parse(command) {
            match builtin.program {
                Builtin::PluginList => {
                    let plugins = self.manager.lock().unwrap().plugins();
                    println!("Scrutiny Plugins: ");
                    for plugin in plugins.iter() {
                        let state = self.manager.lock().unwrap().state(plugin).unwrap();
                        println!("  {} - {}", plugin, state);
                    }
                }
                Builtin::PluginControllers => {
                    if builtin.args.len() != 1 {
                        println!("Error: Provide a single plugin argument.");
                        return true;
                    }
                    let desc = PluginDescriptor::new(builtin.args.first().unwrap());
                    let result = self.manager.lock().unwrap().instance_id(&desc);
                    if let Err(e) = result {
                        println!("Error: {}", e);
                    } else {
                        let instance_id = result.unwrap();
                        let controllers = self.dispatcher.read().unwrap().controllers(instance_id);
                        println!("{} Controllers:", desc);
                        for controller in controllers.iter() {
                            println!("  {}", controller);
                        }
                    }
                }
                Builtin::PluginCollectors => {
                    if builtin.args.len() != 1 {
                        println!("Error: Provide a single plugin argument.");
                        return true;
                    }
                    let desc = PluginDescriptor::new(builtin.args.first().unwrap());
                    let result = self.manager.lock().unwrap().instance_id(&desc);
                    if let Err(e) = result {
                        println!("Error: {}", e);
                    } else {
                        let instance_id = result.unwrap();
                        let scheduler_guard = self.scheduler.lock().unwrap();
                        let collectors = scheduler_guard.collectors(instance_id);

                        println!("{} Collectors:", desc);
                        for (handle, name) in collectors.iter() {
                            let state = scheduler_guard.state(handle).unwrap();
                            println!("  {} - {}", name, state);
                        }
                    }
                }
                Builtin::PluginLoad => {
                    if builtin.args.len() != 1 {
                        println!("Error: Provide a single plugin to load.");
                        return true;
                    }
                    let desc = PluginDescriptor::new(builtin.args.first().unwrap());
                    if let Err(e) = self.manager.lock().unwrap().load(&desc) {
                        println!("Error: {}", e);
                    }
                }
                Builtin::PluginUnload => {
                    if builtin.args.len() != 1 {
                        println!("Error: Provide a single plugin to unload.");
                        return true;
                    }
                    let desc = PluginDescriptor::new(builtin.args.first().unwrap());
                    if let Err(e) = self.manager.lock().unwrap().unload(&desc) {
                        println!("Error: {}", e);
                    }
                }
                Builtin::PluginSchedule => {
                    let scheduler = self.scheduler.lock().unwrap();
                    let collectors = scheduler.collectors_all();
                    if let Err(e) = scheduler.schedule() {
                        println!("Error: {}", e);
                    } else {
                        println!("Scheduling:");
                        for (_handle, name) in collectors {
                            println!("  {}", name);
                        }
                    }
                }
                Builtin::Help => {
                    BuiltinCommand::usage();
                    println!("\nPlugin Commands:\n");
                    let manager = self.manager.lock().unwrap();
                    let plugins = manager.plugins();
                    for plugin in plugins.iter() {
                        let state = manager.state(plugin).unwrap();
                        if state == PluginState::Loaded {
                            let instance_id = manager.instance_id(plugin).unwrap();
                            let controllers =
                                self.dispatcher.read().unwrap().controllers(instance_id);
                            println!("{} Commands:", plugin);
                            for controller in controllers.iter() {
                                let command = str::replace(&controller, "/api/", "");
                                println!("  {}", str::replace(&command, "/", "."));
                            }
                            println!("");
                        }
                    }
                }
                Builtin::History => {
                    let mut count = 1;
                    for entry in self.history.iter() {
                        print!("{} {}", count, entry);
                        count += 1;
                    }
                }
                Builtin::Clear => {
                    print!("{}{}", clear::All, cursor::Goto(1, 1));
                }
                Builtin::Exit => {
                    process::exit(0);
                }
            };
            return true;
        }
        false
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
        let mut query = json!("");
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
            }
        }
        Ok((namespace, query))
    }

    /// Attempts to transform the command into a dispatcher command to see if
    /// any plugin services that url. Two syntaxes are supported /foo/bar or
    /// foo.bar both will be translated into /foo/bar before being sent to the
    /// dispatcher.
    fn plugin(&mut self, command: String) -> bool {
        let command_result = Self::parse_command(command);
        if let Err(err) = command_result {
            println!("Error: {}", err);
            return true;
        }
        let (namespace, query) = command_result.unwrap();

        let result = self.dispatcher.read().unwrap().query(namespace, query);
        match result {
            Err(e) => {
                if let Some(dispatch_error) = e.downcast_ref::<DispatcherError>() {
                    match dispatch_error {
                        DispatcherError::NamespaceDoesNotExist(_) => {
                            return false;
                        }
                        _ => {}
                    }
                }
                println!("Error: {}", e);
                true
            }
            Ok(result) => {
                println!("{}", serde_json::to_string_pretty(&result).unwrap());
                true
            }
        }
    }

    /// Executes a single command.
    pub fn execute(&mut self, command: String) {
        if command.is_empty() {
            return;
        }
        self.history.push_front(command.clone());
        info!("Command: {}", command);

        if self.builtin(command.clone()) {
        } else if self.plugin(command.clone()) {
        } else {
            println!("scrutiny: command not found: {}", command);
        }
    }

    /// Runs a blocking loop executing commands from standard input.
    pub fn run(&mut self) {
        loop {
            if let Some(command) = self.prompt() {
                self.execute(command);
            } else {
                break;
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

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
}
