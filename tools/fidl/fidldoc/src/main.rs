// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{anyhow, bail, Context, Error};
use argh::FromArgs;
use std::collections::{BTreeMap, HashMap};
use std::fs;
use std::path::{Path, PathBuf};
use std::process;
use tracing::{error, info};

use rayon::prelude::*;

use serde_json::{json, Value};

mod fidljson;
use fidljson::{
    to_lower_snake_case, FidlJson, FidlJsonPackageData, TableOfContents, TableOfContentsItem,
};

mod templates;
use templates::markdown::MarkdownTemplate;
use templates::syscall::SyscallTemplate;
use templates::FidldocTemplate;

static FIDLDOC_CONFIG_PATH: &str = "fidldoc.config.json";
static ATTR_NAME_DOC: &'static str = "doc";
static ATTR_NAME_AVAILABLE: &'static str = "available";
static ATTR_NAME_ADDED: &'static str = "added";
static ATTR_NAME_NO_DOC: &'static str = "no_doc";
static HEAD_VERSION: &'static str = "HEAD";
static HEAD_VERSION_NUMBER: &'static u64 = &(u64::MAX - 1);

#[derive(Debug)]
enum TemplateType {
    Markdown,
    Syscall,
    // Overtime, we'll want to add more rendering options such as HTML.
}

fn parse_template_type_str(value: &str) -> Result<TemplateType, String> {
    match &value.to_lowercase()[..] {
        "markdown" => Ok(TemplateType::Markdown),
        "syscall" => Ok(TemplateType::Syscall),
        _ => Err("invalid template type".to_string()),
    }
}

#[derive(Debug, FromArgs)]
/// FIDL documentation generator.
struct Opt {
    #[argh(option, short = 'c')]
    /// path to a configuration file to provide additional options
    config: Option<PathBuf>,
    #[argh(option, default = "\"main\".to_string()")]
    /// current commit hash, useful to coordinate doc generation with a specific source code revision
    tag: String,
    #[argh(positional)]
    /// set the input file(s) to use
    input: Vec<PathBuf>,
    #[argh(option, short = 'o', default = "\"/tmp/fidldoc/\".to_string()")]
    /// set the output folder
    out: String,
    #[argh(switch, short = 'm')]
    /// merge generated files into output folder (don't delete anything existing)
    merge: bool,
    #[argh(option, short = 'p', default = "\"/\".to_string()")]
    /// set the base URL path for the generated docs
    path: String,
    #[argh(
        option,
        short = 't',
        from_str_fn(parse_template_type_str),
        default = "TemplateType::Markdown"
    )]
    /// select the template to use to render the docs
    template: TemplateType,
    #[argh(switch, short = 'v')]
    /// generate verbose output
    verbose: bool,
    #[argh(switch)]
    /// do not generate any output
    silent: bool,
}

fn main() {
    let opt: Opt = argh::from_env();
    if let Err(e) = run(opt) {
        error!("Error: {}", e);
        process::exit(1);
    }
}

