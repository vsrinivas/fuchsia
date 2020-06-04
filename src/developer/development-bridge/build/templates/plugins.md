pub async fn plugins<D, DFut, R, RFut>(
  daemon_factory: D, 
  remote_factory: R,
  subcommand: ffx_command::Subcommand,
) -> Result<(), anyhow::Error> 
    where 
        D: FnOnce() -> DFut,
        DFut: std::future::Future<Output = std::result::Result<fidl_fuchsia_developer_bridge::DaemonProxy, anyhow::Error>>,
        R: FnOnce() -> RFut,
        RFut: std::future::Future<Output = std::result::Result<fidl_fuchsia_developer_remotecontrol::RemoteControlProxy, anyhow::Error>>,
{
  match subcommand {
{% for plugin in plugins %}
    ffx_command::Subcommand::{{plugin.enum}}(c) => {{plugin.lib}}::ffx_plugin_impl(daemon_factory, remote_factory, c).await,
{% endfor %}
    _ => Err(anyhow::anyhow!("No plugin integration found"))
  }
}

