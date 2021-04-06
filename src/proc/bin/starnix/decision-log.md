## Why implement custom ELF loading instead of using process_builder?

There are many differences between how process_builder creates processes and how Linux creates processes.
- The stack is not initialized, and startup info is passed through a channel message. Linux passes this information on the initial stack. Starnix can fix up the stack after it is created, so this isn't a deal breaker.
- If the ELF includes an interpreter, process_builder loads only the interpreter and entirely skips loading the main executable. Linux loads both.
- process_builder loads the executable into a sub-VMAR of the address space. This makes the implementation of mprotect more complicated since it would need to look up the correct VMAR to call zx_vmar_protect on, instead of simply using the root VMAR.

We can still reuse the code in process_buildler::{elf_load, elf_parse} though.

