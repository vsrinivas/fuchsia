// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

class BasicGnParser {
  List<String> imports;
  Map<String, dynamic> assignedVariables;
  List<String> parseErrors;

  BasicGnParser(List gnJson) {
    imports = [];
    assignedVariables = {};
    parseErrors = [];
    _parse(gnJson);
  }

  static String parseIdentifier(Map el) {
    if (el['type'] != 'IDENTIFIER') {
      throw UnsupportedError('Element must be of type IDENTIFIER: $el');
    }
    return el['value'];
  }

  static String parseLiteral(Map el) {
    if (el['type'] != 'LITERAL') {
      throw UnsupportedError('Element must be of type LITERAL: $el');
    }
    var value = el['value'];
    // remove leading and trailing quotes, faster than regex:
    int start = 0, end = value.length;
    if (end > 0 && value[start] == '\'') {
      start++;
    }
    if ((end - start) > 0 && value[end - 1] == '\'') {
      end--;
    }
    return value.substring(start, end);
  }

  static List<String> parseListOfLiterals(Map listEl) {
    if (listEl['type'] != 'LIST') {
      throw UnsupportedError('Element must be of type LIST: $listEl');
    }
    List<String> result = [];
    for (var el in listEl['child']) {
      result.add(parseLiteral(el));
    }
    return result;
  }

  static dynamic parseLiterals(Map el) {
    String type = el['type'];
    if (type != 'LITERAL' && type != 'LIST') {
      throw UnsupportedError('Element must be of type LITERAL or LIST: $el');
    }
    if (type == 'LITERAL') {
      return parseLiteral(el);
    } else {
      return parseListOfLiterals(el);
    }
  }

  void _parseAssigment(Map el) {
    if (el['type'] != 'BINARY') {
      throw UnsupportedError('Element must be of type BINARY: $el');
    }
    String id = parseIdentifier(el['child'][0]);
    dynamic value = parseLiterals(el['child'][1]);

    bool append = el['value'] == '+=';
    if (append) {
      dynamic previous = assignedVariables[id] ?? [];
      if (previous is! List) {
        previous = [previous];
      }
      if (value is List) {
        value.addAll(List<String>.from(value));
      } else {
        previous.add(value);
        value = previous;
      }
    }
    assignedVariables[id] = value;
  }

  void _parseFunction(Map el) {
    if (el['type'] != 'FUNCTION') {
      throw UnsupportedError('Element must be of type FUNCTION: $el');
    }
    if (el['value'] != 'import') {
      // silently ignore functions that are not import
      return;
    }
    dynamic value = parseLiterals(el['child'][0]);
    if (value is List<String>) {
      imports.addAll(value);
    } else {
      imports.add(value);
    }
  }

  void _parse(List gnJson) {
    for (Map<String, dynamic> el in gnJson) {
      try {
        switch (el['type']) {
          case 'BINARY':
            _parseAssigment(el);
            break;
          case 'FUNCTION':
            _parseFunction(el);
            break;
          default: // ignore
        }
        // ignore: avoid_catching_errors
      } on UnsupportedError catch (ex) {
        parseErrors.add(ex.message);
      }
    }
  }
}
