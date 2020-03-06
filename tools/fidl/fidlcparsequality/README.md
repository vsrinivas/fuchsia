# fidlcparsequality

fidlcparsequality is a simple tool which measures the quality of the error
reporting of the `fidlc` compiler. It works by taking a base library, and
running the compiler on successively mutated version of this base library,
then collecting and categorizing the errors reported by the compiler.

## Build and Run

    fx set core.x64 --with //tools/fidl/fidlcparsequality:host
    fx build

Then

    ./out/default/host_x64/fidlcparsequality --fidlc out/default/host_x64/fidlc

Example output

    runs: 7382, hardExits: 0, unknownErr 0
    ^Invalid library name component .*: 212
    ^Multiple struct fields with the same name;: 6
    ^cannot specify strictness for .*: 1
    ^invalid character .*: 27
    ^invalid identifier .*: 15
    ^unexpected identifier .*, was expecting .*: 1047
    ^unexpected token .*, was expecting .*: 2108
    ^unknown type .*: 2388
