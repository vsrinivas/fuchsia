// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:io';

import 'package:path/path.dart' as path;
import 'package:yaml/yaml.dart';

import 'package:doc_checker/errors.dart';
import 'package:doc_checker/link_verifier.dart';

/// Loads yaml files to verify they are
/// compatible with fuchsia.dev hosting.
class YamlChecker {
  Set<String> _yamlSet;
  Set<String> _mdSet;
  String _rootYaml;
  String _rootDir;

  // List of errors found.
  final List<Error> errors = <Error>[];
  final List<Link<String>> outOfTreeLinks = <Link<String>>[];

  static const List<String> validStatusValues = <String>[
    'alpha',
    'beta',
    'deprecated',
    'experimental',
    'external',
    'limited',
    'new'
  ];

  /// Creates a new instance of YamlChecker.
  ///
  /// [rootDir] is the parent directory of 'docs'. This is used
  ///           to locate the files on the filesystem.
  /// [rootYaml] is the yaml file to start the checking from.
  /// [yamls] is the list of .yaml files to check.
  /// [mdFiles] is the list of .md files to make sure they are
  ///           referenced in the yaml files.
  YamlChecker(String rootDir, String rootYaml, List<String> yamls,
      List<String> mdFiles) {
    _rootYaml = rootYaml;
    _rootDir = rootDir;
    _yamlSet = <String>{}
      ..addAll(yamls)
      ..remove(_rootYaml);

    _mdSet = <String>{}
      ..addAll(filterHidden(mdFiles))
      // Remove navbar.md since it is only used on fuchsia.googlesource.com.
      ..remove('$_rootDir/docs/navbar.md')
      // Remove docs/gen/build_arguments.md since it is generated, it is
      // accessed as a source file and not published.
      ..remove('$_rootDir/docs/gen/build_arguments.md')
      ..remove('$_rootDir/docs/gen/zircon_build_arguments.md');
  }

  /// Filters out paths that are hidden names according to
  /// https://developers.google.com/devsite/reference/filenames?hl=en#hidden_files_single_underscore.
  /// This function expects the input paths to already be canonicalized.
  List<String> filterHidden(List<String> mdFiles) {
    return mdFiles
        .where((doc) => !path
            .split(path.relative(doc, from: _rootDir))
            .any((component) => component.startsWith('_')))
        .toList();
  }

  /// Checks the validity of the yaml files.
  /// Returns true if no errors are found.
  /// Errors can be retrieved via the [errors]
  /// property.
  Future<bool> check() {
    Completer<bool> completer = Completer<bool>();
    // the first yaml file is the root yaml.

    if (_rootYaml != null) {
      File f = File(_rootYaml);
      f.readAsString().then((String data) {
        parse(loadYamlDocuments(data), _rootYaml);
        for (String s in _yamlSet) {
          errors
              .add(Error(ErrorType.invalidMenu, null, 'unreferenced yaml $s'));
        }
        for (String s in _mdSet) {
          errors.add(Error(ErrorType.unreachablePage, null,
              'File $s not referenced in any yaml file'));
        }
        completer.complete(errors.isEmpty);
      });
    } else {
      completer.complete(true);
    }

    return completer.future;
  }

  /// Parses the yaml structure.
  void parse(List<YamlDocument> doc, String filename) {
    for (YamlDocument d in doc) {
      validateTopLevel(d.contents.value, filename);
    }
  }

  /// Validates the top level of the menu.
  void validateTopLevel(YamlMap map, String filename) {
    for (String key in map.keys) {
      switch (key) {
        case 'guides':
        case 'samples':
        case 'support':
        case 'reference':
        case 'toc':
          {
            validateTocElement(map[key], filename);
          }
          break;
        default:
          {
            errors.add(Error(ErrorType.invalidMenu, filename,
                'Unknown top level key: $key'));
            break;
          }
      }
    }
  }

  /// Validates the toc element in the menu.
  void validateTocElement(YamlNode val, String filename) {
    // valid entries are documented at
    if (val is YamlList) {
      for (YamlNode node in val) {
        if (node.runtimeType == YamlMap) {
          validateContentMap(node, filename);
        } else {
          errors.add(Error(ErrorType.invalidMenu, filename,
              'Unexpected node of type: ${node.runtimeType} $node'));
        }
      }
    } else {
      errors.add(Error(ErrorType.invalidMenu, filename,
          'Expected a list, got ${val.runtimeType}: $val'));
    }
  }

