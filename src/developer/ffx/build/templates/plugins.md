pub async fn ffx_plugin_impl<I: ffx_core::Injector>(
{% if not includes_execution and not includes_subcommands %}
  _injector: I,
  _cmd: {{suite_args_lib}}::FfxPluginCommand,
{% else %}
  injector: I,
  cmd: {{suite_args_lib}}::FfxPluginCommand,
{% endif %}
) -> Result<(), anyhow::Error>
{
{% if includes_execution %}
{% if includes_subcommands %}
  match cmd.subcommand {
      Some(sub) => match sub {
{% for plugin in plugins %}
        {{suite_subcommand_lib}}::Subcommand::{{plugin.enum}}(c) => {{plugin.lib}}_suite::ffx_plugin_impl(injector, c).await,
{% endfor %}
      },
      None => {{execution_lib}}::ffx_plugin_impl(injector, cmd).await
    }
{% else %}
  {{execution_lib}}::ffx_plugin_impl(injector, cmd).await
{% endif %}

{% else %}
{% if includes_subcommands %}
    match cmd.subcommand {
{% for plugin in plugins %}
      {{suite_subcommand_lib}}::Subcommand::{{plugin.enum}}(c) => {{plugin.lib}}_suite::ffx_plugin_impl(injector, c).await,
{% endfor %}
    }
{% else %}
    println!("Subcommand not implemented yet.");
    Ok(())
{% endif %}
{% endif %}
}