fn run(opt: Opt) -> Result<(), Error> {
    let mut input_files = opt.input;
    normalize_input_files(&mut input_files);

    let output = &opt.out;
    let output_path = PathBuf::from(output);

    let url_path = &opt.path;

    let template_type = &opt.template;
    let template = select_template(template_type, &output_path)
        .with_context(|| format!("Unable to instantiate template {:?}", template_type))?;

    if opt.silent && opt.verbose {
        bail!("cannot use --silent and --verbose together");
    }

    if opt.verbose {
        tracing_subscriber::fmt().compact().with_max_level(tracing::Level::INFO).init();
    } else {
        tracing_subscriber::fmt().compact().with_max_level(tracing::Level::ERROR).init();
    }

    // Read in fidldoc.config.json
    let fidl_config_file = match opt.config {
        Some(filepath) => filepath,
        None => get_fidldoc_config_default_path()
            .with_context(|| format!("Unable to retrieve default config file location"))?,
    };
    info!("Using config file from {}", fidl_config_file.display());
    let fidl_config = read_fidldoc_config(&fidl_config_file)
        .with_context(|| format!("Error parsing {}", &fidl_config_file.display()))?;

    create_output_dir(opt.merge, &output_path)
        .with_context(|| format!("Unable to create output directory {}", output_path.display()))?;

    // Parse input files to get declarations, package set and fidl json map
    let FidlJsonPackageData { fidl_json_map } = process_fidl_json_files(input_files.to_vec());

    // The table of contents lists all packages in alphabetical order.
    let table_of_contents = create_toc(&fidl_json_map);

    // Modifications to the fidldoc object
    let main_fidl_doc = json!({
        "table_of_contents": table_of_contents,
        "config": fidl_config,
        "url_path": url_path,
    });

    // Copy static files
    template.include_static_files().expect("Unable to copy static files");

    // Create main page
    template.render_main_page(&main_fidl_doc).expect("Unable to render main page");

    let tag = &opt.tag;
    let output_path_string = &output_path.display();
    fidl_json_map
        .par_iter()
        .try_for_each(|(package, package_fidl_json)| {
            render_fidl_library(
                package,
                package_fidl_json,
                &table_of_contents,
                &fidl_config,
                &tag,
                &url_path,
                &template_type,
                &output_path,
            )
        })
        .expect("Unable to write FIDL reference files");

    if !opt.silent {
        println!("Generated documentation at {}", &output_path_string);
    }
    Ok(())
}
/// If an attribute only has one argument, returns that argument's value.  If the number of
/// arguments is not equal to 1, or the argument's value could not be resolved, error instead.
// TODO(fxbug.dev/81390): Attribute values may only be string literals for now. Make sure to fix
//  this API once that changes to resolve the constant value for all constant types.
fn get_attribute_standalone_arg_value(attribute: &Value) -> Result<String, Error> {
    let args = attribute["arguments"].as_array().expect("Arguments invalid");
    match args.len() {
        0 => Err(anyhow!("attribute {} has no arguments", attribute["name"])),
        1 => {
            let value = &args[0]["value"];
            if value["kind"] != "literal" {
                Err(anyhow!(
                    "attribute {} argument is {} not a string literal",
                    attribute["name"],
                    value["kind"]
                ))
            } else {
                Ok(value["value"]
                    .as_str()
                    .expect("Unable to retrieve string value for this attribute")
                    .to_string())
            }
        }
        _ => Err(anyhow!("attribute {} has multiple arguments", attribute["name"])),
    }
}

fn render_fidl_library(
    package: &String,
    package_fidl_json: &FidlJson,
    table_of_contents: &TableOfContents,
    fidl_config: &Value,
    tag: &String,
    url_path: &String,
    template_type: &TemplateType,
    output_path: &PathBuf,
) -> Result<(), Error> {
    // Modifications to the fidldoc object
    let fidl_doc = json!({
        "name": package_fidl_json.name,
        "maybe_attributes": package_fidl_json.maybe_attributes,
        "library_dependencies": package_fidl_json.library_dependencies,
        "bits_declarations": package_fidl_json.bits_declarations,
        "const_declarations": package_fidl_json.const_declarations,
        "enum_declarations": package_fidl_json.enum_declarations,
        "protocol_declarations": package_fidl_json.protocol_declarations,
        "table_declarations": package_fidl_json.table_declarations,
        "struct_declarations": package_fidl_json.struct_declarations,
        "alias_declarations": package_fidl_json.alias_declarations,
        "union_declarations": package_fidl_json.union_declarations,
        "declaration_order": package_fidl_json.declaration_order,
        "declarations": package_fidl_json.declarations,
        "table_of_contents": table_of_contents,
        "config": fidl_config,
        "tag": tag,
        "url_path": url_path,
    });

    let template = select_template(&template_type, &output_path)
        .with_context(|| format!("Unable to instantiate template {:?}", template_type));
    match template?.render_library(&package, &fidl_doc) {
        Err(why) => error!("Unable to render library {}: {:?}", &package, why),
        Ok(()) => info!("Generated library documentation for {}", &package),
    }

    Ok(())
}

