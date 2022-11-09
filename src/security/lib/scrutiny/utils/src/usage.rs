// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// The UsageBuilder provides a simple way to create usages for DataControllers.
/// This provides a unified format for all help commands in Scrutiny.
pub struct UsageBuilder {
    name: String,
    summary: String,
    description: String,
    args: Vec<(String, String)>,
}

impl UsageBuilder {
    pub fn new() -> Self {
        Self {
            name: String::new(),
            summary: String::new(),
            description: String::new(),
            args: Vec::new(),
        }
    }

    /// Sets the name of the command.
    pub fn name<'a>(&'a mut self, name: impl Into<String>) -> &'a mut Self {
        self.name = name.into();
        self
    }

    /// Sets the summary for the usage of this builder.
    pub fn summary<'a>(&'a mut self, summary: impl Into<String>) -> &'a mut Self {
        self.summary = summary.into();
        self
    }

    /// Sets the basic description for the command.
    pub fn description<'a>(&'a mut self, description: impl Into<String>) -> &'a mut Self {
        self.description = description.into();
        self
    }

    /// Sets an argument to be appended to the description section.
    pub fn arg<'a>(&'a mut self, arg: impl Into<String>, desc: impl Into<String>) -> &'a mut Self {
        self.args.push((arg.into(), desc.into()));
        self
    }

    /// Returns the formatted usage statement.
    pub fn build<'a>(&'a mut self) -> String {
        let mut usage = String::new();
        usage.push_str("NAME\n\n");
        usage.push_str(&format!("{:<5}{}", " ", self.name));

        usage.push_str("\n\nSUMMARY\n\n");
        usage.push_str(&format!("{:<5}{}", " ", self.summary));

        // Implement basic word wrapping for descriptions.
        usage.push_str("\n\nDESCRIPTION\n\n");
        let desc_words: Vec<&str> = self.description.split(" ").collect();
        let mut current: Vec<String> = Vec::new();
        let mut count = 0;
        const WRAP_COUNT: usize = 74;
        for word in desc_words.iter() {
            if count + word.len() > WRAP_COUNT {
                usage.push_str(&format!("{:<5}{}\n", " ", current.join(" ")));
                current = Vec::new();
                count = 0;
            }
            count += word.len();
            current.push(word.to_string());
        }
        if !current.is_empty() {
            usage.push_str(&format!("{:<5}{}\n", " ", current.join(" ")));
        }

        usage.push_str("\n");
        for (arg, desc) in self.args.iter() {
            usage.push_str(&format!("{:<5}{}\n", " ", arg));
            usage.push_str(&format!("{:<5}{:<5}{}\n", " ", " ", desc));
        }
        usage
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_usage_builder() {
        let usage = UsageBuilder::new()
            .name("foo")
            .summary("bar")
            .description("baz")
            .arg("foo1", "foo1arg")
            .arg("foo2", "foo2arg")
            .build();
        assert_eq!(usage.contains("NAME"), true);
        assert_eq!(usage.contains("SUMMARY"), true);
        assert_eq!(usage.contains("DESCRIPTION"), true);
        assert_eq!(usage.contains("foo"), true);
        assert_eq!(usage.contains("bar"), true);
        assert_eq!(usage.contains("baz"), true);
        assert_eq!(usage.contains("foo1"), true);
        assert_eq!(usage.contains("foo1arg"), true);
        assert_eq!(usage.contains("foo2"), true);
        assert_eq!(usage.contains("foo2arg"), true);
    }
}
