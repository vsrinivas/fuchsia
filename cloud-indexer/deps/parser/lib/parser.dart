// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:developer';
import 'dart:io';

import 'package:modular_core/entity/schema.dart';
import 'package:path/path.dart' as path;
import 'package:yaml/yaml.dart';

import 'cardinality.dart';
import 'expression.dart';
import 'expression_parser.dart' as expr;
import 'manifest.dart';
import 'parse_error.dart';
import 'recipe.dart';

// The parser in this file relies on two existing parsers, and on
// design choices in the syntax, to simplify the task of parsing.
//
// First, it relies on the yaml package's parser to split lines. Every
// entity parsed in this file is either a |String| which corresponds
// to a one line field in yaml, or a |List| or |YamlMap| ultimately
// composed of such strings. For example, in
//
//   input: foo -> bar
//
// the input parser will receive the string 'foo -> bar', while in
//
//   input:
//   - foo -> bar
//   - bar -> baz
//
// that parser will receive the list ['foo -> bar', 'bar -> baz'].
//
// Furthermore, this parser relies on |Uri.parse| to determine if a
// string is a valid URI. Specifically, any line successfully parsed
// by |Uri.parse| into a URI with a scheme is assumed to be a valid
// URI, and anything else in place where a URI is expected is assumed
// to be a shorthand ID declared in the use: section.
//
// Expressions in the fields of yaml structures are parsed by a
// recursive descent parser on top of a scanner in
// expression_parser.dart. The expression grammar is specified in
// expression_parser.ebnf.
//
// Parsing functions that start with |tryParse| return null on
// failure. Parsing functions that start with |parse| throw a
// |ParseError| on failure.

// RegExp for parsing the `arch` and `modularRevision` fields.
final RegExp _archRegExp = new RegExp(r'^[a-zA-Z0-9]+-[a-zA-Z0-9]+$');
final RegExp _modularRevisionRegExp = new RegExp(r'[a-zA-Z0-9]{40}');

/// Tries to parse the given text as a URI. If it succeeds, returns the URI as a
/// Uri instance. If it fails as indicated by a FormatException, returns null.
Uri tryParseUri(final String text) {
  try {
    final Uri uri = Uri.parse(text);
    if (uri.hasScheme) {
      return uri;
    }
    return null;
  } on FormatException {
    return null;
  }
}

/// Parses one line in a use section, like "foo: https://tq.io/bar".
void parseUseShorthand(final ParserState parserState, final YamlMap map) {
  if (map is! YamlMap) {
    return;
  }

  if (map.length != 1) {
    throw new ParseError.atNode(map, 'Invalid use: syntax');
  }

  final key = map.keys.single;
  if (key is! String) {
    throw new ParseError.atNode(map, 'Invalid use: key');
  }

  if (parserState.shorthand.containsKey(key)) {
    throw new ParseError.atNode(
        map,
        'Invalid use definition: $key already defined at'
        ' ${parserState.shorthandLocation[key]}');
  }

  final value = map.values.single;
  if (value is! String) {
    throw new ParseError.atNode(map, 'Invalid use: value');
  }

  final Uri uri = tryParseUri(value);
  if (uri == null) {
    throw new ParseError.atNode(map, 'Could not parse uri: $value');
  }

  if (parserState.shorthand.values
      .any((final Label label) => label.uri == uri)) {
    final String key = parserState.shorthand.values
        .firstWhere((Label label) => label.uri == uri)
        .shorthand;
    final SourceLocation location = parserState.shorthandLocation[key];
    throw new ParseError.atNode(map,
        'Invalid use mapping: shorthand for $uri already defined at $location to be $key');
  }

  parserState.addShorthand(key, uri, new SourceLocation.yaml(map));
}

PatternExpr parseInputExpr(final ParserState parserState, final YamlNode node) {
  if (node.value is! String) {
    throw new ParseError.atNode(node, 'Invalid expression: $node');
  }

  final scanner =
      new expr.Scanner(parserState, new SourceLocation.yaml(node), node.value);
  final ret = expr.parsePattern(scanner);
  scanner.checkDone();
  return ret;
}

