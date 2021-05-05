pub async fn ffx_plugin_impl<I: ffx_core::Injector>(
{% if not includes_execution and not includes_subcommands %}
  _injector: I,
  _cmd: {{suite_args_lib}}::FfxPluginCommand,
{% else %}
  injector: I,
  cmd: {{suite_args_lib}}::FfxPluginCommand,
{% endif %}
) -> anyhow::Result<i32>
{
{% if includes_execution %}
{% if includes_subcommands %}
  match cmd.subcommand {
      Some(sub) => match sub {
{% for plugin in plugins %}
        {{suite_subcommand_lib}}::Subcommand::{{plugin.enum}}(c) => ffx_core::PluginResult::from({{plugin.lib}}_suite::ffx_plugin_impl(injector, c).await).into(),
{% endfor %}
      },
      None => ffx_core::PluginResult::from({{execution_lib}}::ffx_plugin_impl(injector, cmd).await).into(),
    }
{% else %}
  ffx_core::PluginResult::from({{execution_lib}}::ffx_plugin_impl(injector, cmd).await).into()
{% endif %}

{% else %}
{% if includes_subcommands %}
    match cmd.subcommand {
{% for plugin in plugins %}
      {{suite_subcommand_lib}}::Subcommand::{{plugin.enum}}(c) => ffx_core::PluginResult::from({{plugin.lib}}_suite::ffx_plugin_impl(injector, c).await).into(),
{% endfor %}
    }
{% else %}
    println!("Subcommand not implemented yet.");
    Ok(0)
{% endif %}
{% endif %}
}
