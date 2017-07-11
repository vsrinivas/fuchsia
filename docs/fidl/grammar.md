# fidl grammar

## Modified BNF rules

This is the grammar for fidl source files. The grammar is expressed in
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

## The grammar

`file` is the starting symbol.

```
file = module-header , using-list , declaration-list ;

module-header = "module" , compound-identifier ;

using-list = ( using )* ;

declaration-list = ( declaration )* ;

compound-identifier = IDENTIFIER ( "." , IDENTIFIER )* ;

using = "using" , compound-identifier , ( "as" , IDENTIFIER ) ;

declaration = const-declaration | enum-declaration | interface-declaration |
              struct-declaration | union-declaration ;

const-declaration = "const" , type , IDENTIFIER , "=" , constant ;

enum-declaration = "enum" , IDENTIFIER , ( ":" , integer-type ) ,
                   "{" , ( enum-member , ";" )* , "}" ;

enum-member = IDENTIFIER , ( "=" , enum-member-value ) ;

enum-member-value = IDENTIFIER | NUMERIC-LITERAL ;

interface-declaration = "interface" , IDENTIFIER ,
                        "{" , ( interface-member , ";" )*  , "}" ;

interface-member = method | const-declaration | enum-declaration ;

interface-method = ordinal , ":" , IDENTIFIER , parameter-list ,
                   ( "->" , parameter-list ) ;

parameter-list = "(" , parameters , ")" ;

parameters = parameter | parameter , "," , parameter-list ;

parameter = type , IDENTIFIER ;

struct-declaration = "struct" , IDENTIFIER ,
                     "{" , ( struct-member , ";" )* , "}" ;

struct-member = struct-field | const-declaration | enum-declaration ;

struct-field = type , IDENTIFIER , ( "=" , constant ) ;

union-declaration = "union" , IDENTIFIER , "{" , ( union-member , ";" )* , "}" ;

union-member = union-field | const-declaration | enum-declaration ;

union-field = type , IDENTIFIER ;

type = identifier-type | array-type | vector-type | string-type | handle-type
                       | request-type | primitive-type ;

identifier-type = compound-identifier , ( "?" ) ;

array-type = "array" , "<" , type , ">" , ":" , constant ;

vector-type = "vector" , "<" , type , ">" , ( ":" , constant ) , ( "?" ) ;

string-type = "string" , ( ":" , constant ) , ( "?" ) ;

handle-type = "handle" , ( "<" , handle-subtype , ">" ) , ( "?" ) ;

handle-subtype = "process" | "thread" | "vmo" | "channel" | "event" | "port" |
                 "interrupt" | "iomap" | "pci" | "log" | "socket" |
                 "resource" | "eventpair" | "job" | "vmar" | "fifo" |
                 "hypervisor" | "guest" | "timer" ;

request-type = "request" , "<" , compound-identifier , ">" , ( "?" ) ;

primitive-type = integer-type | "bool" | "float32" | "float64" ;

integer-type = "int8" | "int16" | "int32" | "int64" |
               "uint8" | "uint16" | "uint32" | "uint64" ;

constant = compound-identifier | literal ;

literal = STRING-LITERAL | NUMERIC-LITERAL | TRUE | FALSE | DEFAULT ;
```
