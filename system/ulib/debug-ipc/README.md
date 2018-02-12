# Debug IPC

This is the shared IPC code between the debug router and the client debugger.
It's not useful for random programs. Client debugging code should use the
client debug library.

This is a super simple custom IPC format because it is intended to be used
between two computers (unlike FIDL) and called at a very low level when
debugging the system (when higher-level primitives should be avoided).

## C++ Stuff

This library is shared between the client, which uses STL, and the userspace
target code which uses FBL.

The FBL can be used in host code, but mandating a container style for a larger
codebase from such a small stub seemed undesirable. The client uses many other
STL containers and fbl::string's immutability provides challenges for code
doing a lot of string manipulation that is not performance critical.

Since this shared library is so small, typedefs are provided that map to either
STL or FBL containers, depending on the compilation environment. The code here
must be written to the lowest common denominator of these two libraries.

## Protocol information

  * Structs are defined with sized types and serialized
    little-endian such that they can be memcpy'd on little-endian machines.

  * Vectors are serialized as a 32-bit count followed by the number of records
    serialized in the normal manner.

  * Strings are serialized as a 32-bit size followed by the string data.
