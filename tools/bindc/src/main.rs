// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A Fuchsia Driver Bind Rules compiler

mod cpp_generator;
mod generate;
mod rust_generator;

use anyhow::{anyhow, Context, Error};
use bind::bytecode_encoder::encode_v1::encode_to_string_v1;
use bind::bytecode_encoder::encode_v2::encode_to_string_v2;
use bind::compiler::{
    self, BindRules, CompiledBindRules, CompositeBindRules, CompositeNode, SymbolicInstruction,
    SymbolicInstructionInfo,
};
use bind::debugger::offline_debugger;
use bind::test;
use fidl_ir_lib::fidl::*;
use std::fmt::Write;
use std::fs::File;
use std::io::prelude::*;
use std::io::{self, BufRead, Write as IoWrite};
use std::path::PathBuf;
use structopt::StructOpt;

#[derive(StructOpt, Debug)]
struct SharedOptions {
    /// The bind library input files. These may be included by the bind rules. They should be in
    /// the format described in //tools/bindc/README.md.
    #[structopt(short = "i", long = "include", parse(from_os_str))]
    include: Vec<PathBuf>,

    /// Specifiy the bind library input files as a file. The file must contain a list of filenames
    /// that are bind library input files that may be included by the bind rules. Those files
    /// should be in the format described in //tools/bindc/README.md.
    #[structopt(short = "f", long = "include-file", parse(from_os_str))]
    include_file: Option<PathBuf>,

    /// The bind rules input file. This should be in the format described in
    /// //tools/bindc/README.md. This is required unless disable_autobind is true, in which case
    /// the driver while bind unconditionally (but only on the user's request.)
    #[structopt(parse(from_os_str))]
    input: Option<PathBuf>,

    /// Check inputs for style guide violations.
    #[structopt(short = "l", long = "lint")]
    lint: bool,
}

#[derive(StructOpt, Debug)]
enum Command {
    #[structopt(name = "compile")]
    Compile {
        #[structopt(flatten)]
        options: SharedOptions,

        /// Output file. The compiler emits a C header file.
        #[structopt(short = "o", long = "output", parse(from_os_str))]
        output: Option<PathBuf>,

        /// Specify a path for the compiler to generate a depfile. A depfile contain, in Makefile
        /// format, the files that this invocation of the compiler depends on including all bind
        /// libraries and the bind rules input itself. An output file must be provided to generate
        /// a depfile.
        #[structopt(short = "d", long = "depfile", parse(from_os_str))]
        depfile: Option<PathBuf>,

        // TODO(fxbug.dev/43400): Eventually this option should be removed when we can define this
        // configuration in the driver's component manifest.
        /// Disable automatically binding the driver so that the driver must be bound on a user's
        /// request.
        #[structopt(short = "a", long = "disable-autobind")]
        disable_autobind: bool,

        /// Output a bytecode file, instead of a C header file.
        #[structopt(short = "b", long = "output-bytecode")]
        output_bytecode: bool,

        /// Encode the bytecode in the new format if true. Otherwise, encode to the old format.
        #[structopt(short = "n", long = "use-new-bytecode")]
        use_new_bytecode: bool,

        /// Disable generating device_fragment_t data structures in the C++ header.
        #[structopt(long = "disable-fragment-gen")]
        disable_fragment_gen: bool,
    },
    #[structopt(name = "debug")]
    Debug {
        #[structopt(flatten)]
        options: SharedOptions,

        /// A file containing the properties of a specific device, as a list of key-value pairs.
        /// This will be used as the input to the bind rules debugger.
        #[structopt(short = "d", long = "debug", parse(from_os_str))]
        device_file: PathBuf,
    },
    #[structopt(name = "test")]
    Test {
        #[structopt(flatten)]
        options: SharedOptions,

        // TODO(fxbug.dev/56774): Refer to documentation for bind testing.
        /// A file containing the test specification.
        #[structopt(short = "t", long = "test-spec", parse(from_os_str))]
        test_spec: PathBuf,
    },
    /// Generate a Bind Library based on the input FIDL IR file.
    #[structopt(name = "generate-bind")]
    GenerateBind {
        /// The FIDL IR input file. This should be generated from a FIDL library
        /// by the FIDL compiler at //tools/fidl/fidlc using the $fidl_toolchain suffix.
        #[structopt(parse(from_os_str))]
        input: PathBuf,

        /// Output Bind Library file.
        #[structopt(short = "o", long = "output", parse(from_os_str))]
        output: Option<PathBuf>,
    },
    /// Generate a C++ header file based on the input Bind Library file.
    #[structopt(name = "generate-cpp")]
    GenerateCpp {
        /// The Bind Library input file.
        #[structopt(parse(from_os_str))]
        input: PathBuf,

        /// Check the input for style guide violations.
        #[structopt(short = "l", long = "lint")]
        lint: bool,

        /// Output C++ header file.
        #[structopt(short = "o", long = "output", parse(from_os_str))]
        output: Option<PathBuf>,
    },
    /// Generate a Rust file based on the input Bind Library file.
    #[structopt(name = "generate-rust")]
    GenerateRust {
        /// The Bind Library input file.
        #[structopt(parse(from_os_str))]
        input: PathBuf,

        /// Check the input for style guide violations.
        #[structopt(short = "l", long = "lint")]
        lint: bool,

        /// Output Rust file.
        #[structopt(short = "o", long = "output", parse(from_os_str))]
        output: Option<PathBuf>,
    },
}

