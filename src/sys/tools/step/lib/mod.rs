// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod events;

use {
    crate::events::EventListener,
    rustyline::{Config, Editor},
    structopt::StructOpt,
    test_utils_lib::events::EventSource,
};

#[derive(Clone, Debug, StructOpt)]
#[structopt(
    about = "Pre-start Command Help",
    raw(setting = "structopt::clap::AppSettings::NoBinaryName"),
    raw(setting = "structopt::clap::AppSettings::ColoredHelp")
)]
enum PreStartCommand {
    /// Start the component tree.
    #[structopt(name = "start")]
    Start,

    /// Create listeners for specific event types
    #[structopt(name = "listen")]
    Listen {
        #[structopt(required = true)]
        event_types: Vec<String>,

        /// If set, the listener will automatically resume all events.
        #[structopt(short = "r", long = "auto-resume")]
        auto_resume: bool,
    },

    /// List all event listeners
    #[structopt(name = "listeners")]
    Listeners,

    /// List supported event types
    #[structopt(name = "event-types")]
    EventTypes,

    /// Exit program
    #[structopt(name = "exit")]
    Exit,
}

#[derive(Clone, Debug, StructOpt)]
#[structopt(
    about = "Post-start Command Help",
    raw(setting = "structopt::clap::AppSettings::NoBinaryName"),
    raw(setting = "structopt::clap::AppSettings::ColoredHelp")
)]
enum PostStartCommand {
    /// List all event listeners
    #[structopt(name = "listeners")]
    Listeners,

    /// Display all events caught by the listener
    #[structopt(name = "events")]
    Events {
        listener_id: usize,

        /// Prints detailed information about all events
        #[structopt(short = "d", long = "detailed")]
        detailed: bool,
    },

    /// Resume an event caught by a listener. This will unblock component manager.
    #[structopt(name = "resume")]
    Resume { listener_id: usize, event_id: usize },

    /// Exit program
    #[structopt(name = "exit")]
    Exit,
}

/// Loop until a valid command of type `C` has been read
/// from stdin. This command handles stdin errors, command
/// parsing errors and help text.
fn read_next_command<C>(editor: &mut Editor<()>) -> C
where
    C: StructOpt,
{
    loop {
        match editor.readline("> ") {
            Ok(line) => {
                let strs = line.split_ascii_whitespace();
                match C::from_iter_safe(strs) {
                    Ok(command) => {
                        return command;
                    }
                    Err(error) => {
                        eprintln!("{}", error);
                    }
                }
            }
            Err(error) => {
                eprintln!("{:?}", error);
            }
        }
    }
}

/// All system event types accepted by the
/// fuchsia.sys2.EventSource::Subscribe FIDL Call
const SYSTEM_EVENT_TYPES: [&'static str; 9] = [
    "capability_ready",
    "capability_requested",
    "capability_routed",
    "discovered",
    "marked_for_destruction",
    "resolved",
    "running",
    "started",
    "stopped",
];

/// Processes input and produces a list of well-formatted event types
/// as expected by the fuchsia.sys2.EventSource::Subscribe FIDL Call.
/// If an input is invalid, return it as an error.
fn validate_event_types(inputs: Vec<String>) -> Result<Vec<String>, String> {
    assert!(!inputs.is_empty());

    let mut event_types = vec![];
    for input in inputs {
        let fixed_input = input.trim().to_ascii_lowercase();
        if !SYSTEM_EVENT_TYPES.iter().any(|event_type| event_type == &&fixed_input) {
            return Err(input);
        }
        event_types.push(fixed_input);
    }
    Ok(event_types)
}

/// Runs the interactive shell and stores queriable state
pub struct Shell {
    /// Used to create listeners before the component tree has
    /// been started. |None| once component tree has been started.
    event_source: Option<EventSource>,

    /// All event listeners registered via the shell before
    /// the component tree has been started.
    listeners: Vec<EventListener>,
}

impl Shell {
    pub async fn new(event_source: EventSource) -> Self {
        let event_source = Some(event_source);
        Self { event_source, listeners: vec![] }
    }

    /// Prints a list of all listeners registered via the shell
    pub fn print_listeners(&self) {
        println!("Listeners:");
        for (index, drain) in self.listeners.iter().enumerate() {
            println!("{}: {}", index, drain);
        }
    }

    /// Returns |true| if component tree has been started
    async fn process_pre_start_command(&mut self, command: PreStartCommand) -> bool {
        match command {
            PreStartCommand::Start => {
                // Take the event source to prevent it from being used again
                let event_source = self.event_source.take().unwrap();
                event_source.start_component_tree().await;
                println!("Component tree started");
                return true;
            }
            PreStartCommand::Listen { event_types, auto_resume } => {
                match validate_event_types(event_types) {
                    Ok(event_types) => {
                        let event_source = self.event_source.as_ref().unwrap();
                        let listener =
                            EventListener::new(auto_resume, event_types, &event_source).await;
                        self.listeners.push(listener);
                        self.print_listeners();
                    }
                    Err(input) => {
                        eprintln!("Invalid event type: {}", input);
                    }
                };
            }
            PreStartCommand::Listeners => {
                self.print_listeners();
            }
            PreStartCommand::EventTypes => {
                println!("{:?}", SYSTEM_EVENT_TYPES);
            }
            PreStartCommand::Exit => {
                // We were asked to exit immediately. Drop everything.
                std::process::exit(0);
            }
        }
        return false;
    }

    async fn process_post_start_command(&mut self, command: PostStartCommand) {
        assert!(self.event_source.is_none());
        match command {
            PostStartCommand::Events { listener_id, detailed } => {
                if self.listeners.len() <= listener_id {
                    eprintln!("Invalid listener id: {}", listener_id);
                    return;
                }
                let listener = self.listeners.get(listener_id).unwrap();
                listener.print_events(detailed);
            }
            PostStartCommand::Resume { listener_id, event_id } => {
                if self.listeners.len() <= listener_id {
                    eprintln!("Invalid listener id: {}", listener_id);
                    return;
                }

                let listener = self.listeners.get(listener_id).unwrap();
                match listener.resume(event_id).await {
                    Ok(()) => {
                        println!("Resumed event {} caught by listener {}", event_id, listener_id)
                    }
                    Err(e) => eprintln!("{}", e),
                }
            }
            PostStartCommand::Listeners => {
                self.print_listeners();
            }
            PostStartCommand::Exit => {
                // We were asked to exit immediately. Drop everything.
                std::process::exit(0);
            }
        }
    }

    /// Process shell commands indefinitely
    pub async fn run(mut self) {
        // Enable command history
        let config = Config::builder().auto_add_history(true).max_history_size(10).build();
        let mut editor = Editor::<()>::with_config(config);

        // Accept pre-start commands only
        loop {
            let command = read_next_command::<PreStartCommand>(&mut editor);
            if self.process_pre_start_command(command).await {
                break;
            }
        }

        // Accept post-start commands only
        loop {
            let command = read_next_command::<PostStartCommand>(&mut editor);
            self.process_post_start_command(command).await;
        }
    }
}