fn select_template<'a>(
    template_type: &TemplateType,
    output_path: &'a PathBuf,
) -> Result<Box<dyn FidldocTemplate + 'a>, Error> {
    // Instantiate the template selected by the user
    let template: Box<dyn FidldocTemplate> = match template_type {
        TemplateType::Markdown => {
            let template = MarkdownTemplate::new(&output_path);
            Box::new(template)
        }
        TemplateType::Syscall => {
            let template = SyscallTemplate::new(&output_path);
            Box::new(template)
        }
    };
    Ok(template)
}

fn get_fidldoc_config_default_path() -> Result<PathBuf, Error> {
    // If the fidldoc config file is not available, it should be found
    // in the same directory as the executable.
    // This needs to be calculated at runtime.
    let fidldoc_executable = std::env::current_exe()?;
    let fidldoc_execution_directory = fidldoc_executable.parent().unwrap();
    let fidl_config_default_path = fidldoc_execution_directory.join(FIDLDOC_CONFIG_PATH);
    Ok(fidl_config_default_path)
}

fn read_fidldoc_config(config_path: &Path) -> Result<Value, Error> {
    let fidl_config_str = fs::read_to_string(config_path)
        .with_context(|| format!("Couldn't open file {}", config_path.display()))?;
    Ok(serde_json::from_str(&fidl_config_str)?)
}

fn should_process_fidl_json(fidl_json: &FidlJson) -> bool {
    if fidl_json
        .maybe_attributes
        .iter()
        .any(|attr| to_lower_snake_case(attr["name"].as_str().unwrap_or("")) == ATTR_NAME_NO_DOC)
    {
        info!("Skipping library with @no_doc attribute: {}", fidl_json.name);
        return false;
    }

    true
}

fn process_fidl_json_files(input_files: Vec<PathBuf>) -> FidlJsonPackageData {
    let mut package_data = FidlJsonPackageData::new();
    for file in input_files {
        let fidl_file_path = PathBuf::from(&file);
        let mut fidl_json = match FidlJson::from_path(&fidl_file_path) {
            Err(why) => {
                error!("Error parsing {}: {}", file.display(), why);
                continue;
            }
            Ok(json) => json,
        };

        fidl_json.resolve_method_payloads();
        if should_process_fidl_json(&fidl_json) {
            package_data.insert(fidl_json);
        }
    }

    // Sort declarations inside each package
    package_data.fidl_json_map.par_iter_mut().for_each(|(_, package_fidl_json)| {
        package_fidl_json.sort_declarations();
    });

    package_data
}

fn create_toc(fidl_json_map: &HashMap<String, FidlJson>) -> TableOfContents {
    // The table of contents lists all packages in alphabetical order.
    let mut table_of_contents_items: Vec<_> = fidl_json_map
        .par_iter()
        .map(|(package_name, fidl_json)| TableOfContentsItem {
            name: package_name.clone(),
            link: format!("{name}/index", name = package_name),
            description: get_library_description(&fidl_json.maybe_attributes),
            added: get_library_added(&fidl_json.maybe_attributes),
        })
        .collect();
    table_of_contents_items.sort_unstable_by(|a, b| a.name.cmp(&b.name));

    // Add all versions as <integer value, string representation> to a
    // BTreeMap so that they are automatically sorted by key.
    // A string representation such as "HEAD" gets a 0 value.
    let mut version_map: BTreeMap<u64, String> = BTreeMap::new();
    for item in table_of_contents_items.iter() {
        if !item.added.is_empty() {
            version_map.insert(item.added.parse::<u64>().unwrap_or(0), item.added.clone());
        }
    }

    // Because the BTreeMap is ordered, the max version is the last item in it.
    let head = HEAD_VERSION.to_string();
    let (_, max_version) = version_map.iter().next_back().unwrap_or((&0, &head));

    TableOfContents {
        items: table_of_contents_items,
        versions: version_map.values().cloned().collect(),
        default_version: max_version.to_string(),
    }
}