fn main() {
    let command = Command::from_iter(std::env::args());
    if let Err(err) = handle_command(command) {
        eprintln!("{}", err);
        std::process::exit(1);
    }
}

fn convert_to_var_name(node_name: &String) -> String {
    node_name.replace("-", "_")
}

fn write_depfile(
    output: &PathBuf,
    input: &Option<PathBuf>,
    includes: &[PathBuf],
) -> Result<String, Error> {
    fn path_to_str(path: &PathBuf) -> Result<&str, Error> {
        path.as_os_str().to_str().context("failed to convert path to string")
    }

    let mut deps = includes.iter().map(|s| path_to_str(s)).collect::<Result<Vec<&str>, Error>>()?;

    if let Some(input) = input {
        let input_str = path_to_str(input)?;
        deps.push(input_str);
    }

    let output_str = path_to_str(output)?;
    let mut out = String::new();
    writeln!(&mut out, "{}: {}", output_str, deps.join(" "))?;
    Ok(out)
}

fn write_bind_template<'a>(bind_rules: BindRules<'a>) -> Result<String, Error> {
    let mut output = String::new();
    if bind_rules.use_new_bytecode {
        let (binding, byte_count) = encode_to_string_v2(bind_rules)?;
        output
            .write_fmt(format_args!(
                include_str!("templates/bind_v2.h.template"),
                byte_count = byte_count,
                binding = binding,
                composite_fragment_definition = "",
            ))
            .context("Failed to format output")?;
    } else {
        output
            .write_fmt(format_args!(
                include_str!("templates/bind_v1.h.template"),
                bind_count = bind_rules.instructions.len(),
                binding = encode_to_string_v1(bind_rules.instructions)?,
            ))
            .context("Failed to format output")?;
    }
    Ok(output)
}

fn write_fragment_template<'a>(
    device_name: &String,
    node: CompositeNode<'a>,
) -> Result<String, Error> {
    let mut output = String::new();
    let mut instructions_str = encode_to_string_v1(node.instructions)?;
    instructions_str.push_str(&encode_to_string_v1(vec![SymbolicInstructionInfo {
        location: None,
        instruction: SymbolicInstruction::UnconditionalBind,
    }])?);

    output
        .write_fmt(format_args!(
            include_str!("templates/fragment.template"),
            var_name = convert_to_var_name(&node.name),
            name = node.name,
            device_name = device_name,
            instructions = instructions_str,
        ))
        .context("Failed to format output")?;
    Ok(output)
}

