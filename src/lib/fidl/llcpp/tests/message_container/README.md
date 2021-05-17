# Message container tests

These cover the various LLCPP "message" classes and utilities, which are
responsible for encoding/decoding FIDL values and sometimes allocating storage
for them:

  - `fidl::IncomingMessage`
  - `fidl::OutgoingMessage`
  - `fidl::OutgoingMessage`
  - `fidl::OutgoingIovecMessage`
  - `fidl::OwnedEncodedMessage<Foo>`
  - `fidl::UnownedEncodedMessage<Foo>`
  - `fidl::OutgoingToIncomingMessage`
  - ...

In addition, the `fidl::Result` result/error types are also tested here, since
a major use case is them being composed by message container types.
