# Debug IPC

This is the shared IPC code between the debug agent (code in
[../debug_agent](../debug_agent)) and the zxdb frontend (code in
[../zxdb](../zxdb)). It's not useful for other programs. Client debugging code
should use the client debug library in [../zxdb/client](.,./zxdb/client).

This is a super simple custom IPC format because it is intended to be used
between two computers (unlike FIDL) and called at a very low level when
debugging the system (when higher-level primitives should be avoided). The
goal is to replace it with a more robust IPC library when one is provided for
the system that can communicate off-device.

## Protocol information

  * Structs are defined with sized types and serialized
    little-endian such that they can be memcpy'd on little-endian machines.

  * Vectors are serialized as a 32-bit count followed by the number of records
    serialized in the normal manner.

  * Strings are serialized as a 32-bit size followed by that number of 8-bit
    characters. No encoding is specified, the data is not null terminated.
