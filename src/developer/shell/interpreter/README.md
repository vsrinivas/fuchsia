# Shell interpreter library

This library provides an interpreter for the Fuchsia Shell Language.

The library works at the AST level which means that the interpreter is independent from the syntax.
Only the semantic concepts are used.

# Shell interpreter server

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

# Interface between client and interpreter

The interface is described using a FIDL file: fidl/fuchsia.shell/shell.fidl

This interface allows the client to provide the definition of an AST using the AddNodes method.
This method defines some nodes and attaches them to their container. Because a FIDL message has a
limited size, you may not be able to define a whole AST with only one invocation. You can call
AddNodes several times to construct the full AST.

A container (a node which defines sub nodes) must always be defined after its children. Global
nodes are directly attached to the interpreter (for definition nodes) or to the context (for
instruction nodes).

# Interpreter AST

When the interpreter receives AST nodes (via AddNodes), it creates its own AST objects.

The base classes for the AST nodes are in: interpreter/src/nodes.h

All the nodes are defined in:
- interpreter/src/expressions.h
- interpreter/src/instructions.h
- interpreter/src/types.h
