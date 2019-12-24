use {
    anyhow::Error, fuchsia_async as fasync, fuchsia_syslog as syslog, setui_client_lib::*,
    structopt::StructOpt,
};

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    syslog::init_with_tags(&["setui-client"]).expect("Can't init logger");

    let command = SettingClient::from_args();

    run_command(command).await?;

    Ok(())
}