  /// Validates the content of toc.
  void validateContentMap(YamlMap map, String filename) {
    for (String key in map.keys) {
      switch (key) {
        case 'alternate_paths':
          {
            validatePathList(map[key], filename);
          }
          break;
        case 'break':
        case 'skip_translation':
          {
            // only include if break : true.
            if (map[key] != true) {
              errors.add(Error(ErrorType.invalidMenu, filename,
                  '$key should only be included when set to true'));
            }
          }
          break;
        case 'section':
        case 'contents':
          {
            validateTocElement(map[key], filename);
          }
          break;
        case 'include':
          {
            processIncludedFile(map[key], filename);
          }
          break;
        case 'path':
          {
            String menuPath = map[key];
            if (validatePath(menuPath, filename)) {
              // If the path is to a file, check that the file exists.
              if (!menuPath.startsWith('https://') &&
                  !menuPath.startsWith('//')) {
                checkFileExists(menuPath, filename);
              } else {
                Uri uri;
                try {
                  if (menuPath.startsWith('//')) {
                    uri = Uri.parse('https:$menuPath');
                  } else {
                    uri = Uri.parse(menuPath);
                  }
                } on FormatException {
                  errors.add(Error(ErrorType.invalidUri, filename, menuPath));
                  continue;
                }
                outOfTreeLinks.add(Link(uri, filename));
              }
            }
          }
          break;
        case 'path_attributes':
          {
            errors.add(Error(ErrorType.invalidMenu, filename,
                'path_attributes not supported on fuchsia.dev'));
          }
          break;
        case 'status':
          {
            if (!validStatusValues.contains(map[key])) {
              errors.add(Error(ErrorType.invalidMenu, filename,
                  'invalid status value of ${map[key]}'));
            }
          }
          break;
        case 'step_group':
          {
            // make sure there is no section in this group.
            if (map.containsKey('section')) {
              errors.add(Error(ErrorType.invalidMenu, filename,
                  'invalid use of \'step_group\'. Group cannot also contain \`section\`'));
            }
            if (!(map[key] is String)) {
              errors.add(Error(ErrorType.invalidMenu, filename,
                  'invalid value of \'step_group\'. Expected String got ${map[key].runtimeType} ${map[key]}'));
            }
          }
          break;
        case 'style':
          {
            if (map.containsKey('break') || map.containsKey('include')) {
              errors.add(Error(ErrorType.invalidMenu, filename,
                  'invalid use of \'style\'. Group cannot also contain `break` nor `include`'));
            }
            if (map[key] != 'accordion' && map[key] != 'divider') {
              errors.add(Error(ErrorType.invalidMenu, filename,
                  'invalid value of `style`. Expected `accordion` or `divider`'));
            }
            if (!map.containsKey('heading') && !map.containsKey('section')) {
              errors.add(Error(ErrorType.invalidMenu, filename,
                  'invalid use of \'style\'. Group must also have `heading` or `section`.'));
            }
          }
          break;
        case 'heading':
        case 'name':
        case 'title':
          {
            if (map[key].runtimeType != String) {
              errors.add(Error(ErrorType.invalidMenu, filename,
                  'Expected String for $key, got ${map[key].runtimeType}'));
            }
          }
          break;
        default:
          errors.add(Error(
              ErrorType.invalidMenu, filename, 'Unknown Content Key $key'));
      }
    }
  }

  /// Handles an included yaml file. It is validated completely then returns.
  void processIncludedFile(String menuPath, String parentFilename) {
    if (validatePath(menuPath, parentFilename)) {
      String filePath = '$_rootDir$menuPath';

      // parse in a try/catch to handle any syntax errors reading the included file.
      try {
        parse(loadYamlDocuments(File(filePath).readAsStringSync()), menuPath);
        _yamlSet.remove(filePath);
      } on Exception catch (exception) {
        errors.add(
            Error(ErrorType.invalidMenu, parentFilename, exception.toString()));
      }
    }
  }

  /// Validates a list of paths are valid paths for the menu.
  bool validatePathList(List<String> paths, String filename) {
    for (String s in paths) {
      if (!validatePath(s, filename)) {
        return false;
      }
    }
    return true;
  }

  /// Validates the path is valid for the menu.
  ///
  /// Valid paths are /docs/* and
  /// http URLs.  Exceptions are made for
  /// CONTRIBUTING.md and CODE_OF_CONDUCT.md which
  /// are in the root of the project.
  bool validatePath(String menuPath, String filename) {
    if (!menuPath.startsWith('/docs') &&
        menuPath != '/CONTRIBUTING.md' &&
        menuPath != '/CODE_OF_CONDUCT.md' &&
        !menuPath.startsWith('http://') &&
        !menuPath.startsWith('https://') &&
        !menuPath.startsWith('//')) {
      errors.add(Error(ErrorType.invalidMenu, filename,
          'Path needs to start with \'/docs\', got $menuPath'));
      return false;
    }

    return true;
  }

  /// Check that the file pointed to by the path exists.
  /// if it does, remove the file from the mdSet to keep track of
  /// which files are referenced.
  void checkFileExists(String menuPath, String filename) {
    String filePath = '$_rootDir$menuPath';

    if (File(filePath).existsSync()) {
      _mdSet.remove(filePath);
    } else {
      // could be a directory, so add README.md.
      filePath = '$filePath/README.md';
      if (File(filePath).existsSync()) {
        _mdSet.remove(filePath);
      } else {
        errors.add(Error(ErrorType.invalidMenu, filename,
            'Invalid menu path $menuPath not found'));
      }
    }
  }
}