fn write_fragment_array_template<'a>(bind_rules: CompositeBindRules<'a>) -> Result<String, Error> {
    let mut fragment_list = format!(
        "{}_{}_fragment",
        &bind_rules.device_name,
        convert_to_var_name(&bind_rules.primary_node.name)
    );
    let mut fragment_definition =
        write_fragment_template(&bind_rules.device_name, bind_rules.primary_node)?;

    for node in bind_rules.additional_nodes {
        fragment_list.push_str(&format!(
            ", {}_{}_fragment",
            &bind_rules.device_name,
            convert_to_var_name(&node.name)
        ));
        fragment_definition
            .push_str(&format!("\n{}", write_fragment_template(&bind_rules.device_name, node)?));
    }

    let mut output = String::new();
    output
        .write_fmt(format_args!(
            include_str!("templates/fragment_array.template"),
            device_name = bind_rules.device_name,
            fragment_definition = fragment_definition,
            fragment_list = fragment_list,
        ))
        .context("Failed to format output")?;
    Ok(output)
}

fn write_composite_bind_template<'a>(
    bind_rules: CompositeBindRules<'a>,
    disable_fragment_gen: bool,
) -> Result<String, Error> {
    let fragment_definition = if disable_fragment_gen {
        "".to_string()
    } else {
        write_fragment_array_template(bind_rules)?
    };

    let mut output = String::new();
    output
        .write_fmt(format_args!(
            include_str!("templates/bind_v2.h.template"),
            byte_count = "0",
            binding = "",
            composite_fragment_definition = fragment_definition,
        ))
        .context("Failed to format output")?;
    Ok(output)
}

fn read_file(path: &PathBuf) -> Result<String, Error> {
    let mut file = File::open(path)?;
    let mut buf = String::new();
    file.read_to_string(&mut buf)?;
    Ok(buf)
}

pub enum GeneratedBindingType {
    Cpp,
    Rust,
}

pub fn handle_code_generate(
    binding_type: GeneratedBindingType,
    input: PathBuf,
    lint: bool,
    output: Option<PathBuf>,
) -> Result<(), Error> {
    let input_content = read_file(&input)?;

    // Generate the target.
    let generated_content = generate::generate(binding_type, &input_content, lint)?;

    // Create and open output file.
    let mut output_writer: Box<dyn io::Write> = if let Some(output) = output {
        Box::new(File::create(output).context("Failed to create output file.")?)
    } else {
        // Output file name was not given. Print result to stdout.
        Box::new(io::stdout())
    };

    // Write generated target to output.
    output_writer
        .write_all(generated_content.as_bytes())
        .context("Failed to write to output file")?;

    Ok(())
}

fn handle_command(command: Command) -> Result<(), Error> {
    match command {
        Command::Debug { options, device_file } => {
            let includes = handle_includes(options.include, options.include_file)?;
            let includes = includes.iter().map(read_file).collect::<Result<Vec<String>, _>>()?;
            let input = options.input.ok_or(anyhow!("The debug command requires an input."))?;
            let rules = read_file(&input)?;
            let bind_rules =
                compiler::compile_bind(&rules, &includes, options.lint, false, false, false)?;

            let device = read_file(&device_file)?;
            let binds = offline_debugger::debug_from_str(&bind_rules, &device)?;
            if binds {
                println!("Driver binds to device.");
            } else {
                println!("Driver doesn't bind to device.");
            }
            Ok(())
        }
        Command::Test { options, test_spec } => {
            let input = options.input.ok_or(anyhow!("The test command requires an input."))?;
            let rules = read_file(&input)?;
            let includes = handle_includes(options.include, options.include_file)?;
            let includes = includes.iter().map(read_file).collect::<Result<Vec<String>, _>>()?;
            let test_spec = read_file(&test_spec)?;
            if !test::run(&rules, &includes, &test_spec)? {
                return Err(anyhow!("Test failed"));
            }
            Ok(())
        }
        Command::Compile {
            options,
            output,
            depfile,
            disable_autobind,
            output_bytecode,
            use_new_bytecode,
            disable_fragment_gen,
        } => {
            let includes = handle_includes(options.include, options.include_file)?;
            handle_compile(
                options.input,
                includes,
                disable_autobind,
                output_bytecode,
                use_new_bytecode,
                disable_fragment_gen,
                options.lint,
                output,
                depfile,
            )
        }
        Command::GenerateBind { input, output } => handle_generate_bind(input, output),
        Command::GenerateCpp { input, lint, output } => {
            handle_code_generate(GeneratedBindingType::Cpp, input, lint, output)
        }
        Command::GenerateRust { input, lint, output } => {
            handle_code_generate(GeneratedBindingType::Rust, input, lint, output)
        }
    }
}