List<PatternExpr> parseInput(
    final ParserState parserState, final YamlNode node) {
  if (node == null) {
    return null;
  }

  if (node is YamlList) {
    return new List<PatternExpr>.from(
        node.nodes.map((YamlNode item) => parseInputExpr(parserState, item)));
  } else if (node.value is String) {
    return <PatternExpr>[parseInputExpr(parserState, node)];
  }

  throw new ParseError.atNode(node, 'Invalid input: $node');
}

PatternExpr parseOutputExpr(
    final ParserState parserState, final YamlNode node) {
  if (node.value is! String) {
    throw new ParseError.atNode(node, 'Invalid expression $node');
  }

  final scanner =
      new expr.Scanner(parserState, new SourceLocation.yaml(node), node.value);
  final parsed = expr.parsePattern(scanner);
  scanner.checkDone();
  _checkOutput(parsed);

  return parsed;
}

void _checkOutput(final PatternExpr expr) {
  if (expr.property.cardinality != Cardinality.singular) {
    print('Ignoring output cardinality: $expr');
    expr.property.cardinality = Cardinality.singular;
  }
  expr.children.forEach(_checkOutput);
}

List<PatternExpr> parseOutput(
    final ParserState parserState, final YamlNode node) {
  if (node == null) {
    return null;
  }

  if (node is YamlList) {
    return new List<PatternExpr>.from(
        node.nodes.map((YamlNode item) => parseOutputExpr(parserState, item)));
  } else if (node.value is String) {
    return <PatternExpr>[parseOutputExpr(parserState, node)];
  }

  throw new ParseError.atNode(node, 'Invalid output: $node');
}

PatternExpr parseScope(final ParserState parserState, final YamlNode node) {
  if (node == null) {
    return null;
  }

  if (node.value is! String) {
    throw new ParseError.atNode(node, 'Invalid scope item: $node');
  }

  final scanner =
      new expr.Scanner(parserState, new SourceLocation.yaml(node), node.value);
  final parsed = expr.parsePattern(scanner);
  scanner.checkDone();
  _checkScope(node, parsed);

  return parsed;
}

void _checkScope(final YamlNode location, final PatternExpr expr) {
  if (expr.children.length > 1) {
    throw new ParseError.atNode(location,
        'Scope expression must not have multiple expected properties: expr');
  }

  if (expr.property.cardinality != Cardinality.singular) {
    throw new ParseError.atNode(
        location, 'Scope expression must be singular: $expr');
  }

  expr.children.forEach((PatternExpr expr) => _checkScope(location, expr));
}

/// Helper function to flatten the list of path expressions.
List<PathExpr> _flattenPatternExprList(final List<PatternExpr> exprs) {
  final ret = <PathExpr>[];
  if (exprs != null) {
    for (final PatternExpr i in exprs) {
      ret.addAll(i.flatten());
    }
  }
  return ret;
}

Step parseStep(final ParserState parserState, final YamlNode node) {
  if (node is! YamlMap) {
    throw new ParseError.atNode(node, 'Invalid recipe: step');
  }

  final yaml = (node as YamlMap);

  const List<String> allowedKeys = const [
    'scope',
    'verb',
    'input',
    'output',
    'display',
    'compose',
    'url',
  ];

  for (final key in yaml.keys) {
    if (!allowedKeys.contains(key)) {
      throw new ParseError.atNode(yaml.nodes[key], 'Invalid step keyword $key');
    }
  }

  final PatternExpr scope = parseScope(parserState, yaml.nodes['scope']);
  final Verb verb = parseVerb(parserState, yaml.nodes['verb']);
  final List<PathExpr> input =
      _flattenPatternExprList(parseInput(parserState, yaml.nodes['input']));
  final List<PathExpr> output =
      _flattenPatternExprList(parseOutput(parserState, yaml.nodes['output']));
  final List<PathExpr> display =
      _flattenPatternExprList(parseOutput(parserState, yaml.nodes['display']));
  final List<PathExpr> compose =
      _flattenPatternExprList(parseInput(parserState, yaml.nodes['compose']));
  final Uri url = parseUrl(yaml.nodes['url']);

  return new Step(scope != null ? scope.flatten().single : null, verb, input,
      output, display, compose, url);
}

String parseTitle(final YamlNode node) {
  if (node == null) {
    return null;
  }

  if (node.value is! String) {
    throw new ParseError.atNode(node, 'Body of title: field must be a string');
  }

  return node.value;
}

