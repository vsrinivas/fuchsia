# Driver Binding

## Bind Libraries

A bind library defines a set of properties that drivers may assign to their children. Also,
bind programs may refer to bind libraries.

TODO(fxb/35932): Flesh out this documentation.
TODO(fxb/36103): Implement and document comments.

```
library = library-header , using-list , declaration-list ;

library-header = "library" , compound-identifier , ";" ;

using-list = ( using , ";" )* ;

using = "using" , compound-identifier , ( "as" , IDENTIFIER ) ;

compound-identifier = IDENTIFIER ( "." , IDENTIFIER )* ;

declaration-list = ( declaration , ";" )* ;

declaration = primitive-declaration | enum-declaration ;

primitive-declaration = ( "extend" ) , type , IDENTIFIER ,
                        ( "{" primitive-value-list "}" ) ;

type = "uint" | "string" | "bool";

primitive-value-list = ( IDENTIFIER , "=" , literal , "," )* ;

enum-declaration = ( "extend" ) , "enum" , IDENTIFIER ,
                   ( "{" , enum-value-list , "}" ) ;

enum-value-list = ( IDENTIFIER , "," )* ;

literal = STRING-LITERAL | NUMERIC-LITERAL | "true" | "false" ;
```

An identifier matches the regex `[a-zA-Z]([a-zA-Z0-9_]*[a-zA-Z0-9])?` and must not match any
keyword. The list of keywords is:

```
bool
enum
extend
library
string
uint
using
```

A string literal matches the regex `”[^”]*”`, and a numeric literal matches the regex `[0-9]+` or
`0x[0-9A-F]+.`
