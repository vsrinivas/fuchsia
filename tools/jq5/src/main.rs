// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow;
use fuchsia_async;
use futures::future::join_all;
use io::BufWriter;
use json5format::{Json5Format, ParsedDocument};
use std::{
    ffi::OsString,
    io::{self, Read, Write},
    path::{Path, PathBuf},
    process::{Command, Stdio},
};
use structopt::StructOpt;

mod reader;
mod traverser;

/// Spawns a `jq` process with the specified filter and pipes `json_string` into its stdin. Returns
/// its jq output or an error if it produces an error. If `jq_path` is `None`, it assumes `jq` is in
/// the system path and attempts to invoke it using simply the command `jq`. Otherwise, it invokes
/// `jq` using the provided path.
async fn run_jq(
    filter: &String,
    json_string: String,
    jq_path: &Option<PathBuf>,
) -> Result<String, anyhow::Error> {
    let mut cmd_jq = match jq_path {
        Some(path) => {
            let command_str = path.as_path().to_str().unwrap();
            if !Path::exists(Path::new(&command_str)) {
                return Err(anyhow::anyhow!(
                    "Path provided in path-to-jq option did not specify a valid path to a binary."
                ));
            }
            Command::new(command_str)
                .arg(&filter[..])
                .stdin(Stdio::piped())
                .stdout(Stdio::piped())
                .spawn()?
        }
        None => {
            let command_string = OsString::from("fx");

            Command::new(&command_string)
                .arg("jq")
                .arg(&filter[..])
                .stdin(Stdio::piped())
                .stdout(Stdio::piped())
                .spawn()?
        }
    };
    let mut cmd_jq_stdin = cmd_jq.stdin.take().unwrap();

    let bytestring = json_string.as_bytes();
    let mut writer = BufWriter::new(&mut cmd_jq_stdin);
    writer.write_all(bytestring)?;

    //Close stdin
    writer.flush()?;
    drop(writer);
    drop(cmd_jq_stdin);

    let status = cmd_jq.wait()?;
    let mut cmd_jq_stdout = String::new();
    let mut cmd_jq_stderr = String::new();
    let stdout = cmd_jq.stdout;
    let stderr = cmd_jq.stderr;
    if let Some(mut err) = stderr {
        err.read_to_string(&mut cmd_jq_stderr)?;
        Err(anyhow::anyhow!("jq produced the following error message:\n {}", cmd_jq_stderr))
    } else if let Some(mut out) = stdout {
        out.read_to_string(&mut cmd_jq_stdout)?;
        Ok(cmd_jq_stdout)
    } else if !status.success() {
        Err(anyhow::anyhow!("jq returned with non-zero exit code but no error message"))
    } else {
        Err(anyhow::anyhow!("jq returned exit code 0 but no output or error message"))
    }
}

/// Calls jq on the provided json and then fills back comments at correct places.
async fn run_jq5(
    filter: &String,
    parsed_json5: ParsedDocument,
    json_string: String,
    jq_path: &Option<PathBuf>,
) -> Result<String, anyhow::Error> {
    let jq_out = run_jq(&filter, json_string, jq_path).await?;
    let mut parsed_json = ParsedDocument::from_string(jq_out, None)?;
    traverser::fill_comments(&parsed_json5.content, &mut parsed_json.content)?;

    let format = Json5Format::new()?;
    Ok(format.to_string(&parsed_json)?)
}

/// Calls `run_jq5` on the contents of a file and returns the return value of `run_jq5`.
async fn run_jq5_on_file(
    filter: &String,
    file: &PathBuf,
    jq_path: &Option<PathBuf>,
) -> Result<String, anyhow::Error> {
    let (parsed_json5, json_string) = reader::read_json5_fromfile(&file)?;
    run_jq5(&filter, parsed_json5, json_string, jq_path).await
}

async fn run(
    filter: String,
    files: Vec<PathBuf>,
    jq_path: &Option<PathBuf>,
) -> Result<Vec<String>, anyhow::Error> {
    let mut jq5_output_futures = Vec::with_capacity(files.len());

    for file in files.iter() {
        jq5_output_futures.push(run_jq5_on_file(&filter, file, &jq_path));
    }

    let jq5_outputs = join_all(jq5_output_futures).await;

    let mut trusted_outs = Vec::with_capacity(jq5_outputs.len());
    for (i, jq5_output) in jq5_outputs.into_iter().enumerate() {
        match jq5_output {
            Err(err) => {
                return Err(anyhow::anyhow!(
                    r"jq5 encountered an error processing at least one of the provided json5 objects.
                    The first error occured while processing file'{}':

                    {}",
                    files[i].as_path().to_str().unwrap(),
                    err
                ));
            }
            Ok(output) => {
                trusted_outs.push(output);
            }
        }
    }

    Ok(trusted_outs)
}

