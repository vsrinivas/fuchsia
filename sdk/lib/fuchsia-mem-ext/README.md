# fuchsia-mem

This library provides utility routines for interacting with the FIDL
fuchsia.mem/Data type. This type is used to represent a variable amount
of data that may be stored inline in a FIDL message or out of line in
a separate VMO object. The routines in this library help convert data
between an application's representation and the FIDL representation
in a transparent and simple way.

## Converting bytes to fuchsia.mem/Data

To convert some data into a fuchsia.mem/Data instance, use the `CreateWithData`
function. This will select a storage type based on the size of data. This can
source the data either from a cpp20::span or an std::vector. Pass a std::vector
in if the data is already stored in a vector of the appropriate type and is no
longer needed after creation as this can avoid a copy if the data is stored
inline. Otherwise, provide a reference to the storage via cpp20::span.

## Extracting data from a fuchsia.mem/Data

To extract data out of a fuchsia.mem/Data instance, use the `ExtractData` function.
This provides an std::vector containing the data, either extracted directly out
of inline storage or copied into a newly allocated vector.