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

*   interpreter/src/expressions.h

*   interpreter/src/instructions.h

*   interpreter/src/types.h

# Interpreter Bytecode

The interpreter AST is compiled to a bytecode which is then interpreted. The bytecode is very low
level with operations which are close to an assembler. In particular, values are stored
independently from their type. Only their size is used. That means that a 64 bit floating-point and
a 64 bit integer are handled the same way when they are loaded and stored.

All the operations handled by the code interpreter are defined in interpreter/src/code.h

# Value stack

Because the interpreter doesn't use physical registers directly and, therefore, we don't need to
spill/restore registers, we use a stack based approach. This is very efficient when we generate the
pseudo assembler operations and, it saves space when these operations are stored in memory (no
need to specify on which registers we are working).

Each operation consumes zero, one or several values from the stack and pushes zero, one or several
values to the stack.

# Isolates

An isolate refers to a component execution or a process. Only code from the isolate can modify
data in the isolate which means that code from an isolate cannot modify data from another isolate.

Each time a command line interface is launched, it has it's own isolate associated.

To communicate between isolates you can send FIDL messages (or you can share VMOs).

An explicit connection can be done between two isolates using a pipe. In that case the two isolates
can exchange data using the pipe.

An isolate has some definitions (classes, functions) and has a global scope which holds global
variables.

# Execution contexts

The execution context is a concept used by the command line interface. The main goal of the
execution context is to be able to associate errors and results to a particular input from the
user.

This is particularly useful when the user executes several things in parallel to avoid missing the
output.

An execution context is associated to at least one isolate (the isolate of the command line
interface). If other processes or components are launched by an execution context, they will share
the execution context but will have different isolates (and should communicate using a pipe).

Each execution context launches at least one thread.

# Threads

The shell is massively parallel. That means that, at the same time, we may need several threads to
be defined for a single program/task.

Each thread can execute in parallel with other threads. However, because our programs are
asynchronous, most of the time we will only need one physical thread to execute all of them. A
first thread executes and is stopped waiting for an event, another thread can be executed within
the same physical thread. If several threads should run at the same time (it's, for example, the
case when you have threads which do heavy computation) then, we can allocate more than one physical
thread for a program/task.

Thread are ephemeral. They are created as needed and destroyed when the execution is done. Each one
holds a value stack (equivalent to the register set for a register based assembler) which is used
to compute expressions. They all share the global scope and they may share some local scopes.

For example, if you have some code which uses an iterator (a function which returns some results).

Before using the iterator, we have only one thread defined. When we start using the iterator, we
create another thread within the same scope. Both the code using the iterator and the iterator will
run in parallel each one in its own thread. When the iterator is finished, its thread is destroyed
and we only have one thread left.

Threads are associated to one isolate (which provides functions and global data) and to one
execution context (which provide a way to send errors and results).

# Execution scopes (class ExecutionScope)

An execution scope hold the local state for a function or an iterator. The scopes are linked up to
the global execution scope (defined by the isolate). A scope is linked to exactly one other scope
(or no scope for the global scope). However, several scopes can be linked to the same scope (this
is, for example, the case for a function which uses two iterators).

# Scopes (class Scope)

Scopes are used during the compilation to infer the variable, function, class, ... associated to a
name.

They can also be used for dynamic code when the name resolution can't be done during compilation.
