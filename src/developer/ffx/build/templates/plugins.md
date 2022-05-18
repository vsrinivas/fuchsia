pub async fn ffx_plugin_impl(
{% if not includes_execution and not includes_subcommands %}
  _injector: &dyn ffx_core::Injector,
  _cmd: {{suite_args_lib}}::FfxPluginCommand,
{% else %}
  injector: &dyn ffx_core::Injector,
  cmd: {{suite_args_lib}}::FfxPluginCommand,
{% endif %}
) -> anyhow::Result<()>
{
{% if includes_execution %}
{% if includes_subcommands %}
  match cmd.subcommand {
      Some(sub) => match sub {
{% for plugin in plugins %}
        {{suite_subcommand_lib}}::Subcommand::{{plugin.enum}}(c) => {{plugin.lib}}_suite::ffx_plugin_impl(injector, c).await,
{% endfor %}
      },
      None => {{execution_lib}}::ffx_plugin_impl(injector, cmd).await,
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
{% if not includes_subcommands %}
  _cmd: &{{suite_args_lib}}::FfxPluginCommand,
{% else %}
  cmd: &{{suite_args_lib}}::FfxPluginCommand,
{% endif%}
) -> bool {
{% if includes_execution %}
{% if includes_subcommands %}
  match &cmd.subcommand {
      Some(sub) => match sub {
{% for plugin in plugins %}
        {{suite_subcommand_lib}}::Subcommand::{{plugin.enum}}(c) => {{plugin.lib}}_suite::ffx_plugin_is_machine_supported(c),
{% endfor %}
      },
      None => {{execution_lib}}::ffx_plugin_is_machine_supported()
    }
{% else %}
  {{execution_lib}}::ffx_plugin_is_machine_supported()
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
