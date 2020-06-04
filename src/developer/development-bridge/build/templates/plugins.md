pub async fn plugins<C: ffx_core::RemoteControlProxySource + ffx_core::DaemonProxySource>(
  cli: &C,
  subcommand: ffx_command::Subcommand,
) -> Result<(), anyhow::Error> {
  match subcommand {
{% for plugin in plugins %}
    ffx_command::Subcommand::{{plugin.enum}}(c) => {{plugin.lib}}::ffx_plugin_impl(cli, c).await,
{% endfor %}
    _ => Err(anyhow::anyhow!("No plugin integration found"))
  }
}

