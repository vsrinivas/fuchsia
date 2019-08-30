// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

class BasicGnParser {
  List<String> imports;
  Map<String, dynamic> assignedVariables;
  List<String> parseErrors;

  BasicGnParser(List gnJson) {
    imports = new List<String>();
    assignedVariables = new Map();
    parseErrors = new List<String>();
    _parse(gnJson);
  }

  static String parseIdentifier(Map el) {
    if (el['type'] != 'IDENTIFIER') {
      throw UnsupportedError(
          'Element must be of type IDENTIFIER: ' + el.toString());
    }
    return el['value'];
  }

  static dynamic parseLiteral(Map el) {
    if (el['type'] != 'LITERAL') {
      throw UnsupportedError(
          'Element must be of type LITERAL: ' + el.toString());
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
    value = value.substring(start, end);
    return value;
  }

  static List parseListOfLiterals(Map listEl) {
    if (listEl['type'] != 'LIST') {
      throw UnsupportedError(
          'Element must be of type LIST: ' + listEl.toString());
    }
    List<String> result = new List<String>();
    for (var el in listEl['child']) {
      result.add(parseLiteral(el));
    }
    return result;
  }

  static dynamic parseLiterals(Map el) {
    String type = el['type'];
    if (type != 'LITERAL' && type != 'LIST') {
      throw UnsupportedError(
          'Element must be of type LITERAL or LIST: ' + el.toString());
    }
    if (type == 'LITERAL') {
      return parseLiteral(el);
    } else {
      return parseListOfLiterals(el);
    }
  }

  _parseAssigment(Map el) {
    if (el['type'] != 'BINARY') {
      throw UnsupportedError(
          'Element must be of type BINARY: ' + el.toString());
    }
    String id = parseIdentifier(el['child'][0]);
    dynamic value = parseLiterals(el['child'][1]);

    bool append = el['value'] == '+=';
    if (append) {
      dynamic previous = assignedVariables[id] ?? new List<String>();
      if (previous is! List) {
        previous = [previous];
      }
      if (value is List) {
        value.addAll(previous);
      } else {
        previous.add(value);
        value = previous;
      }
    }
    assignedVariables[id] = value;
  }

  _parseFunction(Map el) {
    if (el['type'] != 'FUNCTION') {
      throw UnsupportedError(
          'Element must be of type FUNCTION: ' + el.toString());
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

  _parse(List gnJson) {
    for (var el in gnJson) {
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
      } on UnsupportedError catch (ex) {
        parseErrors.add(ex.message);
      }
    }
  }
}