Verb parseVerb(final ParserState parserState, final YamlNode node) {
  if (node == null) {
    return null;
  }

  if (node.value is! String) {
    throw new ParseError.atNode(node, 'Body of verb: field must be a string');
  }

  final scanner =
      new expr.Scanner(parserState, new SourceLocation.yaml(node), node.value);
  final ret = new Verb(expr.parseLabel(scanner));
  scanner.checkDone();
  return ret;
}

Uri parseUrl(final YamlNode node) {
  if (node == null) {
    return null;
  }

  if (node.value is! String) {
    throw new ParseError.atNode(node, 'Body of url: field must be a string');
  }

  final Uri url = tryParseUri(node.value);
  if (url == null) {
    throw new ParseError.atNode(
        node, 'Body of url: field must be a valid URL.');
  }

  return url;
}

int parseThemeColor(final YamlNode node) {
  if (node == null) {
    return null;
  }

  if (node.value is! String) {
    throw new ParseError.atNode(
        node, 'Body of theme-color: field must be a string');
  }

  if (!node.value.startsWith('#') || node.value.length != 7) {
    throw new ParseError.atNode(
        node,
        'Body of theme-color: Expected value in #XXXXXX'
        ' format. Found: "${node.value}"');
  }

  try {
    return int.parse('0x${node.value.substring(1)}');
  } on FormatException {
    throw new ParseError.atNode(
        node,
        'Body of theme-color: "${node.value}" does not represent a valid '
        'hex color');
  }
}

/// Parses the import, use, and schema sections of a yaml manifest or recipe
/// file. This happens recursively for imported files, depth first for imports,
/// then use, then schema. The file references to the imported files are stored
/// in imports rather than reading the files here in order to keep the parsing
/// itself synchronous (opening a file is an asynchronous operation, so we do it
/// before the parser is called).
///
/// The parsed values of use and schema are stored in the ParserState instance
/// supplied. It's an error (for now) if a file overrides a use declaration of
/// an imported file with a new value (repeating the existing declaration is
/// ok). It's also an error (for now) if a file overrides a schema of an
/// imported file. Both may be relaxed in the future, once we see how this works
/// for actual manifests.
///
/// Schemas are only needed for manifests, not recipes. A recipe may not declare
/// a schema, but it may import a file that declares a schema, in which case the
/// schema is simply ignored.
void parseImportUseSchema(
    final ParserState parserState,
    final YamlNode importSection,
    final YamlNode useSection,
    final YamlNode schemaSection,
    final String filename,
    final Map<String, YamlMap> imports) {
  // parseImport recurses here.
  parseImport(parserState, importSection, filename, imports);
  parseUse(parserState, useSection, filename);
  parseSchemas(parserState, schemaSection);
}

/// Parses the import section. The parsed result affects the parser state. This
/// recurses to parseImportUseSchema() for each imported file, specifically
/// because use in the imported file must be parsed before schema in the
/// importing file.
void parseImport(final ParserState parserState, final YamlNode importSection,
    final String filename, final Map<String, YamlMap> imports) {
  if (importSection == null) {
    return;
  }

  if (importSection is! YamlList) {
    throw new ParseError.atNode(
        importSection, 'Invalid import body: ${importSection.value}');
  }

  for (final YamlNode importEntry in (importSection as YamlList).nodes) {
    final String importFilename = _resolveFilename(filename, importEntry.value);
    if (imports == null) {
      throw new ParseError.atNode(
          importEntry,
          'No import resolver list,'
          ' but found an import ${importEntry.value}');
    }

    if (!imports.containsKey(importFilename)) {
      throw new ParseError.atNode(
          importEntry, 'In $filename, unknown import file: $importFilename');
    }

    if (parserState.imported.contains(importFilename)) {
      // Ignore double include.
      return;
    }
    parserState.imported.add(importFilename);

    // We recurse into imports first, then parse use and schema. We have
    // to keep track of resolved imports, lest we have loops.
    final YamlMap imported = imports[importFilename];

    parseImportUseSchema(
        parserState,
        imported.nodes['import'],
        imported.nodes['use'],
        imported.nodes['schema'],
        importFilename,
        imports);
  }
}

