// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! # JSON generation for intl
//!
//! `strings_to_json` is a program that takes a `strings.xml` file for a source language, a
//! compatible `strings.xml` file for a target language, and produces a Fuchsia localized resource.
//! Please see the `README.md` file in this program's directory for more information.

use {
    anyhow::Context,
    anyhow::Error,
    anyhow::Result,
    intl_strings::{json, parser, veprintln},
    std::env,
    std::fs::File,
    std::io,
    std::path::PathBuf,
    structopt::StructOpt,
};

#[derive(Debug, StructOpt)]
#[structopt(
    name = "Packages translated messages from strings.xml files into a JSON localized resource"
)]
struct Args {
    #[structopt(
        long = "source-locale",
        help = "The locale ID for the locale that is used as the message source of truth. Example: en-US"
    )]
    source_locale: String,
    #[structopt(
        long = "target-locale",
        help = "The locale ID for the locale that is used as the target for bundling. Example: nl-NL"
    )]
    target_locale: String,
    #[structopt(long = "source-strings-file", help = "The path to the source strings.xml file")]
    source_strings_file: PathBuf,
    #[structopt(
        long = "target-strings-file",
        help = "The path to the strings.xml file containing translated messages"
    )]
    target_strings_file: PathBuf,
    #[structopt(
        long = "output",
        help = "The path to the JSON file that should be generated as output"
    )]
    output: PathBuf,
    #[structopt(long = "verbose", help = "Verbose output, for debugging")]
    verbose: bool,
    #[structopt(
        long = "replace-missing-with-warning",
        help = "Replaces a missing message 'foo' with 'UNTRANSLATED(foo)' instead of failing"
    )]
    replace_missing_with_warning: bool,
}

// All the input and output files needed for the JSON conversion.
struct SourcesSinks {
    source_strings_file: io::BufReader<File>,
    target_strings_file: io::BufReader<File>,
    output_file: File,
}

fn open_single_read(what: &str, path: &PathBuf) -> Result<io::BufReader<File>> {
    let input_str = path.to_str().with_context(|| {
        format!("{} filename is not utf-8, what? Use --verbose flag to print the value.", what)
    })?;
    Ok(io::BufReader::new(
        File::open(path).with_context(|| format!("could not open {}: {}", what, input_str))?,
    ))
}

/// Open the needed files, and handle usual errors.
fn open_files(args: &Args) -> Result<SourcesSinks, Error> {
    let source_input = open_single_read("source_strings_file", &args.source_strings_file)?;
    let target_input = open_single_read("target_strings_file", &args.target_strings_file)?;
    let output_str = args.output.to_str().with_context(|| {
        "output filename is not utf-8, what? Use --verbose flag to print the value."
    })?;
    let output = File::create(&args.output)
        .with_context(|| format!("could not open output file: {}", output_str))?;
    Ok(SourcesSinks {
        source_strings_file: source_input,
        target_strings_file: target_input,
        output_file: output,
    })
}

fn run(args: Args) -> Result<(), Error> {
    veprintln!(args.verbose, "args: {:?}", args);

    let file_gaggle = open_files(&args).with_context(|| "while opening files")?;

    let source_reader = parser::Instance::reader(file_gaggle.source_strings_file);
    let mut source_parser = parser::Instance::new(args.verbose);
    let source_dictionary = source_parser.parse(source_reader).with_context(|| {
        format!("while parsing source dictionary: {:?}", &args.source_strings_file)
    })?;

    // Repetitive, but allows us to avoid copying dictionaries, which could be large.
    let target_reader = parser::Instance::reader(file_gaggle.target_strings_file);
    let mut target_parser = parser::Instance::new(args.verbose);
    let target_dictionary = target_parser.parse(target_reader).with_context(|| {
        format!("while parsing target dictionary: {:?}", &args.target_strings_file)
    })?;

    let model = json::model_from_dictionaries(
        &args.source_locale,
        &source_dictionary,
        &args.target_locale,
        &target_dictionary,
        args.replace_missing_with_warning,
    )?;

    // And at the very end, victoriously write the file out.
    model.to_json_writer(file_gaggle.output_file)
}

