# CMD

`cmd` is a basic command interpreter for Fuchsia. It provides basic
line-editing facilities and can invoke command-line programs.

Eventually, `cmd` will be replaced by a command interpreter that
interacts directly with Fuchsia system concepts, such as kernel
objects, components, and services.

## Syntax

### Tokens

Tokens are separated by whitespace, which are space (U+20), tab (U+09),
carriage return (U+0D), and newline characters (U+0A) and cannot contain
`"` (U+22).

### Comments

A token that begins with a `#` (U+23) begins a comment. Comments cause the
current token and all subsequent tokens to be ignored.

### Quoting

A token that begins with a `"` (U+22) is a quoted token. Within a quoted
token a `\` (U+5C) initiates an escape sequence. The following escape
sequences are available:

| Sequence | Value |
|----------|-------|
| `\t`     | U+09  |
| `\n`     | U+0A  |
| `\r`     | U+0D  |
| `\"`     | U+22  |
| `\\`     | U+5C  |

Rather than terminating by whitespace, a quoted token terminates at the first
unescaped A `"` (U+22), which must be followed either by whitespace or by the
end of the command.

## Built-in commands

### cd

Syntax: `cd PATH`

If one argument is provided, changes the current working directory to the given
path.

It is an error to supply more or fewer than one argument.

### getenv

Syntax: `getenv [VARIABLE_NAME]`

If zero arguments are provided, prints all the variables in the current
environment.

If one argument is provided, prints the value of that environment variable with
the given name.

It is an error to supply more than one argument.

### setenv

Syntax: `setenv VARIABLE_NAME VARIABLE_VALUE`

If two arguments are provided, sets the value of the environment variable with
the given name to the given value.

It is an error to supply more or fewer than two arguments.

### unsetenv

Syntax: `unsetenv ENVIORNMENT_VARIABLE`

Removes the given `ENVIORNMENT_VARIABLE` from the current environment.

### quit, exit

Exit the interpreter. The interpreter always exits with a return code of 0.

## External commands

If a command is not implemented internally, then `cmd` will run the command by
running an executable with the given arguments.

### Resolution

`cmd` finds executables by resolving the command name (the first token in the
command sequence) against the value of the `PATH` environment variable, split
on `:` (U+3A).

### Execution

To run an executable, `cmd` creates a child job and runs the executable in a
new process within that job with the same namespace as `cmd` itself.

## Variable expansion

`cmd` does not support variable expanstion.

## Control flow

`cmd` does not support any control flow.
