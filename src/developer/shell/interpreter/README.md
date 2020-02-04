# shell interpreter library

This library provides an interpreter for the Fuchsia Shell Language.

The library works at the AST level which means that the interpreter is independent from the syntax.
Only the semantic concepts are used.

# shell interpreter server

This server provides a service for interpreting shell programs. It can be used to run a program or
it can be used by an interactive shell.

The service allows the client to specify AST nodes like namespaces, classes, function.

Then the service allows the client to create an execution context, add some instructions (which are
AST nodes) to the context and, finally, execute those instructions (which can refer to the AST
nodes previously added).

When the client want to run a program, there is only one context which usually contains only one
instruction (call a method main with the specified arguments).

When the client is an interactive shell, usually it creates one execution context per line typed by
the user.