/// Parses the use section. The parsed result affects the parser state.
void parseUse(final ParserState parserState, final YamlNode useSection,
    final String filename) {
  if (useSection == null) {
    return;
  }

  if (useSection is! YamlList) {
    throw new ParseError.atNode(
        useSection, 'Invalid use body: ${useSection.value}');
  }

  final Uses localUses = parseUses(useSection);
  localUses.shorthand.forEach((final String shorthand, final Label label) {
    if (parserState.shorthand.containsKey(shorthand)) {
      if (parserState.shorthand[shorthand] != label) {
        throw new ParseError.atNode(
            useSection,
            'Import $filename contains conflicting use entry $shorthand'
            ' here: $label; previously ${parserState.shorthand[shorthand]},'
            ' defined at ${parserState.shorthandLocation[shorthand]}');
      }
    } else {
      parserState.shorthand[shorthand] = label;
    }
  });
}

/// Parses the schema section. The parsed result affects the parser state.
void parseSchemas(final ParserState parserState, final YamlNode schemaSection) {
  if (schemaSection == null) {
    return;
  }

  if (schemaSection is! YamlList) {
    throw new ParseError.atNode(
        schemaSection, 'Invalid schema body: ${schemaSection.value}');
  }

  for (final YamlNode schemaEntry in (schemaSection as YamlList).nodes) {
    if (schemaEntry is! YamlMap) {
      throw new ParseError.atNode(
          schemaEntry, 'Invalid schema declaration: ${schemaEntry}');
    }
    // [YamlMap] implements the [Map] interface already, and we expect the
    // schema to be formatted identically to the JSON formatting expected
    // by [Schema].
    // TODO(thatguy): Add more validation to Schema parsing and report errors
    // here.
    final YamlMap schemaYamlMap = schemaEntry as YamlMap;
    final YamlNode typeNode = schemaYamlMap.nodes['type'];
    if (typeNode == null) {
      throw new ParseError.atNode(schemaEntry, 'Schema does not have a type.');
    }
    final scanner = new expr.Scanner(
        parserState, new SourceLocation.yaml(typeNode), typeNode.value);
    final Label typeLabel = expr.parseLabel(scanner);
    scanner.checkDone();

    // Re-write the map before parsing the Schema itself to include the full
    // URI type of the Schema, instead of the shorthand as given in the
    // Manifest.
    final Map<String, dynamic> schemaJsonMap =
        new Map<String, dynamic>.from(schemaYamlMap);
    schemaJsonMap['type'] = typeLabel.uri.toString();
    final Schema schema = new Schema.fromJson(schemaJsonMap);
    assert(typeLabel.uri.toString() == schema.type);

    if (parserState.schemas.containsKey(typeLabel)) {
      throw new ParseError.atNode(
          schemaEntry,
          'Re-definition of schema ${typeLabel}.'
          ' Previous definition: ${parserState.schemas[typeLabel]} at '
          '${parserState.schemaLocation[typeLabel]}');
    }

    parserState.schemas[typeLabel] = schema;
    parserState.schemaLocation[typeLabel] =
        new SourceLocation.yaml(schemaEntry);
  }
}

String parseArch(final YamlNode node) {
  if (node == null) {
    return null;
  }

  if (node.value is! String) {
    throw new ParseError.atNode(node, 'Body of arch: field must be a string');
  }

  if (!_archRegExp.hasMatch(node.value)) {
    throw new ParseError.atNode(
        node, 'Body of arch: not a valid arch string');
  }

  return node.value;
}

String parseModularRevision(final YamlNode node) {
  if (node == null) {
    return null;
  }

  if (node.value is! String) {
    throw new ParseError.atNode(
        node, 'Body of modularRevision: field must be a string');
  }

  if (!_modularRevisionRegExp.hasMatch(node.value)) {
    throw new ParseError.atNode(
        node, 'Body of modularRevision: invalid SHA1 hash');
  }

  return node.value;
}

Uses parseUses(final YamlNode node) {
  final ParserState localParserState = new ParserState();

  if (node == null) {
    return localParserState.uses;
  }

  if (node is! YamlList) {
    throw new ParseError.atNode(node, 'Body of use: field must be a list');
  }

  for (final YamlNode use in (node as YamlList).nodes) {
    parseUseShorthand(localParserState, use);
  }

  return localParserState.uses;
}

