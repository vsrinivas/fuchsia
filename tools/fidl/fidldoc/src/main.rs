// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO Follow 2018 idioms
#![allow(elided_lifetimes_in_paths)]

use failure::{format_err, Error, ResultExt};
use log::{error, info, LevelFilter};
use std::collections::{HashMap, HashSet};
use std::fs;
use std::path::{Path, PathBuf};
use std::process;

use clap::arg_enum;

use rayon::prelude::*;

use serde_json::{json, Value};

use structopt::StructOpt;

mod fidljson;
use fidljson::{FidlJson, FidlJsonPackageData, TableOfContentsItem};

mod simple_logger;
use simple_logger::SimpleLogger;

mod templates;
use templates::html::HtmlTemplate;
use templates::markdown::MarkdownTemplate;
use templates::FidldocTemplate;

static FIDLDOC_VERSION: &str = "0.0.4";
static SUPPORTED_FIDLJSON: &str = "0.0.1";
static FIDLDOC_CONFIG_PATH: &str = "fidldoc.config.json";

arg_enum! {
    #[derive(Debug)]
    enum TemplateType {
        HTML,
        Markdown
    }
}

#[derive(Debug, StructOpt)]
#[structopt(name = "fidldoc", about = "FIDL documentation generator", version = "0.1")]
struct Opt {
    /// Path to a configuration file to provide additional options
    #[structopt(short = "c", long = "config")]
    config: Option<PathBuf>,
    /// Current commit hash, useful to coordinate doc generation with a specific source code revision
    #[structopt(long = "tag", default_value = "master")]
    tag: String,
    //// Set the input file(s) to use
    #[structopt(parse(from_os_str), raw(required = "true"))]
    input: Vec<PathBuf>,
    /// Set the output folder
    #[structopt(short = "o", long = "out", default_value = "/tmp/fidldoc/")]
    output: String,
    /// Set the base URL path for the generated docs
    #[structopt(short = "p", long = "path", default_value = "/")]
    path: String,
    /// Select the template to use to render the docs
    #[structopt(
        short = "t",
        long = "template",
        default_value = "markdown",
        raw(possible_values = "&TemplateType::variants()", case_insensitive = "true")
    )]
    template: TemplateType,
    /// Generate verbose output
    #[structopt(short = "v", long = "verbose", parse(from_occurrences))]
    verbose: u64,
}

static LOGGER: SimpleLogger = SimpleLogger;

fn main() {
    log::set_logger(&LOGGER).unwrap();
    let opt = Opt::from_args();
    if let Err(e) = run(opt) {
        error!("Error: {}", e);
        process::exit(1);
    }
}

fn run(opt: Opt) -> Result<(), Error> {
    let mut input_files = opt.input;
    normalize_input_files(&mut input_files);

    let output = &opt.output;
    let output_path = PathBuf::from(output);

    let url_path = &opt.path;

    let template_type = &opt.template;
    let template = select_template(template_type, &output_path)
        .with_context(|e| format!("Unable to instantiate template {}: {}", template_type, e))?;

    match opt.verbose {
        0 => log::set_max_level(LevelFilter::Error),
        1 => log::set_max_level(LevelFilter::Info),
        _ => log::set_max_level(LevelFilter::Debug),
    }

    // Read in fidldoc.config.json
    let fidl_config_file = match opt.config {
        Some(filepath) => filepath,
        None => get_fidldoc_config_default_path()
            .with_context(|e| format!("Unable to retrieve default config file location: {}", e))?,
    };
    info!("Using config file from {}", fidl_config_file.display());
    let fidl_config = read_fidldoc_config(&fidl_config_file)
        .with_context(|e| format!("Error parsing {}: {}", &fidl_config_file.display(), e))?;

    create_output_dir(&output_path).with_context(|e| {
        format!("Unable to create output directory {}: {}", output_path.display(), e)
    })?;

    // Parse input files to get declarations, package set and fidl json map
    let FidlJsonPackageData { declarations, fidl_json_map } =
        process_fidl_json_files(input_files.to_vec())?;

    // The table of contents lists all packages in alphabetical order.
    let table_of_contents = create_toc(&fidl_json_map);

    // Modifications to the fidldoc object
    let main_fidl_doc = json!({
        "table_of_contents": table_of_contents,
        "fidldoc_version": FIDLDOC_VERSION,
        "config": fidl_config,
        "search": declarations,
        "url_path": url_path,
    });

    // Create main page
    template.render_main_page(&main_fidl_doc).expect("Unable to render main page");

    let tag = &opt.tag;
    let output_path_string = &output_path.display();
    fidl_json_map
        .par_iter()
        .try_for_each(|(package, package_fidl_json)| {
            render_fidl_interface(
                package,
                package_fidl_json,
                &table_of_contents,
                &fidl_config,
                &tag,
                &declarations,
                &url_path,
                &template_type,
                &output_path,
            )
        })
        .expect("Unable to write FIDL reference files");

    println!("Generated documentation at {}", &output_path_string);
    Ok(())
}

