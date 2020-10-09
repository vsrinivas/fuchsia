# Fuchsia Shell Parser

Provides a basic parser for the shell grammar.

The parser is built on combinators, which means the parser is built out of other parsers.

A parser in the source appears as a function taking a single argument of type `ParseResult` and
returning a value of type `ParseResult`. The argument is the result of parsing a prefix before the
current parse. As an example, consider the grammar:

```
S <- A B
```

Assuming we had parser functions for `A` and `B`, named `A()` and `B()` respectively, we could
implement `S` as follows:

```
ParseResult S(ParseResult prefix) {
  return B(A(prefix));
}
```

A `ParseResult` represents a position in the string. If parsing fails completely, the returned
`ParseResult` is equal to `ParseResult::kEnd`, which tests `false` when used as a boolean.
Generally, we hope that a failed parse will be recovered, so we will always get a valid parse from
our grammar, but it may or may not contain error nodes where we had to recover a failure.

`ParseResult` contains three pieces of information: the "tail" of the parse, representing the
un-parsed data remaining after the parse, error statistics, which includes the number of errors and
the number of characters that were successfully parsed without error, and a stack, which contains
the AST nodes we have produced.

To understand the stack, consider our `A B` example above. After successfully parsing `S`, the stack
might contain two nodes:

```
Node::A("A") Node::B("B")
```

The `ParseResult`'s `node` accessor would yield `Node::B`, as it yields the top of the stack. We can
call `Reduce()` on a `ParseResult` to collapse the stack into a single node, so if we called
`Reduce<Node::S>()` on our result, we would end up with one node on the stack:

```
Node::S(children = { Node::A Node::B })
```

`Reduce()` will consume the entire stack by default. This is inconvenient for more complicated
parsers. To make `S` safe, we would insert a "marker node" to indicate the point in the stack where
`Reduce()` should stop consuming. Thus our implementation looks like this:

```
ParseResult S(ParseResult prefix) {
  return B(A(prefix.Mark())).Reduce<Node::S>();
}
```

This pattern is somewhat unwieldy, so we implement several combinator functions to produce it
automatically. The `NT` ("NonTerminal") combinator takes a parser as input and automatically places
a `Mark()` before it and a `Reduce()` after it, while the `Seq` combinator runs two parsers in
sequence.

```
ParseResult S(ParseResult prefix) {
  return NT<Node::S>(Seq(A, B));
}
```

## Error Handling in a Nutshell

To mark and recover an error during parsing, there are two special combinators: `ErInsert` and
`ErSkip`.

`ErSkip` takes a child parser, which it runs. If that child parser parses without error, the parsed
text is discarded and an error is inserted instead. Here's an example:

```
Alt(Expression, ErSkip("Expected expression before '%MATCH%'", OnePlus(AnyCharBut(";"))));
```

This parser would attempt to parse an expression, and if it can't find one, it will consume up to
the next semicolon and insert an error message. Note the `'%MATCH%'` in the message string; this
will be substituted for the text matched before the semicolon. The parse result moves ahead by the
length of the match, but the internal count of successfully-parsed characters (accessed via
`parsed_successfully()`) does not increase.

If the parser given to `ErSkip` doesn't match without error, `ErSkip` fails completely, so error
recovery does not occur.

`ErInsert` is more straightforward than `ErSkip`. It injects an error at the current parse position
without moving.

```
Alt(Expression, Alt(Token(";"), ErInsert("Expected semicolon")));
```
