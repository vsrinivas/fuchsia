use url;
use std::collections;
use serde_json::{Value};

use super::schema;
use super::keywords;
use super::helpers;

#[allow(dead_code)]
#[derive(Debug)]
pub struct Scope {
    keywords: keywords::KeywordMap,
    schemes: collections::HashMap<String, schema::Schema>,
}

#[allow(dead_code)]
impl Scope {
    pub fn new() -> Scope {
        let mut scope = Scope {
            keywords: keywords::default(),
            schemes: collections::HashMap::new()
        };

        scope.add_keyword(vec!["format"], keywords::format::Format::new());
        scope
    }

    pub fn without_formats() -> Scope {
        Scope {
            keywords: keywords::default(),
            schemes: collections::HashMap::new()
        }
    }

    pub fn with_formats<F>(build_formats: F) -> Scope where F: FnOnce(&mut keywords::format::FormatBuilders) {
        let mut scope = Scope {
            keywords: keywords::default(),
            schemes: collections::HashMap::new()
        };

        scope.add_keyword(vec!["format"], keywords::format::Format::with(build_formats));
        scope
    }

    pub fn compile(&mut self, def: Value, ban_unknown: bool) -> Result<url::Url, schema::SchemaError> {
        let schema = try!(schema::compile(def, None, schema::CompilationSettings::new(&self.keywords, ban_unknown)));
        let id = schema.id.clone().unwrap();
        try!(self.add(&id, schema));
        Ok(id)
    }

    pub fn compile_with_id(&mut self, id: &url::Url, def: Value, ban_unknown: bool)
        -> Result<(), schema::SchemaError>
    {
        let schema = try!(schema::compile(def, Some(id.clone()), schema::CompilationSettings::new(&self.keywords, ban_unknown)));
        self.add(id, schema)
    }

    pub fn compile_and_return<'a>(&'a mut self, def: Value, ban_unknown: bool)
        -> Result<schema::ScopedSchema<'a>, schema::SchemaError>
    {
        let schema = try!(schema::compile(def, None, schema::CompilationSettings::new(&self.keywords, ban_unknown)));
        self.add_and_return(schema.id.clone().as_ref().unwrap(), schema)
    }

    pub fn compile_and_return_with_id<'a>(&'a mut self, id: &url::Url, def: Value, ban_unknown: bool)
        -> Result<schema::ScopedSchema<'a>, schema::SchemaError>
    {
        let schema = try!(schema::compile(def, Some(id.clone()), schema::CompilationSettings::new(&self.keywords, ban_unknown)));
        self.add_and_return(id, schema)
    }

    pub fn add_keyword<T>(&mut self, keys: Vec<&'static str>, keyword: T) where T: keywords::Keyword + 'static {
        keywords::decouple_keyword((keys, Box::new(keyword)), &mut self.keywords);
    }

    fn add(&mut self, id: &url::Url, schema: schema::Schema) -> Result<(), schema::SchemaError> {
        let (id_str, fragment) = helpers::serialize_schema_path(id);

        match fragment {
            Some(_) => return Err(schema::SchemaError::WrongId),
            None => ()
        }

        if !self.schemes.contains_key(&id_str) {
            self.schemes.insert(id_str, schema);
            Ok(())
        } else {
            Err(schema::SchemaError::IdConflicts)
        }
    }

    fn add_and_return<'a>(&'a mut self, id: &url::Url, schema: schema::Schema)
        -> Result<schema::ScopedSchema<'a>, schema::SchemaError>
    {
        let (id_str, fragment) = helpers::serialize_schema_path(id);

        match fragment {
            Some(_) => return Err(schema::SchemaError::WrongId),
            None => ()
        }

        if !self.schemes.contains_key(&id_str) {
            self.schemes.insert(id_str.clone(), schema);
            Ok(schema::ScopedSchema::new(self, self.schemes.get(&id_str).unwrap()))
        } else {
            Err(schema::SchemaError::IdConflicts)
        }
    }

    pub fn resolve<'a>(&'a self, id: &url::Url) -> Option<schema::ScopedSchema<'a>> {
        let (schema_path, fragment) = helpers::serialize_schema_path(id);

        let schema = self.schemes.get(&schema_path).or_else(|| {
            // Searching for inline schema in O(N)
            for (_, schema) in self.schemes.iter() {
                let internal_schema = schema.resolve(schema_path.as_ref());
                if internal_schema.is_some() {
                    return internal_schema
                }
            }

            None
        });

        schema.and_then(|schema| {
            match fragment {
                Some(ref fragment) => {
                    schema.resolve_fragment(fragment.as_ref()).map(|schema| {
                        schema::ScopedSchema::new(self, schema)
                    })
                },
                None => Some(schema::ScopedSchema::new(self, schema))
            }
        })
    }
}

#[cfg(test)]
use jsonway;

#[test]
fn lookup() {
    let mut scope = Scope::new();

    scope.compile(jsonway::object(|schema| {
        schema.set("id", "http://example.com/schema".to_string())
    }).unwrap(), false).ok().unwrap();

    scope.compile(jsonway::object(|schema| {
        schema.set("id", "http://example.com/schema#sub".to_string());
        schema.object("subschema", |subschema| {
            subschema.set("id", "#subschema".to_string());
        })
    }).unwrap(), false).ok().unwrap();

    assert!(scope.resolve(&url::Url::parse("http://example.com/schema").ok().unwrap()).is_some());
    assert!(scope.resolve(&url::Url::parse("http://example.com/schema#sub").ok().unwrap()).is_some());
    assert!(scope.resolve(&url::Url::parse("http://example.com/schema#sub/subschema").ok().unwrap()).is_some());
    assert!(scope.resolve(&url::Url::parse("http://example.com/schema#subschema").ok().unwrap()).is_some());
}