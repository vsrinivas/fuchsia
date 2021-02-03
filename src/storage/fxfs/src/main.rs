use {
    anyhow::{format_err, Error},
    argh::FromArgs,
    fuchsia_async as fasync,
    fuchsia_runtime::HandleType,
    fuchsia_zircon as zx,
    fxfs::{mkfs, mount},
    remote_block_device::RemoteBlockDevice,
};

#[derive(FromArgs, PartialEq, Debug)]
/// fxfs
struct TopLevel {
    #[argh(subcommand)]
    nested: SubCommand,
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand)]
enum SubCommand {
    Format(FormatSubCommand),
    Mount(MountSubCommand),
}

#[derive(FromArgs, PartialEq, Debug)]
/// Format
#[argh(subcommand, name = "mkfs")]
struct FormatSubCommand {}

#[derive(FromArgs, PartialEq, Debug)]
/// Mount
#[argh(subcommand, name = "mount")]
struct MountSubCommand {}

#[fasync::run(10)]
async fn main() -> Result<(), Error> {
    let args: TopLevel = argh::from_env();

    // TODO: Does this need to be boxed and do we need Cache?
    // Open the remote block device.
    let cache = remote_block_device::Cache::new(RemoteBlockDevice::new_sync(zx::Channel::from(
        fuchsia_runtime::take_startup_handle(fuchsia_runtime::HandleInfo::new(
            HandleType::User0,
            1,
        ))
        .ok_or(format_err!("Missing device handle"))?,
    ))?)?;

    match args {
        TopLevel { nested: SubCommand::Format(_) } => mkfs::mkfs(cache),
        TopLevel { nested: SubCommand::Mount(_) } => mount::mount(cache),
    }
}