fn get_library_description(maybe_attributes: &Vec<Value>) -> String {
    for attribute in maybe_attributes {
        if to_lower_snake_case(attribute["name"].as_str().unwrap_or("")) == ATTR_NAME_DOC {
            return get_attribute_standalone_arg_value(attribute)
                .expect("Unable to retrieve string value for library description")
                .to_string();
        }
    }
    "".to_string()
}

// The toc.hbs template includes hardcoded values for library versions
// Need to create a function that collects all library versions, sorts them,
// and displays them in the template.
fn get_library_added(maybe_attributes: &Vec<Value>) -> String {
    for attribute in maybe_attributes {
        if to_lower_snake_case(attribute["name"].as_str().unwrap_or("")) == ATTR_NAME_AVAILABLE {
            if let Some(arguments) = attribute["arguments"].as_array() {
                for argument in arguments {
                    if to_lower_snake_case(argument["name"].as_str().unwrap_or(""))
                        == ATTR_NAME_ADDED
                    {
                        if let Some(val) = argument["value"].as_object() {
                            let mut vers = val["value"].as_str().unwrap_or("").to_string();
                            if vers == HEAD_VERSION_NUMBER.to_string() {
                                vers = HEAD_VERSION.to_string();
                            }
                            return vers;
                        }
                    }
                }
            }
        }
    }
    "".to_string()
}

fn create_output_dir(merge_into: bool, path: &PathBuf) -> Result<(), Error> {
    if path.exists() {
        if merge_into {
            info!(
                "Directory {} already exists and we're merging into it, skipping creation",
                path.display()
            );
            return Ok(());
        }

        info!("Directory {} already exists", path.display());
        // Clear out the output folder
        fs::remove_dir_all(path)
            .with_context(|| format!("Unable to remove output directory {}", path.display()))?;
        info!("Removed directory {}", path.display());
    }

    // Re-create output folder
    fs::create_dir_all(path)
        .with_context(|| format!("Unable to create output directory {}", path.display()))?;
    info!("Created directory {}", path.display());

    Ok(())
}

// Pre-processes the list of input files by removing duplicates.
fn normalize_input_files(input: &mut Vec<PathBuf>) {
    input.sort_unstable();
    input.dedup();
}

#[cfg(test)]
mod test {
    use super::*;
    use std::fs::File;
    use std::io::Write;
    use tempfile::{tempdir, NamedTempFile};

    use std::path::PathBuf;

    use serde_json::Map;

    #[test]
    fn select_template_test() {
        let path = PathBuf::new();
        let to_template = |template_type| select_template(&template_type, &path).unwrap();

        let all_template_types = vec![TemplateType::Markdown, TemplateType::Syscall];
        for template_type in all_template_types {
            match template_type {
                TemplateType::Markdown => {
                    assert_eq!(to_template(template_type).name(), "Markdown".to_string());
                }
                TemplateType::Syscall => {
                    assert_eq!(to_template(template_type).name(), "Syscall".to_string());
                }
            }
        }
    }

