{# This file is used to define the objects and css style for driver pages #}
{% set gerrit_profile = "https://fuchsia-review.googlesource.com/q/owner:" %}
{% set gerrit_change_url = "https://fuchsia-review.googlesource.com/c/fuchsia/+/" %}
{% set fuchsia_source_tree = "https://fuchsia.googlesource.com/fuchsia/+/master/" %}
{% set fuchsia_editor = "https://ci.android.com/edit?repo=fuchsia/fuchsia/master&file=" %}
{% set issue_url = "https://fxbug.dev/" %}
{% set cs_url = "https://cs.opensource.google/fuchsia/fuchsia/+/master:" %}
{% set fuchsia_source_tree_change = "https://fuchsia.googlesource.com/fuchsia/+/" %}
{% set drivers_dir = "docs/reference/hardware/" %}
{% set drivers_metadata_file = "_drivers.yaml" %}
{% set areas_yaml_file = "_drivers_areas.yaml" %}

{% set drivers | yamlloads %}
{% include "docs/reference/hardware/_drivers.yaml" %}
{% endset %}

{% set areas | yamlloads %}
{% include "docs/reference/hardware/_drivers_areas.yaml" %}
{% endset %}

{% comment %}
{% set epitaphs | yamlloads %}
{% include "docs/reference/hardware/_epitaphs.yaml" %}
{% endset %}
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