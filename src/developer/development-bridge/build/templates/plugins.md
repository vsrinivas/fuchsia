pub async fn plugins(
  remote_proxy: fidl_fuchsia_developer_remotecontrol::RemoteControlProxy, 
  subcommand: ffx_command::Subcommand,
) -> Result<(), anyhow::Error> {
  match subcommand {
{% for plugin in plugins %}
    ffx_command::Subcommand::{{plugin.enum}}(c) => {{plugin.lib}}::ffx_plugin_impl(remote_proxy, c).await,
{% endfor %}
    _ => Err(anyhow::anyhow!("No plugin integration found"))
  }
}

