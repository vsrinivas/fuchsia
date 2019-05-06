# Grammar

## Modified BNF rules

This is the grammar for FIDL source files. The grammar is expressed in
a modified BNF format.

A nonterminal symbol matches a sequence of other symbols, delimited by
commas.
```
nonterminal = list , of , symbols ;
```

Some symbols are terminals, which are either in all caps or are in
double quotes.
```
another-nonterminal = THESE , ARE , TERMINALS , AND , SO , IS , "this" ;
```

Alternation is expressed with a pipe.
```
choice = this | that | the-other ;
```

An option (zero or one) is expressed with parentheses.
```
optional = ( maybe , these ) , but , definitely , these ;
```

Repetition (zero or more) is expressed with parentheses and a star.
```
zero-or-more = ( list-part )* ;
```

Repetition (one or more) is expressed with parentheses and a plus.
```
one-or-more = ( list-part )+ ;

```

## Tokens

Whitespace and comments are ignored during lexing, and thus not
present in the following grammar. Comments are C++-style `//` until
the end of the line.

TODO(US-238): Eventually comments will be read as part of a
documentation generation system.

## The grammar

`file` is the starting symbol.

```
file = library-header , ( using-list ) , declaration-list ;

library-header = ( attribute-list ) , "library" , compound-identifier , ";" ;

using-list = ( using | using-declaration )* ;

using-declaration = "using" , IDENTIFIER ,  "=" , type-constructor , ";" ;

declaration-list = ( declaration , ";" )* ;

compound-identifier = IDENTIFIER ( "." , IDENTIFIER )* ;

using = "using" , compound-identifier , ( "as" , IDENTIFIER ) , ";" ;

declaration = bits-declaration | const-declaration | enum-declaration | protocol-declaration
            | struct-declaration | table-declaration | union-declaration | xunion-declaration ;

const-declaration = ( attribute-list ) , "const" , type-constructor , IDENTIFIER , "=" , constant ;

enum-declaration = ( attribute-list ) , "enum" , IDENTIFIER , ( ":" , type-constructor ) ,
                   "{" , ( bits-or-enum-member , ";" )+ , "}" ; [NOTE 1]

bits-declaration = ( attribute-list ) , "bits" , IDENTIFIER , ( ":" , type-constructor ) ,
                   "{" , ( bits-or-enum-member , ";" )+ , "}" ; [NOTE 2]

bits-or-enum-member = ( attribute-list ) , IDENTIFIER , "=" , bits-or-enum-member-value ;

bits-or-enum-member-value = IDENTIFIER | literal ; [NOTE 3]

protocol-declaration = ( attribute-list ) , "protocol" , IDENTIFIER ,
                       "{" , ( protocol-member , ";" )*  , "}" ;

protocol-member = protocol-method | protocol-event | protocol-compose ;

protocol-method = ( attribute-list ) , IDENTIFIER , parameter-list,
                  ( "->" , parameter-list , ( "error" type-constructor ) ) ; [NOTE 4]

protocol-event = ( attribute-list ) , "->" , IDENTIFIER , parameter-list ;

parameter-list = "(" , ( parameter ( "," , parameter )+ ) , ")" ;

parameter = type-constructor , IDENTIFIER ;

protocol-compose = "compose" , compound-identifier ;

struct-declaration = ( attribute-list ) , "struct" , IDENTIFIER , "{" , ( struct-field , ";" )* , "}" ;

struct-field = ( attribute-list ) , type-constructor , IDENTIFIER , ( "=" , constant ) ;

union-declaration = ( attribute-list ) , "union" , IDENTIFIER , "{" , ( union-field , ";" )+ , "}" ;

xunion-declaration = ( attribute-list ) , "xunion" , IDENTIFIER , "{" , ( union-field , ";" )* , "}" ;

union-field = ( attribute-list ) , type-constructor , IDENTIFIER ;

table-declaration = ( attribute-list ) , "table" , IDENTIFIER , "{" , ( ( attribute-list ) , table-field , ";" )* , "}" ;

table-field = ( attribute-list ) , table-field-ordinal , table-field-declaration ;

table-field-ordinal = ordinal , ":" ;

table-field-declaration = struct-field | "reserved" ;

attribute-list = "[" , attributes , "]" ;

attributes = attribute | attribute , "," , attributes ;

attribute = IDENTIFIER , ( "=" , STRING-LITERAL ) ;

type-constructor = compound-identifier ( "<" type-constructor ">" ) , (  type-constraint ) , ( "?" )
                 | handle-type ;

handle-type = "handle" , ( "<" , handle-subtype , ">" ) , ( "?" ) ;

handle-subtype = "bti" | "channel" | "debuglog" | "event" | "eventpair"
               | "fifo" | "guest" | "interrupt" | "job" | "port" | "process"
               | "profile" | "resource" | "socket" | "thread" | "timer"
               | "vmar" | "vmo" ;

type-constraint = ":" , constant ;

constant = compound-identifier | literal ;

ordinal = NUMERIC-LITERAL ;

literal = STRING-LITERAL | NUMERIC-LITERAL | "true" | "false" ;
```
----------

### NOTE 1
The `enum-declaration` allows the more liberal `type-constructor` in the
grammar, but the compiler limits this to signed or unsigned integer types,
see [primitives].

### NOTE 2
The `bits-declaration` allows the more liberal `type-constructor` in the grammar, but the compiler
limits this to unsigned integer types, see [primitives].

### NOTE 3
The `bits-or-enum-member-value` allows the more liberal `literal` in the grammar, but the compiler limits this to:
* A `NUMERIC-LITERAL` in the context of an `enum`;
* A `NUMERIC-LITERAL` which must be a power of two, in the context of a `bits`.

### NOTE 4
The `protocol-method` error stanza allows the more liberal `type-constructor`
in the grammar, but the compiler limits this to an `int32`, `uint32`, or
an enum thereof.

<!-- xrefs -->
[primitives]: /docs/development/languages/fidl/reference/language.md#primitives