    #[test]
    fn create_toc_test() {
        let mut fidl_json_map: HashMap<String, FidlJson> = HashMap::new();
        fidl_json_map.insert(
            "fuchsia.media".to_string(),
            FidlJson {
                name: "fuchsia.media".to_string(),
                maybe_attributes: vec![json!({"name": ATTR_NAME_AVAILABLE, "arguments": [
                    {
                    "name": "added",
                    "type": "uint64",
                    "value": {
                        "kind": "literal",
                        "value": "7",
                        "expression": "7",
                        "literal": {
                        "kind": "numeric",
                        "value": "7",
                        "expression": "7"
                        }
                    },
                    }
                ]})],
                library_dependencies: Vec::new(),
                bits_declarations: Vec::new(),
                const_declarations: Vec::new(),
                enum_declarations: Vec::new(),
                protocol_declarations: Vec::new(),
                table_declarations: Vec::new(),
                alias_declarations: Vec::new(),
                struct_declarations: Vec::new(),
                external_struct_declarations: Vec::new(),
                union_declarations: Vec::new(),
                declaration_order: Vec::new(),
                declarations: Map::new(),
            },
        );
        fidl_json_map.insert(
            "fuchsia.auth".to_string(),
            FidlJson {
                name: "fuchsia.auth".to_string(),
                // Note that this ATTR_NAME_DOC is UpperCamelCased - this should still
                // pass.
                maybe_attributes: vec![json!({"name": ATTR_NAME_DOC, "arguments": [
                  {
                    "name": "value",
                    "value": {
                      "expression": "Fuchsia Auth API",
                      "kind": "literal",
                      "literal": {
                        "expression":"Fuchsia Auth API",
                        "kind": "string",
                        "value": "Fuchsia Auth API"
                      },
                      "value": "Fuchsia Auth API"
                    }
                  },
                ]})],
                library_dependencies: Vec::new(),
                bits_declarations: Vec::new(),
                const_declarations: Vec::new(),
                enum_declarations: Vec::new(),
                protocol_declarations: Vec::new(),
                table_declarations: Vec::new(),
                alias_declarations: Vec::new(),
                struct_declarations: Vec::new(),
                external_struct_declarations: Vec::new(),
                union_declarations: Vec::new(),
                declaration_order: Vec::new(),
                declarations: Map::new(),
            },
        );
        fidl_json_map.insert(
            "fuchsia.camera.common".to_string(),
            FidlJson {
                name: "fuchsia.camera.common".to_string(),
                maybe_attributes: vec![json!({
                    "some_key": "key", "some_value": "not_description",
                    "name": ATTR_NAME_AVAILABLE, "arguments": [
                    {
                    "name": "added",
                    "type": "uint64",
                    "value": {
                        "kind": "literal",
                        "value": HEAD_VERSION_NUMBER.to_string(),
                        "expression": HEAD_VERSION_NUMBER.to_string(),
                        "literal": {
                        "kind": "numeric",
                        "value": HEAD_VERSION_NUMBER.to_string(),
                        "expression": HEAD_VERSION_NUMBER.to_string()
                        }
                    },
                    }
                ]
                })],
                library_dependencies: Vec::new(),
                bits_declarations: Vec::new(),
                const_declarations: Vec::new(),
                enum_declarations: Vec::new(),
                protocol_declarations: Vec::new(),
                table_declarations: Vec::new(),
                alias_declarations: Vec::new(),
                struct_declarations: Vec::new(),
                external_struct_declarations: Vec::new(),
                union_declarations: Vec::new(),
                declaration_order: Vec::new(),
                declarations: Map::new(),
            },
        );

        let toc = create_toc(&fidl_json_map);
        assert_eq!(toc.items.len(), 3);
        assert_eq!(toc.versions, vec!["HEAD", "7"]);
        assert_eq!(toc.default_version, "7");

        let item0 = toc.items.get(0).unwrap();
        assert_eq!(item0.name, "fuchsia.auth".to_string());
        assert_eq!(item0.link, "fuchsia.auth/index".to_string());
        assert_eq!(item0.description, "Fuchsia Auth API".to_string());

        let item1 = toc.items.get(1).unwrap();
        assert_eq!(item1.name, "fuchsia.camera.common".to_string());
        assert_eq!(item1.link, "fuchsia.camera.common/index".to_string());
        assert_eq!(item1.description, "".to_string());

        let item2 = toc.items.get(2).unwrap();
        assert_eq!(item2.name, "fuchsia.media".to_string());
        assert_eq!(item2.link, "fuchsia.media/index".to_string());
        assert_eq!(item2.description, "".to_string());
    }

    #[test]
    fn get_library_description_test() {
        let maybe_attributes = vec![
            json!({"name": "not doc", "value": "Not the description"}),
            json!({"name": ATTR_NAME_DOC, "arguments": [
              {
                "name": "value",
                "value": {
                  "expression": "Fuchsia Auth API",
                  "kind": "literal",
                  "literal": {
                    "expression":"Fuchsia Auth API",
                    "kind": "string",
                    "value": "Fuchsia Auth API"
                  },
                  "value": "Fuchsia Auth API"
                }
              },
            ]}),
        ];
        let description = get_library_description(&maybe_attributes);
        assert_eq!(description, "Fuchsia Auth API".to_string());
    }

