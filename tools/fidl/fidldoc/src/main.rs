// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::{format_err, Error, ResultExt};
use log::{error, info, LevelFilter};
use std::collections::{HashMap, HashSet};
use std::fs;
use std::path::PathBuf;
use std::process;

use clap::arg_enum;

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
    #[structopt(short = "c", long = "config", default_value = "./fidldoc.config.json")]
    config: String,
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
    let input_files = &opt.input;

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
    let fidl_config = read_fidldoc_config(&opt.config)
        .with_context(|e| format!("Error parsing {}: {}", &opt.config, e))?;

    create_output_dir(&output_path).with_context(|e| {
        format!("Unable to create output directory {}: {}", output_path.display(), e)
    })?;

    // Parse input files to get declarations, package set and fidl json map
    let FidlJsonPackageData { declarations, package_set, fidl_json_map } =
        process_fidl_json_files(input_files.to_vec())?;

    // The table of contents lists all packages in alphabetical order.
    let table_of_contents = create_toc(package_set);

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

    for (package, package_fidl_json) in fidl_json_map {
        // Modifications to the fidldoc object
        let fidl_doc = json!({
            "version": package_fidl_json.version,
            "name": package_fidl_json.name,
            "library_dependencies": package_fidl_json.library_dependencies,
            "bits_declarations": package_fidl_json.bits_declarations,
            "const_declarations": package_fidl_json.const_declarations,
            "enum_declarations": package_fidl_json.enum_declarations,
            "interface_declarations": package_fidl_json.interface_declarations,
            "table_declarations": package_fidl_json.table_declarations,
            "struct_declarations": package_fidl_json.struct_declarations,
            "union_declarations": package_fidl_json.union_declarations,
            "xunion_declarations": package_fidl_json.xunion_declarations,
            "declaration_order": package_fidl_json.declaration_order,
            "declarations": package_fidl_json.declarations,
            "table_of_contents": table_of_contents,
            "fidldoc_version": FIDLDOC_VERSION,
            "config": fidl_config,
            "tag": &opt.tag,
            "search": declarations,
            "url_path": url_path,
        });

        match template.render_interface(&package, &fidl_doc) {
            Err(why) => error!("Unable to render interface {}: {:?}", &package, why),
            Ok(()) => info!("Generated interface documentation for {}", &package),
        }
    }

    println!("Generated documentation at {}", output_path.display());
    Ok(())
}

fn select_template(
    template_type: &TemplateType,
    output_path: &PathBuf,
) -> Result<Box<FidldocTemplate>, Error> {
    // Instantiate the template selected by the user
    let template: Box<FidldocTemplate> = match template_type {
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

fn read_fidldoc_config(config_path_str: &str) -> Result<Value, Error> {
    let fidl_config_str = fs::read_to_string(config_path_str)
        .with_context(|e| format!("Couldn't open file {}: {}", config_path_str, e))?;
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
            package_fidl_json.bits_declarations.append(&mut fidl_json.bits_declarations);
            package_fidl_json.const_declarations.append(&mut fidl_json.const_declarations);
            package_fidl_json.enum_declarations.append(&mut fidl_json.enum_declarations);
            package_fidl_json.interface_declarations.append(&mut fidl_json.interface_declarations);
            package_fidl_json.struct_declarations.append(&mut fidl_json.struct_declarations);
            package_fidl_json.table_declarations.append(&mut fidl_json.table_declarations);
            package_fidl_json.union_declarations.append(&mut fidl_json.union_declarations);
            package_fidl_json.xunion_declarations.append(&mut fidl_json.xunion_declarations);
            package_fidl_json.declaration_order.append(&mut fidl_json.declaration_order);
            fidl_json_map.insert(package_name, package_fidl_json);
        }
    }

    Ok(FidlJsonPackageData { declarations, package_set, fidl_json_map })
}

fn create_toc(package_set: HashSet<String>) -> Vec<TableOfContentsItem> {
    // The table of contents lists all packages in alphabetical order.
    let mut table_of_contents: Vec<_> = package_set
        .iter()
        .map(|package_name| TableOfContentsItem {
            name: package_name.clone(),
            link: format!("{name}/index", name = package_name),
        })
        .collect();
    table_of_contents.sort_unstable_by(|a, b| a.name.cmp(&b.name));
    table_of_contents
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

#[cfg(test)]
mod test {
    use super::*;
    use std::fs::File;
    use std::io::Write;
    use tempfile::{tempdir, NamedTempFile};

    use std::collections::HashSet;
    use std::path::PathBuf;

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
        let mut package_set: HashSet<String> = HashSet::new();
        package_set.insert("fuchsia.media".to_string());
        package_set.insert("fuchsia.auth".to_string());
        package_set.insert("fuchsia.camera.common".to_string());

        let toc = create_toc(package_set);
        assert_eq!(toc.len(), 3);

        let item0 = toc.get(0).unwrap();
        assert_eq!(item0.name, "fuchsia.auth".to_string());
        assert_eq!(item0.link, "fuchsia.auth/index".to_string());

        let item1 = toc.get(1).unwrap();
        assert_eq!(item1.name, "fuchsia.camera.common".to_string());
        assert_eq!(item1.link, "fuchsia.camera.common/index".to_string());

        let item2 = toc.get(2).unwrap();
        assert_eq!(item2.name, "fuchsia.media".to_string());
        assert_eq!(item2.link, "fuchsia.media/index".to_string());
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
        let fidl_config = read_fidldoc_config(&fidl_config_file.path().to_str().unwrap()).unwrap();
        assert_eq!(fidl_config["title"], "Fuchsia FIDLs".to_string());
    }
}
