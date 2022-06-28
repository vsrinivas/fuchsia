#[derive(argh::FromArgs, Debug, PartialEq)]
#[argh(subcommand)]
pub enum SubCommand {
{% for dep in deps %}
  {{dep.enum}}({{dep.lib}}::FfxPluginCommand),
{% endfor %}
}
