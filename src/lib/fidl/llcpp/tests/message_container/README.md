# Message container tests

These cover the various LLCPP "message" classes and utilities, which are
responsible for encoding/decoding FIDL values and sometimes allocating storage
for them:

  - `fidl::OutgoingMessage`
  - `fidl::OutgoingByteMessage`
  - `fidl::OutgoingIovecMessage`
  - `fidl::OwnedEncodedMessage<Foo>`
  - `fidl::UnownedEncodedMessage<Foo>`
  - `fidl::OutgoingMessageAdaptorFromC`
  - `fidl::OutgoingToIncomingMessage`
  - ...

