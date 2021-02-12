// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Clidoc generates documentation for host tool commands consisting of their --help output.
use {
    anyhow::{bail, Context, Result},
    argh::FromArgs,
    log::{info, LevelFilter},
    std::{
        collections::HashSet,
        env,
        ffi::{OsStr, OsString},
        fs::{self, File},
        io::{prelude::*, BufWriter},
        path::{Path, PathBuf},
        process::Command,
    },
};

/// CliDoc generates documentation for core Fuchsia developer tools.
#[derive(Debug, FromArgs)]
struct Opt {
    // Default input dir is parent dir of this tool, containing host tools exes
    // $FUCHSIA_DIR/out/default/host_x64 or $FUCHSIA_DIR/out/default/host-tools
    /// set the input folder
    #[argh(
        option,
        short = 'i',
        default = "env::current_exe().unwrap().parent().unwrap().to_path_buf()"
    )]
    in_dir: PathBuf,

    /// set the output directory
    #[argh(option, short = 'o', default = "PathBuf::from(\".\".to_string())")]
    out_dir: PathBuf,

    /// reduce text output
    #[argh(switch)]
    quiet: bool,

    /// increase text output
    #[argh(switch, short = 'v')]
    verbose: bool,
}

// TODO(fxb/69336): Move allow list to its own separate config file.
const ALLOW_LIST: &'static [&'static str] = &[
    "bootserver",
    "cmc",
    "device-finder",
    "far",
    "fconfig",
    "ffx",
    "fidl-format",
    "fidlc",
    "fidlcat",
    "fidlgen",
    "fpublish",
    "fremote",
    "fserve",
    "fssh",
    "fvdl",
    "fvm",
    "merkleroot",
    "minfs",
    "pm",
    "symbol-index",
    "symbolize",
    "symbolizer",
    "triage",
    "zbi",
    "zxdb",
];

fn main() -> Result<()> {
    let opt: Opt = argh::from_env();
    run(opt)
}

fn run(opt: Opt) -> Result<()> {
    if opt.quiet && opt.verbose {
        bail!("cannot use --quiet and --verbose together");
    }

    if opt.verbose {
        println!("verbose true");
        log::set_max_level(LevelFilter::Debug);
    } else if opt.quiet {
        log::set_max_level(LevelFilter::Warn);
    } else {
        log::set_max_level(LevelFilter::Info);
    }

    // Set the directory for the command executables.
    let input_path = &opt.in_dir;
    info!("Input dir: {}", input_path.display());

    // Set the directory to output documentation to.
    let output_path = &opt.out_dir;
    info!("Output dir: {}", output_path.display());

    // Create a set of SDK tools to generate documentation for.
    let allow_list: HashSet<OsString> = ALLOW_LIST.iter().cloned().map(OsString::from).collect();

    // Create a vector of full paths to each command in the allow_list.
    let cmd_paths: Vec<PathBuf> = get_command_paths(&input_path, &allow_list)?;

    // Create the directory for doc files if it doesn't exist.
    create_output_dir(&output_path)
        .context(format!("Unable to create output directory {:?}", output_path))?;

    // Write documentation output for each command.
    for cmd_path in cmd_paths.iter() {
        let cmd_name =
            cmd_path.file_name().expect("Could not get file name for command").to_os_string();
        let path = path_for(&cmd_name, &output_path);

        // Get terminal output for cmd --help for a given command.
        let lines: Vec<String> = help_output_for(&cmd_path)?;

        let cmd_name = cmd_name.to_str().expect("Could not convert cmd_name to str");
        write_formatted_output(&cmd_name, &lines, &path)
            .with_context(|| format!("Unable to write {:?} at {:?}", cmd_name, path))?;
    }

    info!("Generated documentation at dir: {}", &output_path.display());

    Ok(())
}

/// Format `lines` (`cmd_name`'s --help output) in markdown and write to the `path`.
fn write_formatted_output(cmd_name: &str, lines: &Vec<String>, path: &Path) -> Result<()> {
    // Create a buffer writer to format and write consecutive lines to a file.
    let file = File::create(&path).context(format!("create {:?}", path))?;
    let mut output_writer = BufWriter::new(file);

    // Write out the header.
    writeln!(&mut output_writer, "# {} Reference\n", cmd_name)?;
    writeln!(&mut output_writer, "```")?;

    for line in lines {
        // TODO(fxb/69457): Capture all section headers in addition to "Commands" and "Options".
        if line == "Commands:" || line == "Options:" {
            // End preformatting before writing a section header.
            writeln!(&mut output_writer, "```\n")?;
            // Write the section heading.
            writeln!(&mut output_writer, "## {}\n", line)?;
            // Begin preformatting for next section of non-headers.
            writeln!(&mut output_writer, "```")?;
        } else {
            // Write non-header lines unedited.
            writeln!(&mut output_writer, "{}", line)?;
        }
    }
    // Close preformatting at the end.
    writeln!(&mut output_writer, "```")?;
    Ok(())
}

