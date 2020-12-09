  <tr>
    <td><p>{{ rfc.name }}</p><h3 class="add-link" style="display:none">{{ rfc.name }} - {{ rfc.title }}</h3></td>
    <td>
        <p>
          <a href="{{ rfc.file }}">{{ rfc.title }}</a>
        </p>
      </td>
      <td>
        <ul class="comma-list">
        {% for area in rfc.area %}
          <li>{{ area }}</li>
        {% endfor %}
        </ul>
      <td>
        <ul class="comma-list">
        {% for change in rfc.gerrit_change_id %}
          <li><a href="{{ gerrit_change_url }}{{ change }}">{{ change }}</a></li>
        {% endfor %}
        </ul>
    </td>
  </tr>
