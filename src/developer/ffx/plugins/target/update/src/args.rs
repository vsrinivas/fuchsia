use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "update", description = "Update base system software on target device")]
pub struct UpdateCommand {}
