# An implementation of the audio codec protocol

This library is a C++ implementation of the audio codec protocol for both servers and clients.
The goal is to facilitate simple codec drivers writing and usage.

On the server side, i.e. the audio codec driver, this library simplifies publishing the composite
device, and provides C++ hooks to be overridden by the driver implementing the protocol.
Users of the library should only need to subclass the SimpleCodecServer class and implement
Initialization and Shutdown methods, and respond to protocol messages in their overloaded methods.

On the client side, i.e. the audio DAI controller driver communicating with an audio codec, this
library provides C++ synchronous versions of the protocol functions. Users of the library
should intantiate a SimpleCodecClient class and use it to query the codec capabilities and configure
it by calling the appropiate methods.
