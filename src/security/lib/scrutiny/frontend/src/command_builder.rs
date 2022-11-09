// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// The CommandBuilder implements a builder pattern for constructing Scrutiny
/// commands. This is intended to be used by library consumers of the framework
/// to correctly construct commands.
pub struct CommandBuilder {
    /// The full controller name foo.bar.baz
    controller: String,
    /// The set of arguments to add to the command.
    params: Vec<(String, String)>,
}

impl CommandBuilder {
    /// Constructs a new command taking the controller name which must be
    /// present for all commands.
    pub fn new(controller: impl Into<String>) -> CommandBuilder {
        CommandBuilder { controller: controller.into(), params: Vec::new() }
    }

    /// Adds a parameter to the command. Each parameter must contain a name and
    /// a value.
    pub fn param<'a>(
        &'a mut self,
        key: impl Into<String>,
        value: impl Into<String>,
    ) -> &'a mut CommandBuilder {
        self.params.push((key.into(), value.into()));
        self
    }

    /// Constructs the String command from the component parts returning a
    /// correctly constructed Scrutiny command.
    pub fn build(&self) -> String {
        if self.params.is_empty() {
            self.controller.clone()
        } else {
            let params: Vec<String> =
                self.params.iter().map(|(k, v)| ["--", k, " ", v].join("")).collect();
            format!("{} {}", self.controller, params.join(" "))
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_build_controller() {
        assert_eq!(CommandBuilder::new("foo.bar").build(), "foo.bar");
    }

    #[test]
    fn test_build_one_param() {
        let mut command = CommandBuilder::new("foo.bar");
        command.param("key", "value");
        assert_eq!(command.build(), "foo.bar --key value");
    }

    #[test]
    fn test_two_params() {
        let mut command = CommandBuilder::new("foo.bar");
        command.param("key", "value");
        command.param("key2", "value2");
        assert_eq!(command.build(), "foo.bar --key value --key2 value2");
    }
}
