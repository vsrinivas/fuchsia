use {
    fxfs::mkfs,
    fuchsia_async as fasync,
    anyhow::{Error, format_err},
    argh::FromArgs,
    fuchsia_runtime::HandleType,
    remote_block_device::RemoteBlockDevice,
    fuchsia_zircon as zx,
};

#[derive(FromArgs, PartialEq, Debug)]
/// fxfs
struct TopLevel {
    #[argh(subcommand)]
    nested: SubCommand
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand)]
enum SubCommand {
    Format(FormatSubCommand),
}

#[derive(FromArgs, PartialEq, Debug)]
/// Format
#[argh(subcommand, name = "format")]
struct FormatSubCommand {
}

#[fasync::run(10)]
async fn main() -> Result<(), Error> {
    let args: TopLevel = argh::from_env();

    // TODO: Does this need to be boxed and do we need Cache?
    // Open the remote block device.
    let device = Box::new(remote_block_device::Cache::new(RemoteBlockDevice::new_sync(
        zx::Channel::from(
            fuchsia_runtime::take_startup_handle(fuchsia_runtime::HandleInfo::new(
                HandleType::User0,
                1,
            ))
            .ok_or(format_err!("Missing device handle"))?,
        ),
    )?)?);

    match args {
        TopLevel{ nested: SubCommand::Format(_) } => mkfs::mkfs(device),
    }
}
