pub async fn ffx_plugin_impl<D, DFut, R, RFut>(
  daemon_factory: D,
  remote_factory: R,
  cmd: {{suite_args_lib}}::FfxPluginCommand,
) -> Result<(), anyhow::Error>
    where
    D: FnOnce() -> DFut,
    DFut: std::future::Future<
        Output = std::result::Result<fidl_fuchsia_developer_bridge::DaemonProxy, anyhow::Error>,
    >,
    R: FnOnce() -> RFut,
    RFut: std::future::Future<
        Output = std::result::Result<
            fidl_fuchsia_developer_remotecontrol::RemoteControlProxy,
            anyhow::Error,
        >,
    >,
{
    match cmd.subcommand {
{% for plugin in plugins %}
      {{suite_subcommand_lib}}::Subcommand::{{plugin.enum}}(c) => {{plugin.lib}}::ffx_plugin_impl(daemon_factory, remote_factory, c).await,
{% endfor %}
      {% if not_complete %}
      _ => Err(anyhow::anyhow!("No plugin integration found")),
      {% endif %}
    }
}