List<Step> parseRecipeSteps(
    final ParserState parserState, final YamlNode yaml) {
  if (yaml == null) {
    return null;
  }

  if (yaml is! YamlList) {
    throw new ParseError.atNode(yaml, 'Body of recipe: field must be a list');
  }

  return new List<Step>.from((yaml as YamlList)
      .nodes
      .map((YamlNode item) => parseStep(parserState, item)));
}

Uri parseTest(final ParserState parserState, final YamlNode node) {
  if (node == null) {
    return null;
  }

  if (node.value is String) {
    final Uri uri = tryParseUri(node.value);

    if (uri == null) {
      throw new ParseError.atNode(node, 'Could not parse test uri: $node');
    }

    return uri;
  } else {
    throw new ParseError.atNode(node, 'Invalid test');
  }
}

/// Recipe parser internal entry point.
Recipe _parseRecipe(final YamlMap yaml,
    [final String filename, final Map<String, YamlMap> imports]) {
  const List<String> allowedKeys = const [
    'title',
    'use',
    'verb',
    'input',
    'output',
    'recipe',
    'test',
    'import',
  ];

  for (final key in yaml.keys) {
    if (!allowedKeys.contains(key)) {
      throw new ParseError.atNode(
          yaml.nodes[key], 'Invalid top level keyword $key');
    }
  }

  final String title = parseTitle(yaml.nodes['title']);

  // We parse use: next, as it is needed to resolve identifiers in the rest of
  // the recipe. We also accumulate the use sections of transitive imports here.
  //
  // This also parses schemas; however, in a recipe schemas are not applied to
  // the expressions, this is done only in manifests.
  final ParserState parserState = new ParserState();
  parseImportUseSchema(parserState, yaml.nodes['import'], yaml.nodes['use'],
      yaml.nodes['schema'], filename, imports);

  final Verb verb = parseVerb(parserState, yaml.nodes['verb']);
  final List<PatternExpr> input = parseInput(parserState, yaml.nodes['input']);
  final List<PatternExpr> output =
      parseOutput(parserState, yaml.nodes['output']);
  final List<Step> steps =
      parseRecipeSteps(parserState, yaml.nodes['recipe']) ?? [];
  final Uri test = parseTest(parserState, yaml.nodes['test']);

  return new Recipe(steps,
      title: title,
      use: parserState.uses,
      verb: verb,
      input: _flattenPatternExprList(input),
      output: _flattenPatternExprList(output),
      test: test);
}

Recipe parseRecipe(final String data) {
  return Timeline.timeSync('parseRecipe()', () {
    return _parseRecipe(loadYaml(data) ?? new YamlMap());
  });
}

Future<Recipe> parseRecipeFile(final String filename,
    [final Importer importer = loadFromFile]) async {
  final Map<String, YamlMap> imports = await resolveImports(filename, importer);
  return _parseRecipe(imports[filename], filename, imports);
}

