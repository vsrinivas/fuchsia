# An implementation of the audio driver protocol

This library is a C++ implementation of the Zircon Audio Stream and Ring Buffer
driver protocols suitable for use in simple audio drivers where the codec and
DAI functionality are contained in the same driver.

It handles most of the boilerplate of publishing and shutting down devices,
handling client connections, and FIDL-serialization/de-serialization/validation of
protocol messages as well as enforcing some of the protocol state requirements.

Generally speaking, users of the library should only need to subclass the
SimpleAudioStream, provide details about their capabilities during Init, then
respond to messages in their overloaded methods.  Hopefully, this allows authors
of simple audio drivers more time to focus on banging on registers, and less
time on manually validating messages and protocol state.
