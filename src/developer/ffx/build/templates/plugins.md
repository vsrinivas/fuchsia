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
{% if includes_execution == "true" %}
{% if includes_subcommands == "true" %}
  match cmd.subcommand {
      Some(sub) => match sub {
{% for plugin in plugins %}
        {{suite_subcommand_lib}}::Subcommand::{{plugin.enum}}(c) => {{plugin.lib}}_suite::ffx_plugin_impl(daemon_factory, remote_factory, c).await,
{% endfor %}
      },
      None => {{execution_lib}}::ffx_plugin_impl(daemon_factory, remote_factory, cmd).await
    }
{% else %}
  {{execution_lib}}::ffx_plugin_impl(daemon_factory, remote_factory, cmd).await
{% endif %}

{% else %}
    match cmd.subcommand {
{% for plugin in plugins %}
      {{suite_subcommand_lib}}::Subcommand::{{plugin.enum}}(c) => {{plugin.lib}}_suite::ffx_plugin_impl(daemon_factory, remote_factory, c).await,
{% endfor %}
    }
{% endif %}
}
