// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use handlebars::{handlebars_helper, Handlebars};
use heck::{CamelCase, ShoutySnakeCase, SnekCase};

// heck::CamelCase is actually PascalCase.
handlebars_helper!(pascal_case: |arg: str| arg.to_camel_case());

handlebars_helper!(snake_case: |arg: str| arg.to_snek_case());

handlebars_helper!(screaming_snake_case: |arg: str| arg.to_shouty_snake_case());

/// Register all applicable helper template methods.
pub fn register_helpers(handlebars: &mut Handlebars<'_>) {
    // `{{pascal_case arg}}` converts `arg` to PascalCase.
    handlebars.register_helper("pascal_case", Box::new(pascal_case));

    // `{{snake_case arg}}` converts `arg` to snake_case.
    handlebars.register_helper("snake_case", Box::new(snake_case));

    // `{{screaming_snake_case arg}}` converts `arg` to SCREAMING_SNAKE_CASE.
    handlebars.register_helper("screaming_snake_case", Box::new(screaming_snake_case));
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde::Serialize;

    #[derive(Serialize)]
    struct Args {
        var: String,
    }

    #[test]
    fn helpers_work() {
        let mut handlebars = Handlebars::new();
        handlebars.set_strict_mode(true);
        register_helpers(&mut handlebars);

        let args = Args { var: "foo_bar".to_string() };
        let render =
            handlebars.render_template("{{pascal_case var}}", &args).expect("failed to render");
        assert_eq!(render, "FooBar");

        let args = Args { var: "FooBar".to_string() };
        let render =
            handlebars.render_template("{{snake_case var}}", &args).expect("failed to render");
        assert_eq!(render, "foo_bar");

        let args = Args { var: "FooBar".to_string() };
        let render = handlebars
            .render_template("{{screaming_snake_case var}}", &args)
            .expect("failed to render");
        assert_eq!(render, "FOO_BAR");
    }
}