fn handle_includes(
    mut includes: Vec<PathBuf>,
    include_file: Option<PathBuf>,
) -> Result<Vec<PathBuf>, Error> {
    if let Some(include_file) = include_file {
        let file = File::open(include_file).context("Failed to open include file")?;
        let reader = io::BufReader::new(file);
        let mut filenames = reader
            .lines()
            .map(|line| line.map(PathBuf::from))
            .map(|line| line.context("Failed to read include file"))
            .collect::<Result<Vec<_>, Error>>()?;
        includes.append(&mut filenames);
    }
    Ok(includes)
}

fn handle_compile(
    input: Option<PathBuf>,
    includes: Vec<PathBuf>,
    disable_autobind: bool,
    output_bytecode: bool,
    use_new_bytecode: bool,
    disable_fragment_gen: bool,
    lint: bool,
    output: Option<PathBuf>,
    depfile: Option<PathBuf>,
) -> Result<(), Error> {
    let mut output_writer: Box<dyn io::Write> = if let Some(output) = output {
        // If there's an output filename then we can generate a depfile too.
        if let Some(filename) = depfile {
            let mut file = File::create(filename).context("Failed to open depfile")?;
            let depfile_string =
                write_depfile(&output, &input, &includes).context("Failed to create depfile")?;
            file.write(depfile_string.as_bytes()).context("Failed to write to depfile")?;
        }
        Box::new(File::create(output).context("Failed to create output file")?)
    } else {
        Box::new(io::stdout())
    };

    let rules_str;
    let compiled_bind_rules = if !disable_autobind {
        let input = input.ok_or(anyhow!("An input is required when disable_autobind is false."))?;
        rules_str = read_file(&input)?;
        let includes = includes.iter().map(read_file).collect::<Result<Vec<String>, _>>()?;
        compiler::compile(&rules_str, &includes, lint, disable_autobind, use_new_bytecode, false)?
    } else if let Some(input) = input {
        // Autobind is disabled but there are some bind rules for manual binding.
        rules_str = read_file(&input)?;
        let includes = includes.iter().map(read_file).collect::<Result<Vec<String>, _>>()?;
        let compiled_bind_rules = compiler::compile(
            &rules_str,
            &includes,
            lint,
            disable_autobind,
            use_new_bytecode,
            false,
        )?;
        compiled_bind_rules
    } else {
        CompiledBindRules::empty_bind_rules(disable_autobind, use_new_bytecode, false)
    };

    if output_bytecode {
        let bytecode = compiled_bind_rules.encode_to_bytecode()?;
        output_writer.write_all(bytecode.as_slice()).context("Failed to write to output file")?;
    } else {
        let template = match compiled_bind_rules {
            CompiledBindRules::Bind(bind_rules) => write_bind_template(bind_rules),
            CompiledBindRules::CompositeBind(bind_rules) => {
                write_composite_bind_template(bind_rules, disable_fragment_gen)
            }
        }?;
        output_writer.write_all(template.as_bytes()).context("Failed to write to output file")?;
    };

    Ok(())
}

/// Converts the name of a protocol to a bind library enum for its transport method.
fn convert_to_bind_library_enum(
    identifier: &CompoundIdentifier,
    prefix: &String,
) -> Result<String, Error> {
    let enum_name = identifier
        .0
        .strip_prefix(format!("{}/", prefix).as_str())
        .ok_or(anyhow!("Failed to strip library name from CompoundIdentifier."))?;
    let result = format!(include_str!("templates/bind_lib_enum.template"), enum_name = enum_name,);
    Ok(result)
}

fn generate_bind_library(input: &str) -> Result<String, Error> {
    let in_fidl_ir: FidlIr = serde_json::from_str(input)?;

    // Use the FIDL library name as the bind library name.
    let library_name = in_fidl_ir.get_library_name();

    let bind_lib_content = in_fidl_ir
        .declarations
        .0
        .iter()
        .filter(|entry| {
            matches!(entry.1, Declaration::Protocol) || matches!(entry.1, Declaration::Service)
        })
        .map(|entry| convert_to_bind_library_enum(entry.0, &library_name))
        .collect::<Result<Vec<String>, _>>()?
        .join("\n");

    // Output result into template.
    let mut output = String::new();
    output
        .write_fmt(format_args!(
            include_str!("templates/bind_lib.template"),
            library_name = library_name,
            bind_lib_content = bind_lib_content,
        ))
        .context("Failed to format output")?;

    Ok(output.to_string())
}

