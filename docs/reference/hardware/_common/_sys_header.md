{# This file is used to define the objects and css style for driver pages #}
{%- set gerrit_profile = "https://fuchsia-review.googlesource.com/q/owner:" %}
{%- set gerrit_change_url = "https://fuchsia-review.googlesource.com/c/fuchsia/+/" %}
{%- set fuchsia_source_tree = "https://fuchsia.googlesource.com/fuchsia/+/main/" %}
{%- set fuchsia_editor = "https://ci.android.com/edit?repo=fuchsia/fuchsia/main&file=" %}
{%- set issue_url = "https://fxbug.dev/" %}
{%- set cs_url = "https://cs.opensource.google/fuchsia/fuchsia/+/main:" %}
{%- set fuchsia_source_tree_change = "https://fuchsia.googlesource.com/fuchsia/+/" %}
{%- set system_config_dir = "docs/reference/hardware/" %}
{%- set system_config_yaml_file = "_supported_sys_config.yaml" %}
{%- set architecture_yaml_file = "_supported_cpu_architecture.yaml" %}

{%- set sysconfig | yamlloads %}
{% include "docs/reference/hardware/_supported_sys_config.yaml" %}
{%- endset %}

{%- set architecture | yamlloads %}
{% include "docs/reference/hardware/_supported_cpu_architecture.yaml" %}
{%- endset %}

{% comment %}
{%- set epitaphs | yamlloads %}
{% include "docs/reference/hardware/_supported_sys_config_epitaphs.yaml" %}
{%- endset %}
{% endcomment %}

<style>
.comma-list {
  display: inline;
  list-style: none;
  padding: 0px;
}

.comma-list li {
  display: inline;
}

.comma-list li::after {
  content: ", ";
}

.comma-list li:last-child::after {
    content: "";
}

table {
  text-overflow: ellipsis;
}


.checkbox-div {
  display:inline-block;
  padding-top: 3px;
  padding-right: 2px;
  padding-bottom: 3px;
  padding-left: 2px;
}

.checkbox-div input+label {
  font-size: 80%;
}

.form-checkbox button {
  font-size: 80%;
}

.col-key {
  width:1px;white-space:nowrap;
}

.note {

}
.edit-buttons {
  display:inline-block;
  width:100%;
}

.edit-buttons-left {
  float: left;
  margin-left: 20%;
}

.edit-buttons-right {
  float: right;
  margin-right: 20%;
}

.see-rfcs {
  display:inline-block;
  width:100%;
}

</style>