    #[test]
    fn create_output_dir_test() {
        // Create a temp dir to run tests on
        let dir = tempdir().expect("Unable to create temp dir");
        let dir_path = PathBuf::from(dir.path());

        // Add a temp file inside the temp dir
        let file_path = dir_path.join("temp.txt");
        File::create(file_path).expect("Unable to create temp file");

        create_output_dir(false, &dir_path).expect("create_output_dir failed");
        assert!(dir_path.exists());
        assert!(dir_path.is_dir());

        // The temp file has been deleted
        assert_eq!(dir_path.read_dir().unwrap().count(), 0);
    }

    #[test]
    fn get_fidldoc_config_default_path_test() {
        // Ensure that I get a valid filepath
        let default = std::env::current_exe().unwrap().parent().unwrap().join(FIDLDOC_CONFIG_PATH);
        assert_eq!(default, get_fidldoc_config_default_path().unwrap());
    }

    #[test]
    fn read_fidldoc_config_test() {
        // Generate a test config file
        let fidl_config_sample = json!({
            "title": "Fuchsia FIDLs"
        });
        // Write this to a temporary file
        let mut fidl_config_file = NamedTempFile::new().unwrap();
        fidl_config_file
            .write_all(fidl_config_sample.to_string().as_bytes())
            .expect("Unable to write to temporary file");

        // Read in file
        let fidl_config = read_fidldoc_config(&fidl_config_file.path()).unwrap();
        assert_eq!(fidl_config["title"], "Fuchsia FIDLs".to_string());
    }

    #[test]
    fn normalize_input_files_test() {
        let mut input_files = vec![
            PathBuf::from(r"/tmp/file1"),
            PathBuf::from(r"/file2"),
            PathBuf::from(r"/usr/file1"),
        ];
        normalize_input_files(&mut input_files);
        assert_eq!(input_files.len(), 3);

        let mut dup_input_files = vec![
            PathBuf::from(r"/tmp/file1"),
            PathBuf::from(r"/file2"),
            PathBuf::from(r"/tmp/file1"),
        ];
        normalize_input_files(&mut dup_input_files);
        assert_eq!(dup_input_files.len(), 2);
    }

    #[test]
    fn should_process_test() {
        let fidl_json = FidlJson {
            name: "fuchsia.camera.common".to_string(),
            maybe_attributes: vec![json!({"name": "not no_doc", "value": ""})],
            library_dependencies: Vec::new(),
            bits_declarations: Vec::new(),
            const_declarations: Vec::new(),
            enum_declarations: Vec::new(),
            protocol_declarations: Vec::new(),
            table_declarations: Vec::new(),
            alias_declarations: Vec::new(),
            struct_declarations: Vec::new(),
            external_struct_declarations: Vec::new(),
            union_declarations: Vec::new(),
            declaration_order: Vec::new(),
            declarations: Map::new(),
        };
        assert_eq!(should_process_fidl_json(&fidl_json), true);
    }

    #[test]
    fn check_nodoc_attribute_test() {
        let fidl_json = FidlJson {
            name: "fuchsia.camera.common".to_string(),
            maybe_attributes: vec![json!({"name": ATTR_NAME_NO_DOC, "value": ""})],
            library_dependencies: Vec::new(),
            bits_declarations: Vec::new(),
            const_declarations: Vec::new(),
            enum_declarations: Vec::new(),
            protocol_declarations: Vec::new(),
            table_declarations: Vec::new(),
            alias_declarations: Vec::new(),
            struct_declarations: Vec::new(),
            external_struct_declarations: Vec::new(),
            union_declarations: Vec::new(),
            declaration_order: Vec::new(),
            declarations: Map::new(),
        };
        assert_eq!(should_process_fidl_json(&fidl_json), false);
    }
}