fn handle_generate_bind(input: PathBuf, output: Option<PathBuf>) -> Result<(), Error> {
    let input_content = read_file(&input)?;

    // Generate the bind library.
    let generated_content = generate_bind_library(&input_content)?;

    // Create and open output file.
    let mut output_writer: Box<dyn io::Write> = if let Some(output) = output {
        Box::new(File::create(output).context("Failed to create output file.")?)
    } else {
        // Output file name was not given. Print result to stdout.
        Box::new(io::stdout())
    };

    // Write bind library to output.
    output_writer
        .write_all(generated_content.as_bytes())
        .context("Failed to write to output file")?;

    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use assert_matches::assert_matches;
    use bind::compiler::{SymbolicInstruction, SymbolicInstructionInfo};
    use std::collections::HashMap;

    #[test]
    fn zero_instructions_v1() {
        let bind_rules = CompiledBindRules::Bind(BindRules {
            instructions: vec![],
            symbol_table: HashMap::new(),
            use_new_bytecode: false,
            enable_debug: false,
        });

        let bytecode = bind_rules.encode_to_bytecode().unwrap();
        assert!(bytecode.is_empty());

        let bind_rules = BindRules {
            instructions: vec![],
            symbol_table: HashMap::new(),
            use_new_bytecode: false,
            enable_debug: false,
        };
        let template = write_bind_template(bind_rules).unwrap();
        assert!(
            template.contains("ZIRCON_DRIVER_BEGIN_PRIV_V1(Driver, Ops, VendorName, Version, 0)")
        );
    }

    #[test]
    fn one_instruction_v1() {
        let bind_rules = CompiledBindRules::Bind(BindRules {
            instructions: vec![SymbolicInstructionInfo {
                location: None,
                instruction: SymbolicInstruction::UnconditionalBind,
            }],
            symbol_table: HashMap::new(),
            use_new_bytecode: false,
            enable_debug: false,
        });

        let bytecode = bind_rules.encode_to_bytecode().unwrap();
        assert_eq!(bytecode, vec![0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0]);

        let bind_rules = BindRules {
            instructions: vec![SymbolicInstructionInfo {
                location: None,
                instruction: SymbolicInstruction::UnconditionalBind,
            }],
            symbol_table: HashMap::new(),
            use_new_bytecode: false,
            enable_debug: false,
        };
        let template = write_bind_template(bind_rules).unwrap();
        assert!(
            template.contains("ZIRCON_DRIVER_BEGIN_PRIV_V1(Driver, Ops, VendorName, Version, 1)")
        );
        assert!(template.contains("{0x1000000,0x0,0x0}"));
    }

    #[test]
    fn zero_instructions_v2() {
        let bind_rules = CompiledBindRules::Bind(BindRules {
            instructions: vec![],
            symbol_table: HashMap::new(),
            use_new_bytecode: true,
            enable_debug: false,
        });
        assert_eq!(
            bind_rules.encode_to_bytecode().unwrap(),
            vec![
                66, 73, 78, 68, 2, 0, 0, 0, 0, 83, 89, 78, 66, 0, 0, 0, 0, 73, 78, 83, 84, 0, 0, 0,
                0
            ]
        );

        let bind_rules = BindRules {
            instructions: vec![],
            symbol_table: HashMap::new(),
            use_new_bytecode: true,
            enable_debug: false,
        };
        let template = write_bind_template(bind_rules).unwrap();
        assert!(
            template.contains("ZIRCON_DRIVER_BEGIN_PRIV_V2(Driver, Ops, VendorName, Version, 25)")
        );
        assert!(template.contains(
            "0x42,0x49,0x4e,0x44,0x2,0x0,0x0,0x0,0x0,0x53,0x59,0x4e,0x42,0x0,\
             0x0,0x0,0x0,0x49,0x4e,0x53,0x54,0x0,0x0,0x0,0x0"
        ));
    }

    #[test]
    fn one_instruction_v2() {
        let bind_rules = CompiledBindRules::Bind(BindRules {
            instructions: vec![SymbolicInstructionInfo {
                location: None,
                instruction: SymbolicInstruction::UnconditionalAbort,
            }],
            symbol_table: HashMap::new(),
            use_new_bytecode: true,
            enable_debug: false,
        });
        assert_eq!(
            bind_rules.encode_to_bytecode().unwrap(),
            vec![
                66, 73, 78, 68, 2, 0, 0, 0, 0, 83, 89, 78, 66, 0, 0, 0, 0, 73, 78, 83, 84, 1, 0, 0,
                0, 48
            ]
        );

        let bind_rules = BindRules {
            instructions: vec![SymbolicInstructionInfo {
                location: None,
                instruction: SymbolicInstruction::UnconditionalAbort,
            }],
            symbol_table: HashMap::new(),
            use_new_bytecode: true,
            enable_debug: false,
        };
        let template = write_bind_template(bind_rules).unwrap();
        assert!(
            template.contains("ZIRCON_DRIVER_BEGIN_PRIV_V2(Driver, Ops, VendorName, Version, 26)")
        );
        assert!(template.contains(
            "0x42,0x49,0x4e,0x44,0x2,0x0,0x0,0x0,0x0,0x53,0x59,0x4e,0x42,0x0,0x0,\
             0x0,0x0,0x49,0x4e,0x53,0x54,0x1,0x0,0x0,0x0,0x30"
        ));
    }

    #[test]
    fn disable_autobind() {
        let bind_rules = CompiledBindRules::Bind(BindRules {
            instructions: vec![
                SymbolicInstructionInfo::disable_autobind(),
                SymbolicInstructionInfo {
                    location: None,
                    instruction: SymbolicInstruction::UnconditionalBind,
                },
            ],
            symbol_table: HashMap::new(),
            use_new_bytecode: false,
            enable_debug: false,
        });

        let bytecode = bind_rules.encode_to_bytecode().unwrap();
        assert_eq!(bytecode[..12], [2, 0, 0, 0x20, 0, 0, 0, 0, 0, 0, 0, 0]);

        let bind_rules = BindRules {
            instructions: vec![
                SymbolicInstructionInfo::disable_autobind(),
                SymbolicInstructionInfo {
                    location: None,
                    instruction: SymbolicInstruction::UnconditionalBind,
                },
            ],
            symbol_table: HashMap::new(),
            use_new_bytecode: false,
            enable_debug: false,
        };
        let template = write_bind_template(bind_rules).unwrap();
        assert!(
            template.contains("ZIRCON_DRIVER_BEGIN_PRIV_V1(Driver, Ops, VendorName, Version, 2)")
        );
        assert!(template.contains("{0x20000002,0x0,0x0}"));
    }

    #[test]
    fn depfile_no_includes() {
        let output = PathBuf::from("/a/output");
        let input = PathBuf::from("/a/input");
        assert_eq!(
            write_depfile(&output, &Some(input), &[]).unwrap(),
            "/a/output: /a/input\n".to_string()
        );
    }

    #[test]
    fn depfile_no_input() {
        let output = PathBuf::from("/a/output");
        let includes = vec![PathBuf::from("/a/include"), PathBuf::from("/b/include")];
        let result = write_depfile(&output, &None, &includes).unwrap();
        assert!(result.starts_with("/a/output:"));
        assert!(result.contains("/a/include"));
        assert!(result.contains("/b/include"));
    }

    #[test]
    fn depfile_input_and_includes() {
        let output = PathBuf::from("/a/output");
        let input = PathBuf::from("/a/input");
        let includes = vec![PathBuf::from("/a/include"), PathBuf::from("/b/include")];
        let result = write_depfile(&output, &Some(input), &includes).unwrap();
        assert!(result.starts_with("/a/output:"));
        assert!(result.contains("/a/input"));
        assert!(result.contains("/a/include"));
        assert!(result.contains("/b/include"));
    }

    #[test]
    fn composite_bind() {
        let composite_bind_rules = "composite wallcreeper;
            primary node \"wagtail\" {
                fuchsia.BIND_PROTOCOL == 1;
            }
            node \"redpoll\" {
                fuchsia.BIND_PROTOCOL == 2;
            }";

        let compiled_bind_rules =
            compiler::compile(&composite_bind_rules, &vec![], false, false, true, false).unwrap();

        assert_eq!(
            compiled_bind_rules.encode_to_bytecode().unwrap(),
            vec![
                0x42, 0x49, 0x4e, 0x44, 0x02, 0x00, 0x00, 0x00, // BIND header
                0x00, // debug_flag byte
                0x53, 0x59, 0x4e, 0x42, 0x28, 0x00, 0x00, 0x00, // SYMB header
                0x01, 0x00, 0x00, 0x00, // "wallcreeper" ID
                0x77, 0x61, 0x6c, 0x6c, 0x63, 0x72, 0x65, 0x65, // "wallcree"
                0x70, 0x65, 0x72, 0x00, // "per"
                0x02, 0x00, 0x00, 0x00, // "wagtail" ID
                0x77, 0x61, 0x67, 0x74, 0x61, 0x69, 0x6c, 0x00, // "wagtail"
                0x03, 0x00, 0x00, 0x00, // "redpoll" ID
                0x72, 0x65, 0x64, 0x70, 0x6f, 0x6c, 0x6c, 0x00, // "redpoll"
                0x43, 0x4f, 0x4d, 0x50, 0x2c, 0x00, 0x00, 0x00, // COMP header
                0x01, 0x00, 0x00, 0x00, // Device name ID
                0x50, 0x02, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, // Primary node header
                0x01, 0x01, 0x01, 0x00, 0x00, 0x00, // BIND_PROTOCOL ==
                0x01, 0x01, 0x00, 0x00, 0x00, // 1
                0x51, 0x03, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, // Node header
                0x01, 0x01, 0x01, 0x00, 0x00, 0x00, // // BIND_PROTOCOL ==
                0x01, 0x02, 0x00, 0x00, 0x00 // 2
            ]
        );

        let compiled_bind_rules =
            compiler::compile(&composite_bind_rules, &vec![], false, false, true, false).unwrap();

        assert_matches!(compiled_bind_rules, CompiledBindRules::CompositeBind(_));
        if let CompiledBindRules::CompositeBind(bind_rules) = compiled_bind_rules {
            assert_eq!(
                include_str!("tests/expected_composite_code"),
                write_composite_bind_template(bind_rules, false).unwrap()
            );
        }
    }

    #[test]
    fn composite_bind_with_node_name_dashes() {
        let composite_bind_rules = "composite grey_lourie;
            primary node \"go-away-bird\" {
                fuchsia.BIND_PROTOCOL == 1;
            }";

        let compiled_bind_rules =
            compiler::compile(&composite_bind_rules, &vec![], false, false, true, false).unwrap();
        assert_matches!(compiled_bind_rules, CompiledBindRules::CompositeBind(_));
        if let CompiledBindRules::CompositeBind(bind_rules) = compiled_bind_rules {
            assert_eq!(
                include_str!("tests/expected_composite_code_w_dashes"),
                write_composite_bind_template(bind_rules, false).unwrap()
            );
        }
    }

    #[test]
    fn composite_bind_with_disabled_fragment_gen() {
        let composite_bind_rules = "composite grey_lourie;
            primary node \"go-away-bird\" {
                fuchsia.BIND_PROTOCOL == 1;
            }";

        let compiled_bind_rules =
            compiler::compile(&composite_bind_rules, &vec![], false, false, true, false).unwrap();
        assert_matches!(compiled_bind_rules, CompiledBindRules::CompositeBind(_));
        if let CompiledBindRules::CompositeBind(bind_rules) = compiled_bind_rules {
            assert_eq!(
                include_str!("tests/expected_composite_code_w_disabled_fragments"),
                write_composite_bind_template(bind_rules, true).unwrap()
            );
        }
    }

    #[test]
    fn test_bind_lib_generation() {
        assert_eq!(
            include_str!("tests/expected_bind_lib_gen"),
            generate_bind_library(ir_importer::bindc_test::IR).unwrap()
        );
    }
}