fn render_fidl_interface(
    package: &String,
    package_fidl_json: &FidlJson,
    table_of_contents: &Vec<TableOfContentsItem>,
    fidl_config: &Value,
    tag: &String,
    declarations: &Vec<String>,
    url_path: &String,
    template_type: &TemplateType,
    output_path: &PathBuf,
) -> Result<(), Error> {
    // Modifications to the fidldoc object
    let fidl_doc = json!({
        "version": package_fidl_json.version,
        "name": package_fidl_json.name,
        "maybe_attributes": package_fidl_json.maybe_attributes,
        "library_dependencies": package_fidl_json.library_dependencies,
        "bits_declarations": package_fidl_json.bits_declarations,
        "const_declarations": package_fidl_json.const_declarations,
        "enum_declarations": package_fidl_json.enum_declarations,
        "interface_declarations": package_fidl_json.interface_declarations,
        "table_declarations": package_fidl_json.table_declarations,
        "struct_declarations": package_fidl_json.struct_declarations,
        "type_alias_declarations": package_fidl_json.type_alias_declarations,
        "union_declarations": package_fidl_json.union_declarations,
        "xunion_declarations": package_fidl_json.xunion_declarations,
        "declaration_order": package_fidl_json.declaration_order,
        "declarations": package_fidl_json.declarations,
        "table_of_contents": table_of_contents,
        "fidldoc_version": FIDLDOC_VERSION,
        "config": fidl_config,
        "tag": tag,
        "search": declarations,
        "url_path": url_path,
    });

    let template = select_template(&template_type, &output_path)
        .with_context(|e| format!("Unable to instantiate template {}: {}", template_type, e));
    match template?.render_interface(&package, &fidl_doc) {
        Err(why) => error!("Unable to render interface {}: {:?}", &package, why),
        Ok(()) => info!("Generated interface documentation for {}", &package),
    }

    Ok(())
}

