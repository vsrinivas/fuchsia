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
        io::{BufWriter, Write},
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
    "blobfs-compression",
    "bootserver",
    "cmc",
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
        write_formatted_output(&cmd_path, output_path)
            .context(format!("Unable to write tool at {:?} to {:?}", cmd_path, output_path))?;
    }

    info!("Generated documentation at dir: {}", &output_path.display());

    Ok(())
}

/// Helper function for write_formatted_output.
///
/// Recursively calls `cmd_name`'s subcommands and writes to `output_writer`.
fn recurse_cmd_output<W: Write>(
    cmd_name: &str,
    cmd_path: &PathBuf,
    output_writer: &mut W,
    cmds_sequence: &Vec<&String>,
) -> Result<()> {
    // Create vector to collect subcommands.
    let mut cmds_list: Vec<String> = Vec::new();

    let mut inside_command_section = false;

    // Formatting styles for codeblocks.
    let codeblock_formatting_start = "```none {: style=\"white-space: break-spaces;\" \
        .devsite-disable-click-to-copy}\n";
    let codeblock_formatting_end = "```\n";

    // Track command level starting from 0, to set command headers' formatting.
    let cmd_level = cmds_sequence.len();

    // Write out the header.
    let cmd_heading_formatting = "#".repeat(cmd_level + 1);
    writeln!(output_writer, "{} {}\n", cmd_heading_formatting, cmd_name)?;
    writeln!(output_writer, "{}", codeblock_formatting_start)?;

    // Get terminal output for cmd <subcommands> --help for a given command.
    let lines: Vec<String> = help_output_for(&cmd_path, &cmds_sequence)?;

    for line in lines {
        // TODO(fxb/69457): Capture all section headers in addition to "Commands" and "Options".
        if line.to_lowercase() == "commands:" || line.to_lowercase() == "options:" {
            // End preformatting before writing a section header.
            writeln!(output_writer, "{}", codeblock_formatting_end)?;
            // Write the section heading.
            writeln!(output_writer, "__{}__\n", line)?;
            // Begin preformatting for next section of non-headers.
            writeln!(output_writer, "{}", codeblock_formatting_start)?;
            inside_command_section = line.to_lowercase() == "commands:";
        } else {
            // Command section ends at a blank line (or end of file).
            if line.trim() == "" {
                inside_command_section = false;
            } else if line.contains(&cmd_path.as_path().display().to_string()) {
                let line_no_path =
                    line.replace(&cmd_path.as_path().display().to_string(), &cmd_name);
                // Write line after stripping full path preceeding command name.
                writeln!(output_writer, "{}", line_no_path)?;
            } else if !line.contains("sdk WARN:") && !line.contains("See 'ffx help <command>'") {
                // TODO(fxb/71456): Remove filtering ffx repeated line after documentation standardized.
                // Write non-header lines unedited.
                writeln!(output_writer, "{}", line)?;
            }
            // Collect commands into a vector.
            if inside_command_section {
                // Command name is the first word on the line.
                if let Some(command) = line.split_whitespace().next() {
                    cmds_list.push(command.to_string());
                }
            }
        }
    }
    // Close preformatting at the end.
    writeln!(output_writer, "{}", codeblock_formatting_end)?;
    cmds_list.sort();

    for cmd in cmds_list {
        // Copy current command sequence and append newest command.
        let mut cmds_sequence = cmds_sequence.clone();
        cmds_sequence.push(&cmd);
        recurse_cmd_output(&cmd, &cmd_path, output_writer, &cmds_sequence)?;
    }

    Ok(())
}

