# Debug IPC

This is the shared IPC code between the debug router and the client debugger.
It's not useful for random programs. Client debugging code should use the
client debug library.

This is a super simple custom IPC format because it is intended to be used
between two computers (unlike FIDL) and called at a very low level when
debugging the system (when higher-level primitives should be avoided).

## Protocol information

  * Structs are defined with sized types and serialized
    little-endian such that they can be memcpy'd on little-endian machines.

  * Vectors are serialized as a 32-bit count followed by the number of records
    serialized in the normal manner.

  * Strings are serialized as a 32-bit size followed by that number of 8-bit
    characters. No encoding is specified, the data is not null terminated.
