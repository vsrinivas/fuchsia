  <tr class = "driver">
    {%- if driver.name %}
    <td><p>{{ driver.name }}<a name="{{ driver.name|replace(" ", "-")|replace("(", "")|replace(")", "")|lower() }}"></a></p><h3 class="add-link" style="display:none">{{ driver.name }}</h3></td>
    {% endif %}
    <td>
      <table class = "nested responsive">
        <colgroup>
        <col width="10%">
      </colgroup>
        <tbody class="list">
          {%- if driver.short_description %}
          <tr>
            <td>Description</td>
            <td>{{ driver.short_description }}</td>
          </tr>
          {%- endif %}
          {%- if driver.manufacturer %}
          <tr>
            <td>Manufacturer</td>
            <td>{{ driver.manufacturer |capitalize() }}</td>
          </tr>
          {%- endif %}
          {%- if driver.families %}
          <tr>
            <td>Families</td>
            <td>
              <ul class="comma-list">
                {%- for fam in driver.families %}
                <li>{{ fam|capitalize() }}</li>
                {%- endfor %}
              </ul>
            </td>
          </tr>
          {%- endif %}
          {%- if driver.areas %}
          <tr>
            <td>Areas</td>
            <td>
              <ul class="comma-list">
                {%- for area in driver.areas %}
                <li>{{ area }}</li>
                {%- endfor %}
              </ul>
            </td>
          </tr>
          {%- endif %}
          {%- if driver.models %}
          <tr>
            <td>Models</td>
            <td>
              <ul class="comma-list">
                {%- for mod in driver.models %}
                <li>{{ mod|capitalize() }}</li>
                {%- endfor %}
              </ul>
            </td>
          </tr>
          {%- endif %}
          {%- if driver.path %}
          <tr>
            <td>Path</td>
          {%- if driver.path|first in 's' %}
            <td><a href="{{ cs_url }}{{ driver.path }}"><code>//{{ driver.path }}</code></a></td>
          {%- elif driver.path[0] == '/' and driver.path[1] != '/' %}
            <td><a href="{{ cs_url }}{{ driver.path }}"><code>/{{ driver.path }}</code></a></td>
          {%- else %}
            <td><a href="{{ cs_url }}{{ driver.path }}"><code>{{ driver.path }}</code></a></td>
          {%- endif %}
          </tr>
          {%- endif %}
        </tbody>
      </table>
    </td>
  </tr>