/// Write output of cmd at `cmd_path` to new cmd.md file at `output_path`.
fn write_formatted_output(cmd_path: &PathBuf, output_path: &PathBuf) -> Result<()> {
    // Get name of command from full path to the command executable.
    let cmd_name = cmd_path.file_name().expect("Could not get file name for command");
    let output_md_path = md_path(&cmd_name, &output_path);

    // Create vector for commands to call in sequence.
    let cmd_sequence = Vec::new();

    // Create a buffer writer to format and write consecutive lines to a file.
    let file = File::create(&output_md_path).context(format!("create {:?}", output_md_path))?;
    let output_writer = &mut BufWriter::new(file);

    let cmd_name = cmd_name.to_str().expect("Could not convert cmd_name from OsStr to str");

    // Write ouput for cmd and all of its subcommands.
    recurse_cmd_output(&cmd_name, &cmd_path, output_writer, &cmd_sequence)
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
fn help_output_for(tool: &Path, subcommands: &Vec<&String>) -> Result<Vec<String>> {
    let output = Command::new(&tool)
        .args(&*subcommands)
        .arg("--help")
        .output()
        .context(format!("Command failed for {:?}", &tool.display()))?;

    let stdout = output.stdout;
    let stderr = output.stderr;

    // Convert string outputs to vector of lines.
    let stdout_string = String::from_utf8(stdout).expect("Help string from utf8");
    let mut combined_lines = stdout_string.lines().map(String::from).collect::<Vec<_>>();

    let stderr_string = String::from_utf8(stderr).expect("Help string from utf8");
    let stderr_lines = stderr_string.lines().map(String::from).collect::<Vec<_>>();

    combined_lines.extend(stderr_lines);

    Ok(combined_lines)
}

/// Given a cmd name and a dir, create a full path ending in cmd.md.
fn md_path(file_stem: &OsStr, dir: &PathBuf) -> PathBuf {
    let mut path = Path::new(dir).join(file_stem);
    path.set_extension("md");
    path
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        std::{fs::File, io::Read, os::unix::fs::PermissionsExt, path::PathBuf, process},
    };

    /// Helper method for write_formatted_output_test(), creates mock cli tool.
    fn create_temp_script(path: &PathBuf) -> Result<()> {
        let mut file = File::create(&path)?;

        // Set permission to executable.
        fs::set_permissions(&path, fs::Permissions::from_mode(0o770))?;

        // Build a bash script mimicking a tool with --help output.

        let output = process::Command::new("/usr/bin/env").arg("which").arg("bash").output()?;
        let bash_path = PathBuf::from(String::from_utf8_lossy(&output.stdout).trim());
        let bash_line = format!("#!{}", bash_path.display());
        let lines = vec![
            bash_line.to_string(),
            "\n".to_string(),
            "# No args and --help flag".to_string(),
            "if [[ $1 == \"--help\" ]] && [[ \"$#\" -ne 0 ]]; then".to_string(),
            "    echo \"Usage: host-tool-cmd\"".to_string(),
            "    echo \"Tool description\"".to_string(),
            "    echo \"Options:\"".to_string(),
            "    echo \"--help Display usage information\"".to_string(),
            "    echo \"Commands:\"".to_string(),
            "    echo \"debug Start debug session\"".to_string(),
            "elif [[ $1 == \"debug\" ]] && [[ $2 == \"--help\" ]] && [[ \"$#\" -ne 1 ]]; then"
                .to_string(),
            "# One param and --help flag".to_string(),
            "    echo \"Usage: host-tool-cmd debug [<socket_location>]\"".to_string(),
            "    echo \"Start a debugging session.\"".to_string(),
            "fi".to_string(),
        ];

        // Write lines to file.
        file.write(lines.join("\n").as_bytes()).expect("Unable to write to temporary file");

        Ok(())
    }

    #[test]
    fn write_formatted_output_test() -> Result<()> {
        // Create temp file for a mock tool script.
        let cmd_path = PathBuf::from(r"/tmp/host-tool-cmd.sh");

        create_temp_script(&cmd_path)?;

        let output_path = PathBuf::from(r"/tmp/");

        // Creates filename host-tool-cmd.md to match filename of `cmd_path`,
        // then write formatted output to `output_path` + filename.
        write_formatted_output(&cmd_path, &output_path)?;

        // Write a hard-coded formatted file.
        let formatted_contents = vec![
            "# host-tool-cmd.sh\n".to_string(),
            "```none {: style=\"white-space: break-spaces;\" .devsite-disable-click-to-copy}\n"
                .to_string(),
            "Usage: host-tool-cmd".to_string(),
            "Tool description".to_string(),
            "```\n".to_string(),
            "__Options:__\n".to_string(),
            "```none {: style=\"white-space: break-spaces;\" .devsite-disable-click-to-copy}\n"
                .to_string(),
            "--help Display usage information".to_string(),
            "```\n".to_string(),
            "__Commands:__\n".to_string(),
            "```none {: style=\"white-space: break-spaces;\" .devsite-disable-click-to-copy}\n"
                .to_string(),
            "debug Start debug session".to_string(),
            "```\n".to_string(),
            "## debug\n".to_string(),
            "```none {: style=\"white-space: break-spaces;\" .devsite-disable-click-to-copy}\n"
                .to_string(),
            "Usage: host-tool-cmd debug [<socket_location>]".to_string(),
            "Start a debugging session.".to_string(),
            "```\n\n".to_string(),
        ];

        let formatted_file_path = PathBuf::from("/tmp/host-tool-cmd-formatted.md");
        let mut formatted_file =
            File::create(&formatted_file_path).expect("Unable to create temp file");
        formatted_file
            .write(formatted_contents.join("\n").as_bytes())
            .expect("Unable to write to temporary file");

        // Get name of command from full path to the command executable.
        let cmd_name = cmd_path.file_name().expect("Could not get file name for command");
        let output_md_path = md_path(&cmd_name, &output_path);
        // Read the cmd file into a string.
        let mut output_file = File::open(output_md_path)?;
        let mut output_buffer = String::new();
        output_file.read_to_string(&mut output_buffer)?;

        // Read the formatted file into a string.
        let mut formatted_file = File::open(formatted_file_path)?;
        let mut formatted_buffer = String::new();
        formatted_file.read_to_string(&mut formatted_buffer)?;

        // Assert write_formatted_output formatted the documentation correctly.
        assert_eq!(output_buffer, formatted_buffer);
        Ok(())
    }
}
