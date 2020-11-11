// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{bail, Error},
    log::warn,
};

/// Loads an argh struct from the command line.
///
/// We can't just use the one-line argh parse in v2, because that writes to stdout
/// and stdout doesn't currently work in v2 components. Instead, grab and
/// log the output.
pub fn load_command_line<T: argh::FromArgs>() -> Result<T, Error> {
    load_from_strings(std::env::args().collect::<Vec<_>>())
}

fn load_from_strings<T: argh::FromArgs>(arg_strings: Vec<String>) -> Result<T, Error> {
    let arg_strs: Vec<&str> = arg_strings.iter().map(|s| s.as_str()).collect();
    match T::from_args(&[arg_strs[0]], &arg_strs[1..]) {
        Ok(args) => Ok(args),
        Err(output) => {
            for line in output.output.split("\n") {
                warn!("CmdLine: {}", line);
            }
            match output.status {
                Ok(()) => bail!("Exited as requested by command line args"),
                Err(()) => bail!("Exited due to bad command line args"),
            }
        }
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use argh::FromArgs;

    /// Top-level command.
    #[derive(FromArgs, PartialEq, Debug)]
    struct OuterArgs {
        /// subcommands
        #[argh(subcommand)]
        sub: SubChoices,
    }

    /// Sub struct options
    #[derive(FromArgs, PartialEq, Debug)]
    #[argh(subcommand)]
    enum SubChoices {
        Child(ChildArgs),
    }

    /// One (the only) possible subcommmand
    #[derive(FromArgs, PartialEq, Debug)]
    #[argh(subcommand, name = "child")]
    struct ChildArgs {
        /// argh requires doc commands on everything
        #[argh(option)]
        option: u32,
    }

    #[test]
    fn args_work() -> Result<(), Error> {
        let arg_strings = vec![
            "program_name".to_string(),
            "child".to_string(),
            "--option".to_string(),
            "42".to_string(),
        ];
        let args = load_from_strings::<OuterArgs>(arg_strings)?;
        let SubChoices::Child(child_args) = args.sub;
        assert_eq!(child_args.option, 42);
        Ok(())
    }
}