fn main() -> Result<(), Error> {
    env::set_var("RUST_BACKTRACE", "full");
    run(Args::from_args())
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;
    use std::io::Write;
    use tempfile;

    // This test is only used to confirm that a program call generates some
    // output that looks meaningful.  Refer to the unit tests in the library
    // for tests that actually enforce the specification.
    #[test]
    fn basic() -> Result<(), Error> {
        let en = tempfile::NamedTempFile::new()?;
        write!(
            en.as_file(),
            r#"
               <?xml version="1.0" encoding="utf-8"?>
               <resources>
                 <!-- comment -->
                 <string
                   name="string_name"
                     >string</string>
               </resources>

            "#
        )
        .with_context(|| "while writing 'en' tempfile")?;
        let fr = tempfile::NamedTempFile::new()?;

        write!(
            fr.as_file(),
            r#"
               <?xml version="1.0" encoding="utf-8"?>
               <resources>
                 <!-- comment -->
                 <string
                   name="string_name"
                     >le stringue</string>
               </resources>
            "#
        )
        .with_context(|| "while writing 'fr' tempfile")?;

        let fr_json = tempfile::NamedTempFile::new()?;

        let args = Args {
            source_locale: "en".to_string(),
            target_locale: "fr".to_string(),
            source_strings_file: en.path().to_path_buf(),
            target_strings_file: fr.path().to_path_buf(),
            output: fr_json.path().to_path_buf(),
            verbose: false,
            replace_missing_with_warning: false,
        };
        run(args)?;

        let outcome = fs::read_to_string(fr_json.path())?;
        assert_eq!(
            r#"{"locale_id":"fr","source_locale_id":"en","num_messages":1,"messages":{"7134240810508078445":"le stringue"}}"#,
            outcome
        );
        Ok(())
    }

    #[test]
    fn early_comment_not_allowed() -> Result<(), Error> {
        struct TestCase {
            name: &'static str,
            en: &'static str,
            fr: &'static str,
        }
        let tests = vec![
            TestCase {
                name: "there is a wrong comment in en",
                en: r#"
               <!-- comment not allowed here -->
               <?xml version="1.0" encoding="utf-8"?>
               <resources>
                 <!-- comment -->
                 <string
                   name="string_name"
                     >string</string>
               </resources>

            "#,
                fr: r#"
               <?xml version="1.0" encoding="utf-8"?>
               <resources>
                 <!-- comment -->
                 <string
                   name="string_name"
                     >le stringue</string>
               </resources>
            "#,
            },
            TestCase {
                name: "there is a wrong comment in fr",
                en: r#"
               <?xml version="1.0" encoding="utf-8"?>
               <resources>
                 <!-- comment -->
                 <string
                   name="string_name"
                     >string</string>
               </resources>

            "#,
                fr: r#"
               <!-- comment not allowed here -->
               <?xml version="1.0" encoding="utf-8"?>
               <resources>
                 <!-- comment -->
                 <string
                   name="string_name"
                     >le stringue</string>
               </resources>
            "#,
            },
        ];

        for test in tests.iter() {
            let en = tempfile::NamedTempFile::new()?;
            write!(en.as_file(), "{}", test.en).with_context(|| "while writing 'en' tempfile")?;
            let fr = tempfile::NamedTempFile::new()?;

            write!(fr.as_file(), "{}", test.fr).with_context(|| "while writing 'fr' tempfile")?;

            let fr_json = tempfile::NamedTempFile::new()?;

            let args = Args {
                source_locale: "en".to_string(),
                target_locale: "fr".to_string(),
                source_strings_file: en.path().to_path_buf(),
                target_strings_file: fr.path().to_path_buf(),
                output: fr_json.path().to_path_buf(),
                verbose: false,
                replace_missing_with_warning: false,
            };
            if let Ok(_) = run(args) {
                return Err(anyhow::anyhow!("unexpected OK in test: {}", &test.name));
            }
        }

        Ok(())
    }
}
