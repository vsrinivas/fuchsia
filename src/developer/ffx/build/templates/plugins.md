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

pub fn ffx_plugin_writer_all_output(_level: usize) {
{% if includes_execution %}
{% if includes_subcommands %}
{% for plugin in plugins %}
  println!("{:level$}- {{plugin.lib}}", "", level=_level);
  {{plugin.lib}}_suite::ffx_plugin_writer_all_output(_level + 2);
{% endfor %}
{% else %}
  println!("{:level$}- {}", "", {{execution_lib}}::ffx_plugin_writer_output(), level=_level);
{% endif %}
{% else %}
{% if includes_subcommands %}
{% for plugin in plugins %}
  println!("{:level$}- {{plugin.lib}}", "", level=_level);
  {{plugin.lib}}_suite::ffx_plugin_writer_all_output(_level + 2);
{% endfor %}
{% endif %}
{% endif %}
}

pub fn ffx_plugin_is_machine_supported(
{% if not includes_execution and not includes_subcommands %}
  _cmd: &{{suite_args_lib}}::FfxPluginCommand,
{% else %}
  cmd: &{{suite_args_lib}}::FfxPluginCommand,
{% endif %}
) -> bool {
{% if includes_execution %}
{% if includes_subcommands %}
  match &cmd.subcommand {
      Some(sub) => match sub {
{% for plugin in plugins %}
        {{suite_subcommand_lib}}::Subcommand::{{plugin.enum}}(c) => {{plugin.lib}}_suite::ffx_plugin_is_machine_supported(c),
{% endfor %}
      },
      None => {{execution_lib}}::ffx_plugin_is_machine_supported(cmd)
    }
{% else %}
  {{execution_lib}}::ffx_plugin_is_machine_supported(cmd)
{% endif %}

{% else %}
{% if includes_subcommands %}
    match &cmd.subcommand {
{% for plugin in plugins %}
      {{suite_subcommand_lib}}::Subcommand::{{plugin.enum}}(c) => {{plugin.lib}}_suite::ffx_plugin_is_machine_supported(c),
{% endfor %}
    }
{% else %}
    false
{% endif %}
{% endif %}
}
