# Driver for AMLogic g12 audio

This folder contains drivers for the AMLogic g12 audio subsystem. There are drivers available for
the [Audio Streaming Interface](//docs/concepts/drivers/driver_interfaces/audio_streaming.md)
implemented in audio-stream.cc and for the
[Digital Audio Interface](//docs/concepts/drivers/driver_interfaces/audio_dai.md) implemented in
dai.cc.

See [Audio Codec Interface](//docs/concepts/drivers/driver_interfaces/audio_codec.md) for a
description of codec terms used in this driver, and
[DAI interface](//docs/concepts/drivers/driver_interfaces/audio_dai.md) for the DAI terms used in
this driver.
See [audio.h](//src/lib/ddktl/include/ddktl/metadata/audio.h) for descriptions of audio metadata.
See [aml-audio.h](//src/devices/lib/amlogic/include/soc/aml-common/aml-audio.h) for descriptions of
AMLogic specific metadata.

