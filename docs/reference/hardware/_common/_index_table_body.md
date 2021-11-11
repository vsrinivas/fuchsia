  <tr class = "driver">
    {%- if driver.short_description != '' %}
    <td><p>{{ driver.short_description }}<a name="{{ driver.short_description|replace(" ", "-")|replace("(", "")|replace(")", "")|lower() }}"></a></p><h3 class="add-link" style="display:none">{{ driver.short_description }}</h3></td>
    {%- elif driver.path != '' %}
      {%- for index in range(driver.path|length - 1) -%}
        {% if driver.path[index] == '/' %}
          {% set pathmark = index %}
        {% endif %}
      {%- endfor -%}
      {% set drivername = [] %}
      {%- for index in range(pathmark + 1, driver.path|length) -%}
      {% do drivername.append(driver.path[index]) %}
      {%- endfor -%}
      <td><p>{{drivername|join|capitalize}}<a name="{{ drivername|join|replace(" ", "-")|replace("(", "")|replace(")", "")|lower() }}"></a></p><h3 class="add-link" style="display:none">{{ drivername|join|capitalize }}</h3></td>
    {% else %}
    <td><p>No short description</p></td>
    {% endif %}
    <td>
      <table class = "nested responsive">
        <colgroup>
        <col width="10%">
      </colgroup>
        <tbody class="list">
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