fn select_template(
    template_type: &TemplateType,
    output_path: &PathBuf,
) -> Result<Box<dyn FidldocTemplate>, Error> {
    // Instantiate the template selected by the user
    let template: Box<dyn FidldocTemplate> = match template_type {
        TemplateType::HTML => {
            let template = HtmlTemplate::new(&output_path)?;
            Box::new(template)
        }
        TemplateType::Markdown => {
            let template = MarkdownTemplate::new(&output_path);
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
        .with_context(|e| format!("Couldn't open file {}: {}", config_path.display(), e))?;
    Ok(serde_json::from_str(&fidl_config_str)?)
}

fn process_fidl_json_files(input_files: Vec<PathBuf>) -> Result<FidlJsonPackageData, Error> {
    // Get table of contents as a HashSet of packages
    let mut package_set: HashSet<String> = HashSet::new();
    // Store all of the FidlJson values as we pass through.
    // There should be one HashMap entry for each package.
    // Multiple files will be merged together.
    let mut fidl_json_map: HashMap<String, FidlJson> = HashMap::new();
    // Store every `declaration_order` item to populate search
    let mut declarations: Vec<String> = Vec::new();

    for file in input_files {
        let fidl_file_path = PathBuf::from(&file);
        let mut fidl_json = match FidlJson::from_path(&fidl_file_path) {
            Err(why) => {
                error!("Error parsing {}: {}", file.display(), why);
                continue;
            }
            Ok(json) => json,
        };

        // Check version
        if fidl_json.version != SUPPORTED_FIDLJSON {
            error!(
                "Error parsing {}: fidldoc does not support version {}, only {}",
                file.display(),
                fidl_json.version,
                SUPPORTED_FIDLJSON
            );
            continue;
        }

        let package_name = fidl_json.name.clone();
        declarations.append(&mut fidl_json.declaration_order);
        if !package_set.contains(&package_name) {
            package_set.insert(package_name.clone());
            fidl_json_map.insert(package_name, fidl_json);
        } else {
            // Merge
            let mut package_fidl_json: FidlJson = fidl_json_map
                .get(&package_name)
                .cloned()
                .ok_or(format_err!("Package {} not found in FidlJson map", package_name))?;
            package_fidl_json.maybe_attributes.append(&mut fidl_json.maybe_attributes);
            package_fidl_json.bits_declarations.append(&mut fidl_json.bits_declarations);
            package_fidl_json.const_declarations.append(&mut fidl_json.const_declarations);
            package_fidl_json.enum_declarations.append(&mut fidl_json.enum_declarations);
            package_fidl_json.interface_declarations.append(&mut fidl_json.interface_declarations);
            package_fidl_json.struct_declarations.append(&mut fidl_json.struct_declarations);
            package_fidl_json.table_declarations.append(&mut fidl_json.table_declarations);
            package_fidl_json
                .type_alias_declarations
                .append(&mut fidl_json.type_alias_declarations);
            package_fidl_json.union_declarations.append(&mut fidl_json.union_declarations);
            package_fidl_json.xunion_declarations.append(&mut fidl_json.xunion_declarations);
            package_fidl_json.declaration_order.append(&mut fidl_json.declaration_order);
            fidl_json_map.insert(package_name, package_fidl_json);
        }
    }

    Ok(FidlJsonPackageData { declarations, fidl_json_map })
}

fn create_toc(fidl_json_map: &HashMap<String, FidlJson>) -> Vec<TableOfContentsItem> {
    // The table of contents lists all packages in alphabetical order.
    let mut table_of_contents: Vec<_> = fidl_json_map
        .par_iter()
        .map(|(package_name, fidl_json)| TableOfContentsItem {
            name: package_name.clone(),
            link: format!("{name}/index", name = package_name),
            description: get_library_description(&fidl_json.maybe_attributes),
        })
        .collect();
    table_of_contents.sort_unstable_by(|a, b| a.name.cmp(&b.name));
    table_of_contents
}

fn get_library_description(maybe_attributes: &Vec<Value>) -> String {
    for attribute in maybe_attributes {
        if attribute["name"] == "Doc" {
            return attribute["value"]
                .as_str()
                .expect("Unable to retrieve string value for library description")
                .to_string();
        }
    }
    "".to_string()
}

fn create_output_dir(path: &PathBuf) -> Result<(), Error> {
    if path.exists() {
        info!("Directory {} already exists", path.display());
        // Clear out the output folder
        fs::remove_dir_all(path).with_context(|e| {
            format!("Unable to remove output directory {}: {}", path.display(), e)
        })?;
        info!("Removed directory {}", path.display());
    }

    // Re-create output folder
    fs::create_dir_all(path)
        .with_context(|e| format!("Unable to create output directory {}: {}", path.display(), e))?;
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

        let html_template = select_template(&TemplateType::HTML, &path).unwrap();
        assert_eq!(html_template.name(), "HTML".to_string());

        let markdown_template = select_template(&TemplateType::Markdown, &path).unwrap();
        assert_eq!(markdown_template.name(), "Markdown".to_string());
    }

    #[test]
    fn create_toc_test() {
        let mut fidl_json_map: HashMap<String, FidlJson> = HashMap::new();
        fidl_json_map.insert(
            "fuchsia.media".to_string(),
            FidlJson {
                name: "fuchsia.media".to_string(),
                version: "0.0.1".to_string(),
                maybe_attributes: Vec::new(),
                library_dependencies: Vec::new(),
                bits_declarations: Vec::new(),
                const_declarations: Vec::new(),
                enum_declarations: Vec::new(),
                interface_declarations: Vec::new(),
                table_declarations: Vec::new(),
                type_alias_declarations: Vec::new(),
                struct_declarations: Vec::new(),
                union_declarations: Vec::new(),
                xunion_declarations: Vec::new(),
                declaration_order: Vec::new(),
                declarations: Map::new(),
            },
        );
        fidl_json_map.insert(
            "fuchsia.auth".to_string(),
            FidlJson {
                name: "fuchsia.auth".to_string(),
                version: "0.0.1".to_string(),
                maybe_attributes: vec![json!({"name": "Doc", "value": "Fuchsia Auth API"})],
                library_dependencies: Vec::new(),
                bits_declarations: Vec::new(),
                const_declarations: Vec::new(),
                enum_declarations: Vec::new(),
                interface_declarations: Vec::new(),
                table_declarations: Vec::new(),
                type_alias_declarations: Vec::new(),
                struct_declarations: Vec::new(),
                union_declarations: Vec::new(),
                xunion_declarations: Vec::new(),
                declaration_order: Vec::new(),
                declarations: Map::new(),
            },
        );
        fidl_json_map.insert(
            "fuchsia.camera.common".to_string(),
            FidlJson {
                name: "fuchsia.camera.common".to_string(),
                version: "0.0.1".to_string(),
                maybe_attributes: vec![json!({"some_key": "key", "some_value": "not_description"})],
                library_dependencies: Vec::new(),
                bits_declarations: Vec::new(),
                const_declarations: Vec::new(),
                enum_declarations: Vec::new(),
                interface_declarations: Vec::new(),
                table_declarations: Vec::new(),
                type_alias_declarations: Vec::new(),
                struct_declarations: Vec::new(),
                union_declarations: Vec::new(),
                xunion_declarations: Vec::new(),
                declaration_order: Vec::new(),
                declarations: Map::new(),
            },
        );

        let toc = create_toc(&fidl_json_map);
        assert_eq!(toc.len(), 3);

        let item0 = toc.get(0).unwrap();
        assert_eq!(item0.name, "fuchsia.auth".to_string());
        assert_eq!(item0.link, "fuchsia.auth/index".to_string());
        assert_eq!(item0.description, "Fuchsia Auth API".to_string());

        let item1 = toc.get(1).unwrap();
        assert_eq!(item1.name, "fuchsia.camera.common".to_string());
        assert_eq!(item1.link, "fuchsia.camera.common/index".to_string());
        assert_eq!(item1.description, "".to_string());

        let item2 = toc.get(2).unwrap();
        assert_eq!(item2.name, "fuchsia.media".to_string());
        assert_eq!(item2.link, "fuchsia.media/index".to_string());
        assert_eq!(item2.description, "".to_string());
    }

    #[test]
    fn get_library_description_test() {
        let maybe_attributes = vec![
            json!({"name": "Not Doc", "value": "Not the description"}),
            json!({"name": "Doc", "value": "Fuchsia Auth API"}),
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

        create_output_dir(&dir_path).expect("create_output_dir failed");
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
            .write(fidl_config_sample.to_string().as_bytes())
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
}
