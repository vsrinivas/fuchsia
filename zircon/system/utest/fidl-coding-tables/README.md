# FIDL Coding Tables Tests

The FIDL compiler generates coding tables, which are instructions for the walker/visitor
to encode/decode messages. This test suite runs the FIDL compiler at compile time and
verifies important properties of the generated coding table at run time. Its purpose
is to test the tables generator in fidlc in a flexible manner.

For example, given the following FIDL definition:

```fidl
library example;
xunion MyMessage {
    int32 a;
    int64 b;
    SomeStruct c;
};
```

It is expected to generate coding tables along the lines of:

```cpp
static const ::fidl::FidlXUnionField example_MyMessageFields[] = {
    ::fidl::FidlXUnionField(&fidl::internal::kInt32Table, [... xunion ordinal for a ...]),
    ::fidl::FidlXUnionField(&fidl::internal::kInt64Table, [... xunion ordinal for b ...]),
    ::fidl::FidlXUnionField(&example_SomeStructTable, [... xunion ordinal for c ...])
};
const fidl_type_t example_MyMessageTable =
    fidl_type_t(::fidl::FidlCodedXUnion(3, example_MyMessageFields, "example/MyMessage"));
```

The test will check that the pointers in the coding tables are as expected.
