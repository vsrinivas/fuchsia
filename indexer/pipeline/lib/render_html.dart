// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:mustache/mustache.dart';

import 'index.dart';

final String htmlIndexTemplate = '''
<html>
<head>
  <title>Modular Index</title>
  <meta name=viewport content="width=device-width,initial-scale=1">
</head>
<body>
  <h1>Modular Index</h1>

  <h2>Launcher</h2>
  <ul>
  <li><a href="mojo://tq.mojoapps.io/handler.mojo?root_session=true">
    User shell
  </a></li>
  </ul>

  <h2>Recipes</h2>
  <ul>
  {{#recipes}}
    {{#makeLink}}
    <li><a href="{{& url}}">{{name}}</a></li>
    {{/makeLink}}

    {{^makeLink}}
    <li>{{name}}</li>
    {{/makeLink}}
  {{/recipes}}
  </ul>

  <h2>Labels</h2>
  <p>Labels in individual sections are ordered by different manifests that
  reference them.</p>

  <h3>Verbs</h3>
  <ul>
  {{#verbs}}
    <li>
      <a href="{{& uri}}">{{& uri}}</a> ({{referenceCount}})<br/>
      shorthands: {{shorthands}}
    </li>
  {{/verbs}}
  </ul>

  <h3>Semantic Labels</h3>
  <ul>
  {{#semantic}}
    <li>
      <a href="{{& uri}}">{{& uri}}</a> ({{referenceCount}})<br/>
      shorthands: {{shorthands}}
    </li>
  {{/semantic}}
  </ul>

  <h3>Representation</h3>
  <ul>
  {{#representation}}
    <li>
      <a href="{{& uri}}">{{& uri}}</a> ({{referenceCount}})<br/>
      shorthands: {{shorthands}}
    </li>
  {{/representation}}
  </ul>

  <h3>Embodiments</h3>
  <ul>
  {{#embodiment}}
    <li>
      <a href="{{& uri}}">{{& uri}}</a> ({{referenceCount}})<br/>
      shorthands: {{shorthands}}
    </li>
  {{/embodiment}}
  </ul>
</body>
</html>
''';

String renderHtmlIndex(final Index index) {
  List<Map<String, dynamic>> recipes = [];
  for (RecipeEntry entry in index.recipes.values) {
    recipes.add(
        {'name': entry.name, 'url': entry.url, 'makeLink': entry.url != null});
  }

  recipes.sort(
      (final Map<String, dynamic> entry1, final Map<String, dynamic> entry2) =>
          entry1['name'].compareTo(entry2['name']));

  Map<String, dynamic> params = {
    'verbs': index.verbRanking,
    'semantic': index.semanticRanking,
    'representation': index.representationRanking,
    'embodiment': index.embodimentRanking,
    'recipes': recipes,
  };

  return new Template(htmlIndexTemplate).renderString(params);
}
