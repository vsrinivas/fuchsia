# abigen grammar

## Modified BNF rules

This is the grammar for abigen files. The grammar is expressed in
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
file = ( declaration )* , EOF ;

declaration = syscall-declaration ;

syscall-declaration = syscall IDENTIFIER attribute-list "(" parameter-list ")"
                      ( "returns" "(" type ( "," paramater-list ) ")" )

parameter-list = parameter | parameter "," parameter-list

parameter = IDENTIFIER ":" type attribute-list ( inout )

attribute-list = ( attribute )*

attribute = IDENTIFIER

type = IDENTIFIER

inout = "IN" | "OUT" | "INOUT"
```
