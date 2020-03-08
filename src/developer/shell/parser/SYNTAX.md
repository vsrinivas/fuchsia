# Fuchsia Shell Syntax

The Fuchsia shell syntax is defined as a [Parsing Expression
Grammar](https://en.wikipedia.org/wiki/Parsing_expression_grammar). This means alternation in the
specification of the grammar is explicitly sequential; `A ← B | C` will *always* match `B` if it
can. By convention, we use the syntax `A ← B / C` to make it explicit that alternation is
sequential.

## Grammar Specification Conventions

We will use the following syntax to specify our grammar in this document:

* Literal tokens will be reflected with single quotes, as in `'while'` or `'|>'`
* Non-terminals will be camel-cased and capitalized, as in `Expression` or `FunctionBody`
* One or more terms listed consecutively with no operator between them are referred to as a
  "sequence"
* `⊔` indicates a sequence of one or more whitespace characters. See below.
* A sequence surrounded by parentheses forms a single term.
* A term can be suffixed with `*` to indicate zero or more repetitions, `+` to indicate one or more
  repetitions, `?` to indicate zero or one occurrences, `{m,n}` to indicate between `m` and `n`
  repetitions, and `{n}` to indicate exactly `n` repetitions.
* Sequences separated by `/` are sequential alternatives.
* A term prefixed by `&` is a zero-length match. It will not consume the text it matches and terms
  after it will attempt to match at the same position the zero-length term did.
* A term prefixed by `!` is an inverse match. This behaves as a zero-length match, but parsing fails
  if matching this term succeeds and vice versa.
* Two terms joined by `∩` are intersected terms. We define the intersected term as a term which
  matches the longest possible string which matches both terms. By convention, the parse tree
  yielded from this operation is assumed to be the parse tree of the right-hand operand term.
* `[` and `]` delineate a character match block, as would appear in a Perl-compatible regular
  expression.
* `<nl>` indicates the newline character.
* `.` is a term which matches any single character.
* Productions will be indicated with `←` as in `Addition ← Multiplication '+' Multiplication`
* `←⊔` indicates a production where between each term, and each subterm in grouped sequences, the
  term `⊔?` is present, but has been elided for clarity. More plainly, `←⊔` indicates a term which
  is whitespace-insensitive.

We will assume our input is a stream of UTF-8 Characters.

### Whitespace

We define Whitespace as follows:

```
⊔ ← '#' (!<nl> .)* <nl> / AnyUnicodeWhitespace+
```

Where `AnyUnicodeWhitespace` is any single character classified as whitespace by the Unicode
standard. (NOTE: Today's parser only counts space, newline, carriage return, and tab).

Note that our comment syntax is embedded in our whitespace definition:

```
# This line will parse entirely as whitespace.
```

## Identifiers

Identifiers are defined as follows:

```
UnescapedIdentifier ← [a-zA-Z0-9_]+
Identifier ← ![0-9] UnescapedIdentifier
```

Valid identifiers might include:

```
foo
item_0
a_Mixed_Bag
```

## Integers

Integers are defined as follows:

```
Digit ← [0-9]
HexDigit ← [a-fA-F0-9]
DecimalInteger ← 0 !Digit / !'0' Digit+ ( '_' Digit+ )*
HexInteger ← '0x' HexDigit+ ( '_' HexDigit+ )*
Integer ← DecimalInteger / HexInteger
```

Valid integers might include:

```
0
12345
12_345
0x1234abcd
0x12_abcd
```

## Strings

Strings are defined as follows:

```
EscapeSequence ← '\n' / '\t' / '\r' / '\' <nl> / '\\' / '\"' / '\u' HexDigit{6}
StringEntity ← !( '\' / '"' / <nl> ) . / EscapeSequence
NormalString ← '"' StringEntity* '"'
String ← NormalString / MultiString
```

TODO: Define `MultiString`

Valid strings might include:

```
"The quick brown fox jumped over the lazy dog."
"A newline.\nA tab\tA code point\u00264b"
"String starts here \
and keeps on going"
```

## Paths

Paths are defined as follows:

```
PathCharacter ← ![`&;|/\()[]{}] .
PathElement ← PathCharacter+ / '\' . / '`' ( !'`' . )* '`'
RootPath ← ( '/' PathElement+ )+
Path ← '.'? RootPath '/'? / '.'? '/' / '.'
```

Valid paths might include:

```
/foo
/foo/bar
/foo/bar/
./foo/bar/
./
/
.
```

## Variable Declarations

Variable declarations are defined as follows:

```
KWVar ← 'var' !IdentifierCharacter
KWConst ← 'const' !IdentifierCharacter
VariableDecl ←⊔ ( KWVar / KWConst ) Identifier '=' Expression
```

Valid variable declarations might include:

```
var foo = 4
const foo = "Ham Sandwich"
```

## Object literals

Object literals are defined as follows:

```
Object ←⊔ '{' ObjectBody? '}'
ObjectBody ←⊔ Field ( ',' Field  )* ','?
Field ←⊔ ( NormalString / Identifier  ) ':' SimpleExpression
```

Valid object literals might include:

```
{}
{ foo: 6, "bar & grill": "Open now"  }
{ foo: { bar: 6  }, "bar & grill": "Open now"  }
```

## Expressions

Expressions are defined as follows:

```
Expression ← Value
Value ← Object / Atom
Atom ← Identifer / String / Real / Integer / Path

```

## Programs

A program is defined as:

```
Program ←⊔ VariableDecl ([;&] Program)?
```
