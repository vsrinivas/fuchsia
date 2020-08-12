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

In the board configuration file, instantiate a composite codec device with binding rules for the
codec protocol, and vendor and device ids, for instance:

zx_bind_inst_t codec_match[] = {
  BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_CODEC),
  BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_TI),
  BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_TI_TAS2770),
};

If there are more than one instance of the same codec in the system, also specify a non-zero
BIND_CODEC_INSTANCE value also returned by the each instance of the driver in
DriverIds.instance_count, for instance:

zx_bind_inst_t codec_woofer_match[] = {
  BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_CODEC),
  BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_TI),
  BI_ABORT_IF(NE, BIND_PLATFORM_DEV_DID, PDEV_DID_TI_TAS5720),
  BI_MATCH_IF(EQ, BIND_CODEC_INSTANCE, 1),
};
