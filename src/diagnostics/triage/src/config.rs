// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::Options,
    anyhow::{bail, format_err, Error},
    libtriage::{
        act::Actions,
        config::{
            action_tag_directive_from_tags, load_config_files, parse_config_files, DiagnosticData,
            ParseResult,
        },
        metrics::Metrics,
        validate::validate,
    },
    std::str::FromStr,
};

// TODO(fxb/50451): Add support for CSV.
#[derive(Debug, PartialEq)]
pub enum OutputFormat {
    Text,
}

impl FromStr for OutputFormat {
    type Err = anyhow::Error;
    fn from_str(output_format: &str) -> Result<Self, Self::Err> {
        match output_format {
            "text" => Ok(OutputFormat::Text),
            incorrect => {
                Err(format_err!("Invalid output type '{}' - must be 'csv' or 'text'", incorrect))
            }
        }
    }
}

/// Complete program execution context.
pub struct ProgramStateHolder {
    pub metrics: Metrics,
    pub actions: Actions,
    pub diagnostic_data: Vec<DiagnosticData>,
    pub output_format: OutputFormat,
}

/// Parses the inspect.json file and all the config files.
pub fn initialize(options: Options) -> Result<ProgramStateHolder, Error> {
    let Options { data_directories, output_format, config_files, tags, exclude_tags, .. } = options;

    let action_tag_directive = action_tag_directive_from_tags(tags, exclude_tags);

    let diagnostic_data = data_directories
        .into_iter()
        .map(|path| DiagnosticData::initialize_from_directory(path))
        .collect::<Result<Vec<_>, Error>>()?;

    if config_files.len() == 0 {
        bail!("Need at least one config file; use --config");
    }

    let config_file_map = load_config_files(&config_files)?;
    let ParseResult { actions, metrics, tests } =
        parse_config_files(config_file_map, action_tag_directive)?;

    validate(&metrics, &actions, &tests)?;

    Ok(ProgramStateHolder { metrics, actions, diagnostic_data, output_format })
}

#[cfg(test)]
mod test {
    use {super::*, anyhow::Error};

    #[test]
    fn output_format_from_string() -> Result<(), Error> {
        assert_eq!(OutputFormat::from_str("text")?, OutputFormat::Text);
        assert!(OutputFormat::from_str("").is_err(), "Should have returned 'Err' on ''");
        assert!(OutputFormat::from_str("CSV").is_err(), "Should have returned 'Err' on 'CSV'");
        assert!(OutputFormat::from_str("Text").is_err(), "Should have returned 'Err' on 'Text'");
        assert!(
            OutputFormat::from_str("GARBAGE").is_err(),
            "Should have returned 'Err' on 'GARBAGE'"
        );
        Ok(())
    }
}
