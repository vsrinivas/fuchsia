use std::fs;
use std::path;
use std::io::Read;
use serde_json::{Value, from_str, to_value, to_string_pretty};
use valico::json_schema;

fn visit_specs<F>(dir: &path::Path, cb: F) where F: Fn(&path::Path, Value) + Copy {
    let contents = fs::read_dir(dir).ok().unwrap();
    for entry in contents {
        let entry = entry.unwrap();
        let path = entry.path();
        if entry.file_type().unwrap().is_dir() {
            visit_specs(&path, cb);
        } else {
            match fs::File::open(&path) {
                Err(_) => continue,
                Ok(mut file) => {
                    let metadata = file.metadata().unwrap();
                    if metadata.is_file() {
                        let mut content = String::new();
                        file.read_to_string(&mut content).ok().unwrap();
                        let json: Value = from_str(&content).unwrap();
                        cb(&path, json);
                    }
                }
            }
        }
    }
}

#[test]
fn test_suite() {
    let mut content = String::new();

    fs::File::open(&path::Path::new("tests/schema/schema.json")).ok().unwrap()
        .read_to_string(&mut content).ok().unwrap();

    let json_v4_schema: Value = from_str(&content).unwrap();

    visit_specs(&path::Path::new("tests/schema/JSON-Schema-Test-Suite/tests/draft4"), |path, spec_set: Value| {
        let spec_set = spec_set.as_array().unwrap();
        let exceptions: Vec<(String, String)> = vec![
            ("maxLength.json".to_string(), "two supplementary Unicode code points is long enough".to_string()),
            ("minLength.json".to_string(), "one supplementary Unicode code point is not long enough".to_string()),
            ("refRemote.json".to_string(), "remote ref invalid".to_string()),
            ("refRemote.json".to_string(), "remote fragment invalid".to_string()),
            ("refRemote.json".to_string(), "ref within ref invalid".to_string()),
            ("refRemote.json".to_string(), "changed scope ref invalid".to_string()),
            ("refRemote.json".to_string(), "base URI change ref invalid".to_string()),
            ("refRemote.json".to_string(), "string is invalid".to_string()),
            ("refRemote.json".to_string(), "object is invalid".to_string()),
            ("bignum.json".to_string(), "a bignum is an integer".to_string()),
            ("bignum.json".to_string(), "a negative bignum is an integer".to_string()),
            ("ecmascript-regex.json".to_string(), "ECMA 262 has no support for \\Z anchor from .NET".to_string()),
        ];

        for spec in spec_set.iter() {
            let spec = spec.as_object().unwrap();
            let mut scope = json_schema::Scope::new();

            scope.compile(json_v4_schema.clone(), true).ok().unwrap();

            let spec_desc = spec.get("description").map(|v| v.as_str().unwrap()).unwrap_or("");

            let schema = match scope.compile_and_return(spec.get("schema").unwrap().clone(), false) {
                Ok(schema) => schema,
                Err(err) => panic!("Error in schema {} {}: {:?}",
                    path.file_name().unwrap().to_str().unwrap(),
                    spec.get("description").unwrap().as_str().unwrap(),
                    err
                )
            };

            let tests = spec.get("tests").unwrap().as_array().unwrap();

            for test in tests.iter() {
                let test = test.as_object().unwrap();
                let description = test.get("description").unwrap().as_str().unwrap();
                let data = test.get("data").unwrap();
                let valid = test.get("valid").unwrap().as_bool().unwrap();

                let state = schema.validate(&data);

                if state.is_valid() != valid {
                    if !&exceptions[..].contains(&(path.file_name().unwrap().to_str().unwrap().to_string(), description.to_string())) {
                        panic!("Failure: \"{}\" in \"{}\" -> \"{}\" with state: \n {}",
                            path.file_name().unwrap().to_str().unwrap(),
                            spec_desc,
                            description.to_string(),
                            to_string_pretty(&to_value(&state).unwrap()).unwrap()
                        )
                    }
                } else {
                    println!("test json_schema::test_suite -> {} .. ok", description);
                }
            }
        }
    })
}
