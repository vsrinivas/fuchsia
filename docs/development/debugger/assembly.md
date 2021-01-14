# Assembly language in zxdb

Zxdb supports the following commands for dealing with assembly language:

  * `disassemble` / `di`: Disassemble at the current location (or a given location)

  * `nexti` / `ni`: Step to the next instruction, stepping over function calls.

  * `stepi` / `si`: Step the next instruction, following function calls.

  * `regs`: Get the CPU registers.

zxdb maintains information about whether the last command was an assembly command or a source-code
and will show that information on stepping or breakpoint hits. To switch to assembly-language mode,
type `disassemble`, and to switch back to source-code mode, type `list`.