/// Generate a vector of full paths to each command in the allow_list.
fn get_command_paths(input_path: &Path, allow_list: &HashSet<OsString>) -> Result<Vec<PathBuf>> {
    // Build a set of all file names in the input_path dir.
    let mut files = HashSet::new();
    if let Ok(paths) = fs::read_dir(&input_path) {
        for path in paths {
            if let Ok(path) = path {
                files.insert(path.file_name());
            }
        }
    }

    // Get the intersection of all files and commands in the allow_list.
    let commands: HashSet<_> = files.intersection(&allow_list).collect();
    info!("Including tools: {:?}", commands);

    // Build full paths to allowed commands found in the input_path dir.
    let mut cmd_paths = Vec::new();
    for c in commands.iter() {
        let path = Path::new(&input_path).join(c);
        cmd_paths.push(path);
    }
    Ok(cmd_paths)
}

/// Create the output dir if doesn't exist, recursively creating subdirs in path.
fn create_output_dir(path: &Path) -> Result<()> {
    if !path.exists() {
        fs::create_dir_all(path)
            .with_context(|| format!("Unable to create output directory {}", path.display()))?;
        info!("Created directory {}", path.display());
    }
    Ok(())
}

/// Get cmd --help output when given a full path to a cmd.
fn help_output_for(tool: &Path) -> Result<Vec<String>> {
    let output = Command::new(&tool)
        .arg("--help")
        .output()
        .context(format!("Command failed for {:?}", &tool.display()))?;

    let output = output.stdout;
    let string_output = String::from_utf8(output).expect("Help string from utf8");
    let lines = string_output.lines().map(String::from).collect::<Vec<_>>();
    Ok(lines)
}

/// Given a cmd name and a dir, create a full path ending in cmd.md.
fn path_for(file_stem: &OsStr, dir: &PathBuf) -> PathBuf {
    let mut path = Path::new(dir).join(file_stem);
    path.set_extension("md");
    path
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        std::{fs::File, io::Write, path::PathBuf},
    };

    #[test]
    fn write_formatted_output_test() -> Result<()> {
        // Format and write a command line tool's help output to a file.
        let cmd_name = "host-tool-cmd";
        let lines = vec![
            "Usage: host-tool-cmd".to_string(),
            "Tool description".to_string(),
            "Options:".to_string(),
            "--help Display usage information".to_string(),
            "Commands:".to_string(),
            "debug Start debug session:".to_string(),
        ];
        let cmd_output_path = PathBuf::from(r"/tmp/host-tool-cmd.md");
        write_formatted_output(&cmd_name, &lines, &cmd_output_path)?;

        // Write a hard-coded formatted file.
        let formatted_contents = vec![
            "# host-tool-cmd Reference\n".to_string(),
            "```".to_string(),
            "Usage: host-tool-cmd".to_string(),
            "Tool description".to_string(),
            "```\n".to_string(),
            "## Options:\n".to_string(),
            "```".to_string(),
            "--help Display usage information".to_string(),
            "```\n".to_string(),
            "## Commands:\n".to_string(),
            "```".to_string(),
            "debug Start debug session:".to_string(),
            "```\n".to_string(),
        ];

        let formatted_file_path = PathBuf::from("/tmp/host-tool-cmd-2.md");
        let mut formatted_file =
            File::create(&formatted_file_path).expect("Unable to create temp file");
        formatted_file
            .write(formatted_contents.join("\n").as_bytes())
            .expect("Unable to write to temporary file");

        // Read the cmd file into a string.
        let mut cmd_file = File::open(cmd_output_path).unwrap();
        let mut cmd_buffer = String::new();
        cmd_file.read_to_string(&mut cmd_buffer).unwrap();

        // Read the formatted file into a string.
        let mut formatted_file = File::open(formatted_file_path).unwrap();
        let mut formatted_buffer = String::new();
        formatted_file.read_to_string(&mut formatted_buffer).unwrap();

        // Assert write_formatted_output formatted the documentation correctly.
        assert_eq!(cmd_buffer, formatted_buffer);
        Ok(())
    }
}
