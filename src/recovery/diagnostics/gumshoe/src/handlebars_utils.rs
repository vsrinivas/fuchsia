// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::device_info::DeviceInfoImpl;
use anyhow::{anyhow, Error};
use handlebars::Handlebars;
use mockall::automock;
use std::path::Path;

const FILE_NAME_NOT_AVAILABLE_ERROR: &str = "file_name not available";
const FILE_NAME_NOT_UTF8_ERROR: &str = "file_name not UTF8";
const FILE_NAME_MISSING_SUFFIX_ERROR: &str = "file_name missing suffix";

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

/// Returns the "template name" for a path (eg "foo" for "/bar/foo.hbs.html").
fn template_name_from_path(path_str: &str) -> Result<String, Error> {
    let path = Path::new(path_str);

    let full_name = path.file_name().ok_or(anyhow!(FILE_NAME_NOT_AVAILABLE_ERROR))?;
    let full_name_str = full_name.to_str().ok_or(anyhow!(FILE_NAME_NOT_UTF8_ERROR))?;
    let prefix_end = full_name_str.find(".").ok_or(anyhow!(FILE_NAME_MISSING_SUFFIX_ERROR))?;
    let prefix = &full_name_str[..prefix_end];

    Ok(prefix.to_string())
}

/// Registers templates with Handlebars.
///
/// Registration fails if a "$TEMPLATE_NAME.hbs.html" is not available as
/// a component resource.
///
/// See BUILD.gn's resource("templates") rule for adding new templates as
/// component resources.
pub fn register_template_resources<'a>(
    template_engine: &mut Box<dyn TemplateEngine>,
    template_paths: impl Iterator<Item = &'a str>,
) -> Result<(), Error> {
    for template_path in template_paths {
        let template_name = template_name_from_path(template_path)?;

        // Register the template at given path using the file name's prefix.
        (*template_engine).register_resource(&template_name, template_path)?;

        println!("Registered: {:?} from {:?}", template_name, template_path);
    }

    Ok(())
}

#[cfg(test)]
mod tests {
    use crate::handlebars_utils::{
        register_template_resources, template_name_from_path, MockTemplateEngine, TemplateEngine,
        FILE_NAME_MISSING_SUFFIX_ERROR,
    };
    use anyhow::{anyhow, Error};
    use mockall::predicate::eq;
    use mockall::Sequence;

    const TEMPLATE_ENGINE_ERROR: &str = "Template Engine Error!";

    /// Verifies provided templates result in register_resource() calls.
    #[test]
    fn register_template_resources_generates_engine_calls() -> Result<(), Error> {
        let mut template_engine = MockTemplateEngine::new();

        // Mock a Handlebars expecting 3 template files to be registered.
        let mut call_sequence = Sequence::new();
        let resources = vec![("a", "a.suffix"), ("b", "/dir/b.suffix"), ("c", "/dir.dir/c.suffix")];
        for (name, path) in &resources {
            template_engine
                .expect_register_resource()
                .with(eq(*name), eq(*path))
                .times(1)
                .returning(|_, _| Ok(()))
                .in_sequence(&mut call_sequence);
        }

        let mut boxed_template_engine: Box<dyn TemplateEngine> = Box::new(template_engine);

        register_template_resources(
            &mut boxed_template_engine,
            vec!["a.suffix", "/dir/b.suffix", "/dir.dir/c.suffix"].into_iter(),
        )
    }

    /// Verifies register_template_resources() fails when given a file without a suffix.
    /// Suffixes are required to disambiguate "filenames" from "logical template names."
    #[test]
    fn register_template_resources_fails_when_passed_file_without_suffix() {
        let template_engine = MockTemplateEngine::new();

        let mut boxed_template_engine: Box<dyn TemplateEngine> = Box::new(template_engine);

        // Expect error since there's no "." in the path's filename.
        let result =
            register_template_resources(&mut boxed_template_engine, vec!["no_suffix"].into_iter());

        assert!(result.is_err(), "register_template_resources should return an error.");
        assert_eq!(
            result.err().unwrap().to_string(),
            FILE_NAME_MISSING_SUFFIX_ERROR,
            "register_template_resources should return a FILE_NAME_MISSING_SUFFIX_ERROR",
        );
    }

    /// Verifies register_template_resources() percolates register_resource() errors.
    #[test]
    fn register_template_resources_percolates_template_error() {
        let mut template_engine = MockTemplateEngine::new();

        // Only 1 call expected because registering stops after first failure.
        template_engine
            .expect_register_resource()
            .with(eq("a"), eq("a.suffix"))
            .times(1)
            .returning(|_, _| Err(anyhow!(TEMPLATE_ENGINE_ERROR)));

        let mut boxed_template_engine: Box<dyn TemplateEngine> = Box::new(template_engine);

        // Request to register 2 resources -- but should fail at first failure.
        let result = register_template_resources(
            &mut boxed_template_engine,
            vec!["a.suffix", "b.suffix"].into_iter(),
        );

        assert!(result.is_err(), "register_template_resources should return an error");
        assert_eq!(
            result.err().unwrap().to_string(),
            TEMPLATE_ENGINE_ERROR,
            "register_template_resources should return a TEMPLATE_ENGINE_ERROR",
        );
    }

    #[test]
    /// Verifies template_name_from_path() handles valid paths correctly.
    fn names_successfully_resolved_from_valid_template_paths() {
        for (name, path) in vec![
            ("a", "a.foo"),
            ("b", "b.foo.bar"),
            ("c", "c.foo.bar.baz"),
            ("d", "/z/d.foo.bar"),
            ("e", "/z/y/e.foo.bar"),
        ] {
            assert_eq!(template_name_from_path(path).expect("Valid name from path"), name);
        }
    }

    #[test]
    /// Verifies template_name_from_path() handles invalid paths correctly.
    fn names_not_resolved_from_invalid_template_paths() {
        for bad_name in vec!["", "a", "/z/b", "/z/y/c"] {
            assert!(template_name_from_path(&bad_name).is_err());
        }
    }
}
