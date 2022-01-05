// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Handlebars helper functions for working with an EmulatorConfiguration.

use ffx_emulator_config::DataUnits;
use handlebars::{
    Context, Handlebars, Helper, HelperDef, HelperResult, JsonRender, Output, RenderContext,
    RenderError,
};

#[derive(Clone, Copy)]
pub struct EqHelper {}

/// This Handlebars helper performs equality comparison of two parameters, and
/// returns a value which can be interpreted as "truthy" or not by the #if
/// built-in.
///
/// Example:
///
///   {{#if (eq var1 var2)}}
///       // Stuff you want if they match.
///   {{else}}
///       // Stuff you want if they don't.
///   {{/if}}
///
/// You can also embed literal values, rather than parameters, like this:
///
///   {{#if (eq "string" 42)}}
///       // String and Number don't match, but you can compare them.
///   {{/if}}
///   {{#if (eq "literal value" param.in.a.structure)}}
///       // Either parameter can be a literal or a variable.
///   {{/if}}
///
impl HelperDef for EqHelper {
    fn call<'reg: 'rc, 'rc>(
        &self,
        h: &Helper<'reg, 'rc>,
        _: &'reg Handlebars<'reg>,
        _: &'rc Context,
        _: &mut RenderContext<'reg, 'rc>,
        out: &mut dyn Output,
    ) -> HelperResult {
        let first = h
            .param(0)
            .ok_or_else(|| RenderError::new("First param not found for helper \"eq\""))?;
        let second = h
            .param(1)
            .ok_or_else(|| RenderError::new("Second param not found for helper \"eq\""))?;

        // Compare the value of the two parameters, and writes "true" (which is truthy) to "out" if
        // they evaluate as equal. In the context of an "#if" helper, this causes the "true" branch
        // to be rendered if and only if the two are equal, and the "else" branch otherwise.
        if first.value() == second.value() {
            out.write("true")?;
        }
        Ok(())
    }
}

#[derive(Clone, Copy)]
pub struct UnitAbbreviationHelper {}

/// This Handlebars helper is used for substitution of a DataUnits variable,
/// only instead of printing the normal serialized form, it outputs the
/// abbreviated form.
///
/// Example:
///
///     let megs = DataUnits::Megabytes;
///
///   Template:
///
///     "{{megs}} abbreviates to {{#abbr megs}}."  {{! output: "megabytes abbreviates to M." }}
///
/// It also works if the template includes the serialized form of a DataUnits value, for example:
///
///     "Short form is {{ua "megabytes"}}."  {{! output: "Short form is M." }}
///
/// If the template wraps a variable that is not a DataUnits type with this helper, the helper
/// will render the full serialized value as normal.
///
///     let os = OperatingSystem::Linux;
///     let name = String::From("something");
///
///     "{{ua name}} and {{ua os}} aren't DataUnits."
///         {{! output: "something and linux aren't DataUnits."}}
///
impl HelperDef for UnitAbbreviationHelper {
    fn call<'reg: 'rc, 'rc>(
        &self,
        h: &Helper<'reg, 'rc>,
        _: &'reg Handlebars<'reg>,
        _: &'rc Context,
        _: &mut RenderContext<'reg, 'rc>,
        out: &mut dyn Output,
    ) -> HelperResult {
        // Convert the serde_json::Value into a DataUnits.
        let param =
            h.param(0).ok_or(RenderError::new(format!("Parameter 0 is missing in {:?}", h)))?;

        match serde_json::from_value::<DataUnits>(param.value().clone()) {
            Ok(units) => out.write(units.abbreviate())?,
            Err(_) => out.write(param.value().render().as_ref())?,
        };
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use ffx_emulator_config::DataUnits;
    use serde::Serialize;

    #[derive(Serialize)]
    struct StringStruct {
        pub units: String,
    }

    #[derive(Serialize)]
    struct UnitsStruct {
        pub units: DataUnits,
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_ua_helper() {
        let template = "{{ua units}}";
        let units_struct = UnitsStruct { units: DataUnits::Gigabytes };
        let string_struct = StringStruct { units: "Gigabytes".to_string() };

        let mut handlebars = Handlebars::new();
        handlebars.set_strict_mode(true);
        handlebars.register_helper("ua", Box::new(UnitAbbreviationHelper {}));

        let json = handlebars.render_template(&template, &string_struct);
        assert!(json.is_ok());
        assert_eq!(json.unwrap(), "Gigabytes");

        let json = handlebars.render_template(&template, &units_struct);
        assert!(json.is_ok());
        assert_eq!(json.unwrap(), "G");
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_eq_helper() {
        let template = r#"{{eq units "Gigabytes"}}"#;
        let if_template = r#"{{#if (eq units "Gigabytes")}}yes{{else}}no{{/if}}"#;
        let mut string_struct = StringStruct { units: "Gigabytes".to_string() };

        let mut handlebars = Handlebars::new();
        handlebars.set_strict_mode(true);
        handlebars.register_helper("eq", Box::new(EqHelper {}));

        let json = handlebars.render_template(&template, &string_struct);
        assert!(json.is_ok());
        assert_eq!(json.unwrap(), "true");

        let json = handlebars.render_template(&if_template, &string_struct);
        assert!(json.is_ok());
        assert_eq!(json.unwrap(), "yes");

        string_struct.units = "Something Else".to_string();

        let json = handlebars.render_template(&template, &string_struct);
        assert!(json.is_ok());
        assert_eq!(json.unwrap(), "");

        let json = handlebars.render_template(&if_template, &string_struct);
        assert!(json.is_ok());
        assert_eq!(json.unwrap(), "no");
    }
}
