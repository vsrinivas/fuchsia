use serde_json::{Value};
use url::{Url};

use super::super::schema;
use super::super::validators;

#[allow(missing_copy_implementations)]
pub struct Ref;
impl super::Keyword for Ref {
    fn compile(&self, def: &Value, ctx: &schema::WalkContext) -> super::KeywordResult {
        let ref_ = keyword_key_exists!(def, "$ref");

        if ref_.is_string() {
            let url = Url::options().base_url(Some(ctx.url)).parse(ref_.as_str().unwrap());
            match url {
                Ok(url) => {
                    Ok(Some(Box::new(validators::Ref {
                        url: url
                    })))
                },
                Err(_) => {
                    Err(schema::SchemaError::Malformed {
                        path: ctx.fragment.join("/"),
                        detail: "The value of $ref MUST be an URI-encoded JSON Pointer".to_string()
                    })
                }
            }
        } else {
            Err(schema::SchemaError::Malformed {
                path: ctx.fragment.join("/"),
                detail: "The value of multipleOf MUST be a string".to_string()
            })
        }
    }

    fn is_exclusive(&self) -> bool {
        true
    }
}

#[cfg(test)] use super::super::scope;
#[cfg(test)] use super::super::builder;
#[cfg(test)] use serde_json::to_value;

#[test]
fn validate() {
    let mut scope = scope::Scope::new();
    let schema = scope.compile_and_return(builder::schema(|s| {
        s.array();
        s.max_items(2u64);
        s.items_schema(|items| {
            items.ref_("#");
        })
    }).into_json(), true).ok().unwrap();

    let array: Vec<String> = vec![];
    let array2: Vec<Vec<String>> = vec![vec![], vec![]];
    let array3: Vec<Vec<String>> = vec![vec![], vec![], vec![]];

    assert_eq!(schema.validate(&to_value(&array).unwrap()).is_valid(), true);
    assert_eq!(schema.validate(&to_value(&array2).unwrap()).is_valid(), true);

    assert_eq!(schema.validate(&to_value(&array3).unwrap()).is_valid(), false);
    assert_eq!(schema.validate(&to_value(&vec![1,2]).unwrap()).is_valid(), false);
}
