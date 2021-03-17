// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow;
use io::BufWriter;
use std::{
    io::{self, Read, Write},
    path::PathBuf,
    process::{Command, Stdio},
};
use structopt::StructOpt;

mod reader;
mod traverser;

fn read_files(files: Vec<PathBuf>) -> Result<Vec<String>, anyhow::Error> {
    let mut json_strings = Vec::with_capacity(files.len());
    for file in files.iter() {
        let object_as_string = (reader::read_json5_fromfile(file)?).1;
        json_strings.push(object_as_string);
    }
    Ok(json_strings)
}

fn run_jq(filter: String, json_strings: Vec<String>) -> Result<(), anyhow::Error> {
    if json_strings.is_empty() {
        return Ok(());
    }

    let mut cmd_jq =
        Command::new("jq").arg(&filter[..]).stdin(Stdio::piped()).stdout(Stdio::piped()).spawn()?;
    let mut cmd_jq_stdin = cmd_jq.stdin.take().unwrap();
    let mut writer = BufWriter::new(&mut cmd_jq_stdin);

    for json_string in json_strings.iter() {
        let bytestring = json_string.as_bytes();
        writer.write_all(bytestring)?;
    }
    //Close stdin
    writer.flush()?;
    drop(writer);
    drop(cmd_jq_stdin);

    let status = cmd_jq.wait()?;
    let mut cmd_jq_stdout = String::new();
    let mut cmd_jq_stderr = String::new();
    let mut stdout = cmd_jq.stdout;
    let mut stderr = cmd_jq.stderr;
    if let Some(_) = stderr {
        stderr.take().unwrap().read_to_string(&mut cmd_jq_stderr)?;
    } else if let Some(_) = stdout {
        stdout.take().unwrap().read_to_string(&mut cmd_jq_stdout)?;
    }
    if !status.success() {
        return Err(anyhow::anyhow!("jq returned with exit code not zero"));
    }

    io::stdout().write_all(&cmd_jq_stdout.as_bytes())?;
    io::stderr().write_all(&cmd_jq_stderr.as_bytes())?;

    Ok(())
}

fn main() -> Result<(), anyhow::Error> {
    println!("{}", "This tool is a work in progress: do not use for large-scale changes.\n");
    let args = Opt::from_args();

    if args.files.len() == 0 {
        //TODO(davidatp) Add support for stdin
        Err(anyhow::anyhow!("jq5 currently only supports reading input from files, not stdin."))
    } else {
        let json_strings = read_files(args.files)?;
        Ok(run_jq(args.filter, json_strings)?)
    }
}
#[derive(Debug, StructOpt)]
#[structopt(
    name = "jq5",
    about = "An extension of jq to work on json5 objects. \nThis tool is a work in progress: do not use for large-scale changes."
)]
struct Opt {
    //TODO(davidatp) Add relevant options from jq
    filter: String,

    #[structopt(parse(from_os_str))]
    files: Vec<PathBuf>,
}