#[fuchsia_async::run_singlethreaded]
async fn main() -> Result<(), anyhow::Error> {
    eprintln!("{}", "This tool is a work in progress: use with caution.\n");
    let args = Opt::from_args();

    if args.files.len() == 0 {
        let (parsed_json5, json_string) = reader::read_json5_from_input(&mut io::stdin())?;

        let out = run_jq5(&args.filter, parsed_json5, json_string, &args.jq_path).await?;
        io::stdout().write_all(out.as_bytes())?;
    } else {
        let outs = run(args.filter, args.files, &args.jq_path).await?;

        for out in outs {
            io::stdout().write_all(out.as_bytes())?;
        }
    }
    Ok(())
}
#[derive(Debug, StructOpt)]
#[structopt(
    name = "jq5",
    about = "An extension of jq to work on json5 objects. \nThis tool is a work in progress: use with caution."
)]
struct Opt {
    // TODO(72435) Add relevant options from jq
    filter: String,

    #[structopt(parse(from_os_str))]
    files: Vec<PathBuf>,

    #[structopt(long = "--path-to-jq", parse(from_os_str))]
    jq_path: Option<PathBuf>,
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::env;
    use std::fs::OpenOptions;

    const JQ_PATH_STR: &str = env!("JQ_PATH");

    // Tests that run_jq successfully invokes jq using the identity filter and
    // an empty JSON object.
    #[fuchsia_async::run_singlethreaded(test)]
    async fn run_jq_id_filter_1() {
        let filter = String::from(".");
        let input = String::from("{}");
        let jq_path = Some(PathBuf::from(JQ_PATH_STR));
        assert_eq!(run_jq(&filter, input, &jq_path).await.unwrap(), "{}\n");
    }

    // Tests that run_jq successfully invokes jq using the identity filter and a
    // simple JSON object.
    #[fuchsia_async::run_singlethreaded(test)]
    async fn run_jq_id_filter_2() {
        let filter = String::from(".");
        let input = String::from(r#"{"foo": 1, "bar": 2}"#);
        let jq_path = Some(PathBuf::from(JQ_PATH_STR));
        assert_eq!(
            run_jq(&filter, input, &jq_path).await.unwrap(),
            r##"{
  "foo": 1,
  "bar": 2
}
"##
        );
    }

    // Tests a simple filter and simple object.
    #[fuchsia_async::run_singlethreaded(test)]
    async fn run_jq_deconstruct_filter() {
        let filter = String::from("{foo2: .foo1, bar2: .bar1}");
        let input = String::from(r#"{"foo1": 0, "bar1": 42}"#);
        let jq_path = Some(PathBuf::from(JQ_PATH_STR));
        assert_eq!(
            run_jq(&filter, input, &jq_path).await.unwrap(),
            r##"{
  "foo2": 0,
  "bar2": 42
}
"##
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn run_jq5_deconstruct_filter() {
        let filter = String::from("{foo: .foo, baz: .bar}");
        let json5_string = String::from(
            r##"{
  //Foo
  foo: 0,
  //Bar
  bar: 42
}"##,
        );
        let format = Json5Format::new().unwrap();
        let (parsed_json5, json_string) = reader::read_json5(json5_string).unwrap();
        let jq_path = Some(PathBuf::from(JQ_PATH_STR));
        assert_eq!(
            run_jq5(&filter, parsed_json5, json_string, &jq_path).await.unwrap(),
            format
                .to_string(
                    &ParsedDocument::from_str(
                        r##"{
  //Foo
  foo: 0,
  baz: 42
}"##,
                        None
                    )
                    .unwrap()
                )
                .unwrap()
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn run_jq5_on_file_w_id_filter() {
        let tmp_path = PathBuf::from(r"/tmp/read_from_file_2.json5");
        let mut file = OpenOptions::new()
            .read(true)
            .write(true)
            .create(true)
            .open(tmp_path.as_path())
            .unwrap();
        let json5_string = String::from(
            r##"{
    "name": {
        "last": "Smith",
        "first": "John",
        "middle": "Jacob"
    },
    "children": [
        "Buffy",
        "Biff",
        "Balto"
    ],
    // Consider adding a note field to the `other` contact option
    "contact_options": [
        {
            "home": {
                "email": "jj@notreallygmail.com",   // This was the original user id.
                                                    // Now user id's are hash values.
                "phone": "212-555-4321"
            },
            "other": {
                "email": "volunteering@serviceprojectsrus.org"
            },
            "work": {
                "phone": "212-555-1234",
                "email": "john.j.smith@worksforme.gov"
            }
        }
    ],
    "address": {
        "city": "Anytown",
        "country": "USA",
        "state": "New York",
        "street": "101 Main Street"
        /* Update schema to support multiple addresses:
           "work": {
               "city": "Anytown",
               "country": "USA",
               "state": "New York",
               "street": "101 Main Street"
           }
        */
    }
}
"##,
        );
        file.write_all(json5_string.as_bytes()).unwrap();

        let (parsed_json5, json_string) = reader::read_json5_fromfile(&tmp_path).unwrap();
        let jq_path = Some(PathBuf::from(JQ_PATH_STR));
        assert_eq!(
            run_jq5(&".".to_string(), parsed_json5, json_string, &jq_path).await.unwrap(),
            run_jq5_on_file(&".".to_string(), &tmp_path, &jq_path).await.unwrap()
        )
    }
}
