Libdoc parses the documentation in FIDL files and tries to validate it as much as possible.

It ensures that the documentation syntax and grammar is consistent. It checks that all references
are correct.

# Source file

Libdoc doesn't parse directly the FIDL files. Instead, it uses the JSON IR generated from the FIDL
files.

# Lexer

Libdoc's goal is to parse documentation that means that the lexer is not a lexer for a programming
language but a lexer for the English language.

## Numbers

Numbers are consistent with a programming language. For example:

* 1234
* 0xabcd

## Identifiers

Identifiers can contain:

* letters.
* digits (including at the beginning of the identifier).
* underscores and dashes (the minus sign).
* single quotes.

If an identifier could be interpreted as a number, it is a number.

## Strings

String are sequences of characters between two single quotes or two double quotes.

Strings can contain new lines.

Strings cannot start with a space (in that case, it's a solo single quote or a solo double quote).

Strings can contain the backslash character. It hasn't any special meaning.
