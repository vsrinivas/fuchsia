The `page_tables` library exists purely to share constants and macros between
the kernel and user-space. This is useful when implementing a virtual machine
manager, or for any similar tasks where user-space needs to be aware of the
structure of page tables.
