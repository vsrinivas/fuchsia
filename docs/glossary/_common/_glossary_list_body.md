<li>
  {{ pencil_edit }}
  <h3 class="add-link">{{ item.term }}</h3>
    {% if item.short_description != '' %}
    <p>{{ item.short_description }}</p>
      {% if item.full_description != '' %}
      <devsite-expandable>
        <a href="#{{ item.term }}-full" class="expand-control once">Full description</a>
        <hr>
        <p>{{ item.full_description }}</p>
      </devsite-expandable>
      {% endif %}
    {% else %}
      {% if item.full_description != '' %}
         {{ item.full_description }}
      {% endif %}
    {% endif %}
  {% if item.see_also != [''] %}
  <devsite-expandable>
    <a href="#{{ item.term }}-also" class="expand-control once">See also</a>
    <hr>
    <h4>Information related to {{ item.term }}</h4>
      <ul class="comma-list">
      {% for see in item.see_also %}
      <li>{{ see }}</li>
      {% endfor %}
    </ul>
  </devsite-expandable>
  {% endif %}
  {% if item.related_guides != [''] %}
  <devsite-expandable>
    <a href="#{{ item.term }}-also" class="expand-control once">Related guides</a>
    <hr>
    <h4>Guides related to {{ item.term }}</h4>
      <ul class="comma-list">
      {% for guide in item.related_guides %}
      <li>{{ guide }}</li>
      {% endfor %}
    </ul>
  </devsite-expandable>
  {% endif %}
  {% if item.area != [''] %}
  <!--
    <ul class="comma-list">
      {% for area in item.area %}
      <li>{{ area }}</li>
      {% endfor %}
    </ul>
  -->
  {% endif %}
<hr>
</li>

