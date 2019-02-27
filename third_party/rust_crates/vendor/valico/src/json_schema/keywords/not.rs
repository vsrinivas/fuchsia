use serde_json::{Value};

use super::super::schema;
use super::super::validators;
use super::super::helpers;

#[allow(missing_copy_implementations)]
pub struct Not;
impl super::Keyword for Not {
    fn compile(&self, def: &Value, ctx: &schema::WalkContext) -> super::KeywordResult {
        let not = keyword_key_exists!(def, "not");

        if not.is_object() {
            Ok(Some(Box::new(validators::Not {
                url: helpers::alter_fragment_path(ctx.url.clone(), [
                        ctx.escaped_fragment().as_ref(),
                        "not"
                     ].join("/"))
            })))
        } else {
            Err(schema::SchemaError::Malformed {
                path: ctx.fragment.join("/"),
                detail: "The value of `not` MUST be an object".to_string()
            })
        }
    }
}