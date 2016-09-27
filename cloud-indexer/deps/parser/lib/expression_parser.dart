// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// See expression.ebnf for the grammar specification.

import 'cardinality.dart';
import 'expression.dart';
import 'parse_error.dart';

/// A scanner for the input of the expression parser.
class Scanner {
  // The location of the expression being scanned in the containing yaml
  // file. Used in parse error reporting to compute file-absolute locations of
  // sub expressions.
  final SourceLocation _location;

  // The declarations list of the yaml file the expression is from.
  final ParserState _parserState;

  // The source text of the expression to parse.
  final String _text;

  // The current position in the source text.
  int _pos = 0;

  Scanner(this._parserState, this._location, this._text);

  String get debug => "[$_text][$_pos]";

  SourceLocation get location => new SourceLocation.expr(_location, _pos);

  ParserState get parserState => _parserState;

  int mark() => _pos;

  void reset(final int mark) {
    _pos = mark;
  }

  static const _alpha = "ABCDEFGHAIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
  static const _digit = "0123456789";
  static const _dash = "-";
  static const _identifier = _alpha + _digit + _dash;
  static const _blank = ' \n\t';

  static bool _isBlank(final String char) => _blank.contains(char);

  bool _end() => _pos >= _text.length;

  bool nextIsAlphaNumeric() {
    _skipBlank();

    return !_end() && (_alpha + _digit).contains(_text[_pos]);
  }

  void checkDone() {
    _skipBlank();
    if (!_end()) {
      throw new ParseError.atLocation(
          location, 'Unparsed expression: ${debug}');
    }
  }

  void _skipBlank() {
    while (!_end() && _isBlank(_text[_pos])) {
      _pos++;
    }
  }

  bool token(final String token) {
    _skipBlank();

    if (token.length > 0 &&
        _pos + token.length <= _text.length &&
        _text.substring(_pos, _pos + token.length) == token) {
      _pos += token.length;
      return true;
    } else {
      return false;
    }
  }

  // A urltoken is any running sequence of nonblank text, except for >,
  // and except for other punctuation at the end.
  String urltoken() {
    _skipBlank();

    final int start = _pos;
    while (!_end() && !_isBlank(_text[_pos]) && _text[_pos] != '>') {
      _pos++;
    }

    // HACK(mesch): Other punctuation *at the end* of the urltoken is
    // cardinality or other punctuation. A dash directly in front of a > is
    // excluded too, but otherwise allowed.
    while (_pos > 0 && '?+*,{}<>()'.contains(_text[_pos - 1]) ||
        (_text[_pos - 1] == '-' && _pos < _text.length && _text[_pos] == '>')) {
      _pos--;
    }

    return _text.substring(start, _pos);
  }

  // An identifier is a run of alphanumerical characters and dashes. A leading
  // digit is not allowed. A trailing dash is not part of an identifier. We need
  // this to support arrows in path expressions without separating whitespace:
  //
  //  foo-token->bar-token
  //
  String identifier() {
    _skipBlank();

    final int start = _pos;
    if (!_end() && _alpha.contains(_text[_pos])) {
      _pos++;

      while (!_end() && _identifier.contains(_text[_pos])) {
        _pos++;
      }

      while (_pos > start && _text[_pos - 1] == '-') {
        _pos--;
      }
    }

    return _text.substring(start, _pos);
  }
}

PatternExpr parsePattern(final Scanner input) {
  return _parsePattern(input, top: true);
}

PatternExpr _parsePattern(final Scanner input, {final bool top: false}) {
  Property property;
  // Only the first component of a pattern expression may be a wildcard _, which
  // matches any label. All other components of a pattern expression must
  // specify actual labels to match.
  if (top && input.token('_')) {
    property = new Property(null);
  } else {
    property = parseProperty(input);
  }

  final PatternExpr ret = new PatternExpr(property);

  if (input.token('->')) {
    ret.children.add(_parsePattern(input));
  }

  if (input.token('{')) {
    ret.children.add(_parsePattern(input));
    while (input.token(',')) {
      ret.children.add(_parsePattern(input));
    }

    if (!input.token('}')) {
      throw new ParseError.atLocation(
          input.location, 'Missing } after expected properties.');
    }
  }

  return ret;
}

Property parseProperty(final Scanner input) {
  return new Property(parseLabelSet(input), parseCardinality(input),
      parseRepresentation(input));
}

List<Label> parseRepresentation(final Scanner input) {
  if (!input.token('<')) {
    return null;
  }

  final ret = <Label>[parseLabel(input)];

  while (input.token(',')) {
    ret.add(parseLabel(input));
  }

  if (!input.token('>')) {
    throw new ParseError.atLocation(
        input.location, 'Missing > after representation types.');
  }

  return ret;
}

Cardinality parseCardinality(final Scanner input) {
  var ret = Cardinality.singular;
  if (input.token('*')) {
    ret = Cardinality.optionalRepeated;
  } else if (input.token('?')) {
    ret = Cardinality.optional;
  } else if (input.token('!')) {
    // TODO(mesch): Soon, we switch default cardinality to optional,
    // and remove the ? qualifier.
    ret = Cardinality.singular;
  } else if (input.token('+')) {
    ret = Cardinality.repeated;
  }
  return ret;
}

Set<Label> parseLabelSet(final Scanner input) {
  final Set<Label> labels = new Set<Label>();

  if (input.token('(')) {
    labels.add(parseLabel(input));
    while (!input.token(')')) {
      labels.add(parseLabel(input));
    }
  } else {
    labels.add(parseLabel(input));
    if (input.nextIsAlphaNumeric()) {
      throw new ParseError.atLocation(
          input.location, 'Multiple label properties must be in parens.');
    }
  }

  return labels;
}

Label parseLabel(final Scanner input) {
  final mark = input.mark();
  var text = input.urltoken();

  // HACK(mesch): The URI parser throws an exception to tell us it
  // cannot parse. For our purposes, it's not an exception, but merely a
  // decision, but we still have to use the exception API here.
  try {
    final Uri uri = Uri.parse(text);
    if (uri.hasScheme) {
      return new Label.fromUri(uri);
    }
  } on FormatException {}

  // Not a URL, so we scan for a shorthand identifier instead.
  input.reset(mark);

  final label = input.identifier();

  if (label.isEmpty) {
    throw new ParseError.atLocation(
        input.location, 'Invalid empty label in "${input._text}"');
  }

  if (!input.parserState.shorthand.containsKey(label)) {
    throw new ParseError.atLocation(
        input.location, 'label missing in "use:" section: "$label"');
  }

  return new Label(input.parserState.shorthand[label].uri,
      input.parserState.shorthand[label].shorthand);
}
