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

A thread holds a stack of 64 bit values which are used to hold temporary results when computing
expressions.

# Execution scopes (class ExecutionScope)

An execution scope hold the local state for a function or an iterator. The scopes are linked up to
the global execution scope (defined by the isolate). A scope is linked to exactly one other scope
(or no scope for the global scope). However, several scopes can be linked to the same scope (this
is, for example, the case for a function which uses two iterators).

An execution scope holds a buffer which is used to store all the persistent values for this scope
(local variables for a local scope, global variables for the global scope).

# Scopes (class Scope)

Scopes are used during the compilation to infer the variable, function, class, ... associated to a
name.

They can also be used for dynamic code when the name resolution can't be done during compilation.

# Values

The interpreter can manage a wide set of different values.

There are basically three different kinds of values:

*   Values that can be stored in a fixed size slot (for example, integers, Booleans).

*   Values that needs additional storage. This storage is referenced by a pointer which is stored
in a fixed size slot (for example, strings and objects).

*   Values that can hold any type of value (that means that such a value can hold an integer and,
later, hold a string). These values are stored using **shell::interpreter::Value** whose size is
fixed.

These values can be pushed to or popped from the thread's value stack (some values may take
several slots).

They can also be stored into the execution scope's buffer (case of global or local variables).

## Fixed size slot values

These values can be stored and copied just by writing a new value within the slot.

## Values with additional storage

These values are reference counted. The classes **ReferenceCountedBase**, **ReferenceCounted** and
**Container** are used to handle the reference counting.

The code class has a vector of **StringContainer** which keeps alive the strings directly
referenced by operations (string literals).

## Any type values

These values are stored using the **Value** class. It contains a type and a 64 bit union which can
hold the different value kinds:

*   **uint64_t** for a 64 bit unsigned integer

*   **String\*** for a string

For the string case, the value takes a reference when it is assigned and releases it when it is
destroyed (or overwritten).

## Strings

Strings are implemented using the classes **String** and **StringContainer**.

The class **String** holds the string. It is encoded using UTF-8 but users of the language can see
it as if all characters were encoded using code points. They are reference counted which means that
they are only copied on write.

The class **StringContainer** automatically manages a **String** (the reference).

## Objects and Object Schemas

An object schema defines a specific referenced type and holds information about how it is
implemented (e.g., its memory layout).  Object schemas are defined by the class **ObjectSchema**.
An object is an instance of the type described by an object schema.  Objects are represented in
memory by the class **Object**.

Every object must have an associated schema; as a result, when defining a new object using the
wire format, users must either refer to an existing schema, or provide a schema for it.

The AST represents objects with **ObjectDeclarations** that have **ObjectFields**.
Similarly, **ObjectSchemas** have **ObjectFieldSchemas**.  Each **ObjectField** has an associated
**ObjectFieldSchema**.

When instantiating a new object with a given schema, the current implementation creates an
object by allocating a buffer large enough to hold both the instance of class **Object** (which
contains a pointer to the object's **ObjectSchema**), and the object's fields.  The
interpreter will then compute the initial values of the fields, and set them appropriately.

The contents of an object are stored in the same way that values are stored in the stack: they may
have fixed-sized slot values, values that require additional storage, and any values.  A given
field's offset in the buffer is described in the **ObjectFieldSchema** associated with that field.
The implementation currently uses the platform alignment for all types (e.g., int32s are 4-byte
aligned, reference types, being backed by pointers, are 8-byte aligned, and so forth).

# Basic arithmetic operations

For the basic arithmetic operations (=, \-, \*, /, %) we have the usual implementation which
truncates values which overflow or underflow. We also have a version which abort when an overflow
or an underflow are found.

For the addition, we have three variants of the instructions and, for each variant 4 versions.

The variants are:

*   addition with truncation (which works with signed and unsigned integers).

*   addition of signed values with an exception generated in case of overflow/underflow.

*   addition of unsigned values with an exception generated in case of overflow.

The versions are:

*   8 bit integers

*   16 bit integers

*   32 bit integers

*   64 bit integers

# Assignments

When an object is assigned to a variable or a field, we only assign a reference. That means that
two variables or fields can reference the same data in memory even if, semantically, this data
represents two different objects.

When we need to assign a field of an object which has more than one reference, we need to do a copy
on write (COW). That means that we do a simple copy of the object (not a deep copy: all the fields
in the copy have the same value as the original). Then, we are able to assign the field.

## Considered implementation for COW

For assignment of fields, we have two operations. First we need to get the value (the internal
reference) of the object. We can get the value from a variable or from a field. If the value we
extract has more than one link, that means we need to do a copy. Second, when the potential copy
is done, we assign the field as usual.

To do a copy, we need to recursively do the copy.
When we have:
a.x.y = 2

If the object referenced by "a.x" has more than one link, we need a copy of "a.x". That means
assigning a new value to "a.x".

To be able to assign a new value to "a.x", if the object referenced by "a" has more than one link,
we need to need a copy of the object referenced by "a".

In that final case, the operations will be:

Do a copy of "a". This is not a deep copy: all the fields in "a" (including those that reference
an object) have the same value in the copy.

Assign the copy to "a"

Do a copy of "a.x" (which is also a copy of "(copy of a).x"

Assign "a.x" with the copy of "a.x"

Assign "a.x.y"

From an opcode point of view, you will have only two opcodes.

*   get a writable reference on a.x (which will be a recursive operation)

*   assign the field "y" of the writable reference

We may have one opcode to access an object for reading. Then, we may have an opcode to access an
object for writing. This will compute the pointer to the object and, most of all, the source for
the object. If we need to access several fields we will have a linked list of all the accesses.

Then will will probably have an opcode which will tell "I want to write to this object computed using the opcode field access for writing" which will use the linked list to perform the needed
copies.

After eventually duplicating some objects, this will free the linked list and return a pointer to an object which is writable.
