# Fuchsia Shell Parser

Provides a basic parser for the shell grammar.

The parser is built on combinators, which means the parser is built out of other parsers.

A parser in the source appears as a function taking a single argument of type ParseResultStream and
returning a value of type ParseResultStream. The argument is the result of parsing a prefix before
the current parse. As an example, consider the grammar:

  S <- A B

Assuming we had parser functions for A and B, named A() and B() respectively, we could implement S
as follows:

  ParseResultStream S(ParseResultStream prefixes) {
    return B(A(prefixes));
  }

(There's some subtleties here, such that this rule wouldn't be implemented this way in practice, but
it gets a point across).

ParseResultStream is a stream of ParseResult objects. If parsing goes normally, the first
ParseResult yielded by the stream is the only one of interest. If parsing has failed, each
successive ParseResult yielded by the stream will contain a different attempt at error recovery.
Successful parses will also yield error recovery attempts if polled further, as a parse that seemed
okay may turn out to be wrong after parsing further. As an example, consider a grammar that parses
nested parentheses encountering the string '('. We might parse the opening parenthesis successfully,
then try to parse the closing parenthesis. When we discover it's missing, we might decide that the
opening parenthesis should not actually have parsed successfully.

ParseResultStream also contains a flag indicating whether the last parser succeeded. This is useful
in avoiding error recovery in some cases, as polling the stream will always attempt to construct a
recovered parse, whereas we sometimes only want to know the parse failed but don't need a recovery.

ParseResult itself contains three pieces of information: the "tail" of the parse, representing the
un-parsed data remaining after the parse, various information comprising an "error score", which
indicates how much error recovery was necessary to make the parse succeed, and a stack, which
contains the AST nodes we have produced.

To understand the stack, consider our A B example above. After successfully parsing S, the stack
might contain two nodes:

Node("A") Node("B")

The ParseResult's "node" accessor would yield Node("B"), as it yields the top of the stack. We can
call Reduce() on a ParseResult to collapse the stack into a single node, so if we called Reduce("S")
on our result, we would end up with one node on the stack:

Node("S", children = { Node(A) Node(B) })

Reduce() will consume the entire stack by default. This is inconvenient for more complicated
parsers. To make S safe, we would insert a "marker node" to indicate the point in the stack where
Reduce() should stop consuming.

ParseResultStream has methods to automatically push a marker node to each result it yields, or
reduce every result it yields, so a better implementation of S would be:

  ParseResultStream S(ParseResultStream prefixes) {
    return B(A(prefixes.Mark())).Reduce("S");
  }

The only issue with this is that it doesn't necessarily set the "ok" flag on ParseResultStream
correctly. Because the "ok" flag is set to the success of the last parser, this would return "ok" if
B returned "ok". But from a caller's perspective, the parse should only succeed if neither A nor B
required error correction, so we need to adjust our handling of the flag:

  ParseResultStream S(ParseResultStream prefixes) {
    auto a_result = A(prefixes.Mark());
    bool a_ok = a_result.ok();
    auto b_result = B(std::move(a_result)).Reduce("S");

    if (ok) {
    	return b_result;
    } else {
    	return b_result.Fail();
    }
  }

This pattern is somewhat unwieldy, so we implement several combinator functions to produce it
automatically. The NT ("NonTerminal") combinator takes a parser as input and automatically places a
Mark() before it and a Reduce after it, while the Seq combinator runs two parsers in sequence and
handles the ok flag correctly. With these tools, our S implementation becomes:

  ParseResultStream S(ParseResultStream prefixes) {
    return NT("S", Seq(A, B));
  }

## Error handling in a nutshell

Error handling relies on one invariant: ParseResultStream yields results in order of increasing
amount of error correction (measured as Levenshtein distance to the nearest correctly-parsing
string). That means that a correct parse will always come first, and that if there is no correct
parse, the parse requiring the least error correction will come first.

For the simple base parsers which parse single tokens, this is baked into the implementation. The
provided combinators guarantee this property by induction (i.e. if the input parsers have this
property, the combinator will produce a parser with this property).
