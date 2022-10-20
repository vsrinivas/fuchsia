// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::device_info::DeviceInfoImpl;
use anyhow::Error;
use handlebars::Handlebars;
use mockall::automock;

/// Trait version of Handlebars useful for mocking.
#[automock]
pub trait TemplateEngine: Send + Sync {
    fn render(&self, name: &str, data: &DeviceInfoImpl) -> Result<String, Error>;
    fn register_resource(&mut self, name: &str, tpl_path: &str) -> Result<(), Error>;
}

impl<'_a> TemplateEngine for Handlebars<'_a> {
    fn render(&self, name: &str, data: &DeviceInfoImpl) -> Result<String, Error> {
        Ok(self.render(name, data)?)
    }

    fn register_resource(&mut self, name: &str, tpl_path: &str) -> Result<(), Error> {
        Ok(self.register_template_file(name, tpl_path)?)
    }
}

/// Registers templates with Handlebars.
///
/// Registration fails if a "$TEMPLATE_NAME.hbs.html" is not available as
/// a component resource.
///
/// See BUILD.gn's resource("templates") rule for adding new templates as
/// component resources.
pub fn register_template_resources(
    template_engine: &mut Box<dyn TemplateEngine>,
    template_names: Vec<&str>,
) -> Result<(), Error> {
    for name in template_names {
        let path = format!("/pkg/templates/{name}.hbs.html");
        match (*template_engine).register_resource(name, &path) {
            Ok(_) => {
                println!("Registered template: {name:?} : {path:?}");
            }
            Err(e) => {
                println!("Problem registering template: {name:?} : {path:?} : {e:?}");
                println!("Is template file built into component as a resource?");
                return Err(e.into());
            }
        };
    }

    Ok(())
}

#[cfg(test)]
mod tests {
    use crate::handlebars_utils::{
        register_template_resources, MockTemplateEngine, TemplateEngine,
    };
    use anyhow::{anyhow, Error};
    use mockall::predicate::{always, eq};
    use mockall::Sequence;

    /// Verifies registering 3 templates results in 3 register_template_file() calls.
    #[test]
    fn register_templates_generates_engine_calls() -> Result<(), Error> {
        let mut template_engine = MockTemplateEngine::new();

        // Mock a Handlebars expecting 3 template files to be registered.
        let mut call_sequence = Sequence::new();
        let resources = vec!["1", "2", "3"];
        for resource in &resources {
            template_engine
                .expect_register_resource()
                .with(eq(*resource), always())
                .times(1)
                .returning(|_, _| Ok(()))
                .in_sequence(&mut call_sequence);
        }

        let mut boxed_template_engine: Box<dyn TemplateEngine> = Box::new(template_engine);

        register_template_resources(&mut boxed_template_engine, resources)
    }

    /// Verifies register_resource() errors percolated up.
    #[test]
    fn register_templates_percolates_template_error() {
        let mut template_engine = MockTemplateEngine::new();

        // Only 1 call expected because registering stops after first failure.
        template_engine
            .expect_register_resource()
            .with(eq("1"), always())
            .times(1)
            .returning(|_, _| Err(anyhow!("Error!")));

        let mut boxed_template_engine: Box<dyn TemplateEngine> = Box::new(template_engine);

        // Request to register 2 resources -- but should die at first failure.
        assert!(register_template_resources(&mut boxed_template_engine, vec!["1", "2"]).is_err());
    }
}
