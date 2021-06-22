  <tr class = "driver">
    <td><p>{{ epitaph.short_description }}<a name="{{ epitaph.short_description|replace(" ", "-")|replace("(", "")|replace(")", "")|lower() }}"></a></p><h3 class="add-link" style="display:none">{{ epitaph.short_description }}</h3></td>
    <td>
      <table class = "nested responsive">
        <colgroup>
        <col width="10%">
      </colgroup>
        <tbody class="list">
          <tr>
            <td>Status</td>
            <td>Deprecated</td>
          </tr>
          {% if epitaph.deletion_reason != '' %}
          <tr>
            <td>Deletion reason</td>
            <td>{{ epitaph.deletion_reason }}</td>
          </tr>
          {% endif %}
          {% if epitaph.gerrit_change_id != '' %}
          <tr>
            <td>Removed in CL</td>
            <td><a href="{{ gerrit_change_url }}{{ epitaph.gerrit_change_id }}">{{ epitaph.gerrit_change_id }}</a></td>
          </tr>
          {% endif %}
          {% if epitaph.availabe_in_git != [''] %}
          <tr>
            <td>Available in Fuchsia revision</td>
            <td><a href="{{ fuchsia_source_tree_change }}{{ epitaph.available_in_git }}">{{ epitaph.available_in_git }}</a></td>
          </tr>
          {% endif %}
          {% if epitaph.path != '' %}
          <tr>
            <td>Path</td>
          {% if epitaph.path|first in 's' %}
            <td><a href="{{ cs_url }}{{ epitaph.path }}"><code>//{{ epitaph.path }}</code></a></td>
          {% elif epitaph.path[0] == '/' and epitaph.path[1] != '/' %}
            <td><a href="{{ cs_url }}{{ epitaph.path }}"><code>/{{ epitaph.path }}</code></a></td>
          {% else %}
            <td><a href="{{ cs_url }}{{ epitaph.path }}"><code>{{ epitaph.path }}</code></a></td>
          {% endif %}
          </tr>
          {% endif %}
        </tbody>
      </table>
    </td>
  </tr>