  <tr class = "driver">
    <td><p>{{ driver.short_description }}<a name="{{ driver.short_description|replace(" ", "-")|replace("(", "")|replace(")", "")|lower() }}"></a></p><h3 class="add-link" style="display:none">{{ driver.short_description }}</h3></td>
    <td>
      <table class = "nested responsive">
        <colgroup>
        <col width="10%">
      </colgroup>
        <tbody class="list">
          {% if driver.vendor != [''] %}
          <tr>
            <td>Vendor</td>
            <td>
              <ul class="comma-list">
                {% for vend in driver.vendor %}
                <li>{{ vend|capitalize() }}</li>
                {% endfor %}
              </ul>
            </td>
          </tr>
          {% endif %}
          {% if driver.families != [''] %}
          <tr>
            <td>Family</td>
            <td>
              <ul class="comma-list">
                {% for fam in driver.families %}
                <li>{{ fam|capitalize() }}</li>
                {% endfor %}
              </ul>
            </td>
          </tr>
          {% endif %}
          {% if driver.areas != [''] %}
          <tr>
            <td>Area</td>
            <td>
              <ul class="comma-list">
                {% for area in driver.areas %}
                <li>{{ area }}</li>
                {% endfor %}
              </ul>
            </td>
          </tr>
          {% endif %}
          {% if driver.platforms != [''] %}
          <tr>
            <td>Platform</td>
            <td>
              <ul class="comma-list">
                {% for plat in driver.platforms %}
                <li>{{ plat|capitalize() }}</li>
                {% endfor %}
              </ul>
            </td>
          </tr>
          {% endif %}
          {% if driver.path != '' %}
          <tr>
            <td>Path</td>
          {% if driver.path|first in 's' %}
            <td><a href="{{ cs_url }}{{ driver.path }}"><code>//{{ driver.path }}</code></a></td>
          {% elif driver.path[0] == '/' and driver.path[1] != '/' %}
            <td><a href="{{ cs_url }}{{ driver.path }}"><code>/{{ driver.path }}</code></a></td>
          {% else %}
            <td><a href="{{ cs_url }}{{ driver.path }}"><code>{{ driver.path }}</code></a></td>
          {% endif %}
          </tr>
          {% endif %}
        </tbody>
      </table>
    </td>
  </tr>