/// Manifest parser internal entry point.
Manifest _parseManifest(final YamlMap yaml,
    [final String filename, final Map<String, YamlMap> imports]) {
  const List<String> allowedKeys = const [
    'title',
    'icon',
    'url',
    'theme-color',
    'use',
    'verb',
    'input',
    'output',
    'compose',
    'display',
    'test',
    'import',
    'schema',
    'arch',
    'modularRevision'
  ];

  for (final key in yaml.keys) {
    if (!allowedKeys.contains(key)) {
      throw new ParseError.atNode(
          yaml.nodes[key], 'Invalid top level keyword $key');
    }
  }

  final String title = parseTitle(yaml.nodes['title']);

  final Uri icon = parseUrl(yaml.nodes['icon']);
  final int themeColor = parseThemeColor(yaml.nodes['theme-color']);

  final String arch = parseArch(yaml.nodes['arch']);
  final String modularRevision =
      parseModularRevision(yaml.nodes['modularRevision']);

  // We parse use: next, as it is needed to resolve identifiers in the rest of
  // the manifest. We also accumulate the use sections of transitive imports
  // here.
  //
  // At the same time, we parse schemas in the imported files and in this
  // file. The schemas defined in each file may use the names declared in the
  // use sections defined in its own and in all imported files.
  final ParserState parserState = new ParserState();
  parseImportUseSchema(parserState, yaml.nodes['import'], yaml.nodes['use'],
      yaml.nodes['schema'], filename, imports);

  final Uri url = parseUrl(yaml.nodes['url']);
  final Verb verb = parseVerb(parserState, yaml.nodes['verb']);

  final List<PatternExpr> inputPatterns =
      parseInput(parserState, yaml.nodes['input']);
  applySchemas(parserState, yaml.nodes['input'], inputPatterns);
  final List<PathExpr> input = _flattenPatternExprList(inputPatterns);

  final List<PatternExpr> outputPatterns =
      parseOutput(parserState, yaml.nodes['output']);
  applySchemas(parserState, yaml.nodes['output'], outputPatterns);
  final List<PathExpr> output = _flattenPatternExprList(outputPatterns);

  // Schemas are not applied to display and compose, because (a) display nodes
  // are not really part of schemas, and (b) if they are attached to any node
  // under a schema, doesn't mean anything else in the schema has a display not
  // attached to it too.

  final List<PatternExpr> displayPatterns =
      parseOutput(parserState, yaml.nodes['display']);
  final List<PathExpr> display = _flattenPatternExprList(displayPatterns);

  final List<PatternExpr> composePatterns =
      parseInput(parserState, yaml.nodes['compose']);
  final List<PathExpr> compose = _flattenPatternExprList(composePatterns);

  if (yaml.nodes['test'] != null) {
    print('ignoring yaml section test');
  }

  return new Manifest(
      title, url, parserState.uses, verb, input, output, display, compose,
      icon: icon,
      themeColor: themeColor,
      schemas: parserState.schemas.values.toList(growable: false),
      arch: arch,
      modularRevision: modularRevision);
}

Manifest parseManifest(final String data) {
  return Timeline.timeSync('parseManifest()', () {
    return _parseManifest(loadYaml(data, sourceUrl: 'inline'));
  });
}

List<Manifest> parseManifests(final String data) {
  return Timeline.timeSync('parseManifests()', () {
    return new List<Manifest>.from(
        loadYamlStream(data).map((node) => _parseManifest(node)));
  });
}

Future<Manifest> parseManifestFile(final String filename,
    [final Importer importer = loadFromFile]) async {
  final Map<String, YamlMap> imports = await resolveImports(filename, importer);
  return _parseManifest(imports[filename], filename, imports);
}

/// A callable that takes the resolved name of the import as argument and
/// returns a future of the text content identified by the name.
typedef Future<String> Importer(String import);

/// An implementation of Importer that just loads files from the file system. An
/// alternative implementation is used when parsing recipes in a mojo content
/// handler, where imports need to be loaded through the mojo network service.
Future<String> loadFromFile(final String name) {
  final File file = new File(name);
  return file.readAsString();
}

/// Loads the given file and all imported files declared in its import
/// sections. The given file is included under its name in the returned import
/// map. This is not done during parsing itself in order to keep the parsing
/// synchronous.
Future<Map<String, YamlMap>> resolveImports(
    final String filename, final Importer importer) async {
  final Map<String, YamlMap> ret = <String, YamlMap>{};

  final List<String> todo = <String>[filename];
  while (todo.isNotEmpty) {
    final String currentFilename = todo.removeAt(0);
    final String text = await importer(currentFilename);
    final YamlNode yamlDoc = loadYaml(text, sourceUrl: currentFilename);
    if (yamlDoc is! YamlMap) {
      continue;
    }
    final YamlMap yaml = (yamlDoc as YamlMap);

    ret[currentFilename] = yaml;

    if (yaml.nodes['import'] == null) {
      continue;
    }

    if (yaml.nodes['import'] is! YamlList) {
      continue;
    }

    for (final YamlNode import in (yaml.nodes['import'] as YamlList).nodes) {
      final String importFilename =
          _resolveFilename(currentFilename, import.value);

      if (!ret.containsKey(importFilename)) {
        todo.add(importFilename);
      }
    }
  }

  return ret;
}

/// Resolves a relative filename to a base filename. Right now we assume that it
/// handles both filenames and String representations of URIs, but it could be
/// abstracted into Importer, where it could be done more specifically to the
/// import machinery used.
String _resolveFilename(final String base, final String file) {
  return path.join(path.dirname(base), file);
}
