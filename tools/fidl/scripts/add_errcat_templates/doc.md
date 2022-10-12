## fi-$num: <!-- TODO: Add title --> {#fi-$num}

<!-- TODO: 1 or 2 lines explaining what the causes this kind of failure -->:

{% include "docs/reference/fidl/language/error-catalog/label/_bad.md" %}

```fidl
{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="tools/fidl/fidlc/tests/fidl/bad/fi-${num}.test.fidl" exclude_regexp="\/\/ (Copyright 20|Use of|found in).*" %}
```

<!-- TODO: 1 or 2 lines explaining how to fix this in the general case -->:

{% include "docs/reference/fidl/language/error-catalog/label/_good.md" %}

```fidl
{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="tools/fidl/fidlc/tests/fidl/good/fi-${num}.test.fidl" exclude_regexp="\/\/ (Copyright 20|Use of|found in).*" %}
```

<!-- TODO(RECOMMENDED): 1 paragraph summarizing strategies to avoid problem -->

<!-- TODO(OPTIONAL): 1 explaining why this is an error in the first place -->
