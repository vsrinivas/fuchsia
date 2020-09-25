# Audio Codec Interface

This document describes the codec interface between controllers and codecs in
Fuchsia. It is meant to serve as a reference for driver-authors, and to define
the interface contract which codec drivers must implement and that controllers
can use. The codec interface is a Banjo protocol exposed by codec drivers.

## Notation and Terminology

In this document:

-   All indices start from 0.
-   Vectors of n elements are represented as `<x0,x1,...,xn-1>`, for example a
    vector with two elements 5 and 6 as `<5,6>`.
-   Vectors can be nested, i.e. `<<5,6>,<7,8>>` represents a vector with 2
    vectors in it.

| Term       | Definition                                                     |
| ---------- | -------------------------------------------------------------- |
| Codec      | A real or virtual device that encodes/decodes a signal from    |
:            : digital/analog to/from analog/digital including all            :
:            : combinations, e.g. digital to digital. Example codecs include  :
:            : DAC-Amplifiers combos and ADC converters.                      :
| Controller | The part of a system that manages the audio signals, for       |
:            : example an SOC's audio subsystem or an independent sound card. :
| DAI        | Digital Audio Interface. Interface between controllers and     |
:            : Codecs. For example an I2S or HDA link.                        :

## Basic Operation

The functionality provided by the codecs is divided into:

-   [Main controls](#main-controls)
-   [Bridged Mode](#bridged-mode)
-   [DAI format](#dai-format)
-   [Gain control](#gain-control)
-   [Plug detect](#plug-detect)
-   [Power control](#power-control)
-   [Peripheral control](#peripheral-control)
-   [Signal processing control](#signal-processing-control)
-   [Content protection](#content-protection)

The controller is responsible for configuring and controlling the codecs. Codecs
advertize capabilities and a controller determines how they are used as
described below. The controller can control the codec's state, such as through the
reset function. A reset is required to get codecs to an initialized state.
Note that the codec drivers are expected to perform their own shutdown, just like
any other driver.

Codecs are composite devices that provide the codec protocol to controllers. It
is expected that only one controller uses a codec's protocol, and one controller
may use multiple codecs at once.

The [simple-codec](/src/media/audio/lib/simple-codec/README.md) library facilitates
writing and using simple codec drivers implementing the codec protocol.

## Protocol definition

The codec protocol is defined in [Banjo](/docs/development/drivers/banjo-tutorial.md) at
[ddk.protocol.codec](/sdk/banjo/ddk.protocol.codec/codec.banjo).

Many codec protocol operations are "fire-and-forget", i.e. they do not expect a
reply. Codec protocol operations with a reply are not considered completed until
the reply of the function is received, and not considered completed successfully
unless the reply contains a status `ZX_OK`.

### Main Controls {#main-controls}

A codec can be reset by a controller at any time by issuing the `Reset`
function.

The `GetInfo` function retrieves information from the codec including:

1.  A unique and persistent identifier for the codec unit, e.g. a serial number
    or connection path.
1.  The manufacturer name.
1.  The product name.

The codec operation can be started and stopped at any time with the `Start` and
`Stop` functions. By default the codec state is stopped, so 'Start' must be
issued at least once for the codec to be fully operational. Stopping the codec
operation can be used for example to change the DAI configuration safely avoiding
glitches or errors in the codec operation.

### Bridged Mode {#bridged-mode}

Before specifying the DAI format the controller must query the codec for its
bridging capabilites. If the codec is bridgeable, then the controller must
enable or disable bridging based on its knowledge of the system configuration.
Note that this is a singular property of a codec, i.e. a codec either supports
bridging or not, and it can be set in bridged mode or not. This protocol allows
configuring as bridged only 2 channel stereo codecs, with the 2 outputs of the
codec electrically bridged.

### DAI Format {#dai-format}

The DAI Format related protocol functions allow the codec to list its supported
formats for the DAI. The supported formats may include multiple sample formats,
rates, etc. Each codec advertises what it can support and the controller
mandates what DAI Format is to be used for each codec.

To find out what formats are supported by a given codec, the controller uses the
`GetDaiFormats` function. The codec replies with a vector of
`DaiSupportedFormats`, where each `DaiSupportedFormats` includes:

1.  A vector of number of channels. This lists the number of channels supported
    by the codec, for example `<2,4,6,8>`. A stereo codec reports a vector with
    one element `<2>`. Note that a codec that takes one channel and outputs its
    contents in all its outputs (e.g. 2 for a stereo amplifier) would report a
    vector with one element `<1>`, if it supports either one or two input
    channels, it would report a vector with two elements `<1,2>`.
2.  A vector of sample formats. DAI sample formats, e.g. `PCM_SIGNED`.
3.  A vector of frame formats, for example `STEREO_LEFT` and `STEREO_RIGHT`.
4.  A vector of rates. Frame rates, for example 44100, 48000, and 96000.
5.  A number of bits per channel. Number of bits in each channel in the DAI,
    e.g. 32 bits per channel.
6.  A vector of bits per sample. Sample widths, e.g. 24 bits per sample.

When not all combinations supported by the codec can be described with one
`DaiSupportedFormats`, the codec returns more than one `DaiSupportedFormats` in
the returned vector.

For example, if one `DaiSupportedFormats` allows for 32 bits samples at 48KHz,
and 16 bits samples at 96KHz, but not 32 bits samples at 96KHz, then the codec
will reply with 2 `DaiSupportedFormats`: `<<32bits>,<48KHz>>` and
`<<16bits>,<96KHz>>`. For simplicity, this example ignores parameters other than
rate and bits per sample. In the case where the codec supports either 16 or 32
bits samples at either 48 or 96KHz, the codec would reply with 1
`DaiSupportedFormats`: `<<16bits,32bits>,<48KHz,96KHz>>`.

Additionally, it is assumed that bits per sample is always smaller or equal to
bits per channel. Hence, a codec can report
`<<16bits_per_channel,32bits_per_channel>,<16bits_per_sample,32bits_per_sample>>`
and this does not imply that it is reporting that 32 bits per sample on 16 bits
samples is valid, it specifies only the 3 valid combinations:

1.  16 bits channels with 16 bits samples
2.  32 bits channels with 32 bits samples
3.  32 bits channels with 16 bits samples

Using the information provided by the codec in `IsBridgeable` and
`GetDaiFormat`, what is supported by the controller, and any other requirements,
the controller specifies the format to use in the DAI with the `SetDaiFormat`
function. This functions takes a parameter that specifies:

1.  A number of channels. This is the number of channels to be used in the DAI
    (for instance number of channels on a TDM bus, i.e. "on the wire"). For I2S
    this must be 2.
2.  A vector of channels to use. These are the channels in the DAI to be used by
    the codec. For I2S this must be a vector with 2 indexes `<0,1>`, i.e. both
    left and right channels are used. In bridged mode this will list only the
    one channel to be used by the codec, for example a codec’s stereo amplifier
    output bridged into one electrical mono output from the right channel of an
    I2S DAI would list only channel `<1>`. If not bridged, a codec with multiple
    electrical outputs that is configured with one channel in `SetDaiFormat` is
    expected to replicate the samples in this mono input on all its outputs.
3.  A sample format.
4.  A frame format.
5.  A frame rate.
6.  A number of bits per channel.
7.  A number of bits per sample.

Once `SetDaiFormat` is successful, the DAI format configuration is considered
completed and samples can be sent across the DAI.

TODO(andresoportus): Add DAI format loss notification support once asynchronous
notifications are added to Banjo.

### Gain Control {#gain-control}

Gain related support by any given codec is returned by the codec in response to
a `GetGainFormat` function in the `GainFormat` structure. The controller can
control gain, mute and AGC states in a codec using the `SetGainState` function
and the corresponding `GetGainState` function allows retrieving the current
state for the same.

### Plug Detect {#plug-detect}

The controller can query the plug detect state with the `GetPlugState` function.
The plug state includes hardwired and plugged states.

TODO(andresoportus): Add `can_notify` bool to `PlugState` once asynchronous
notifications are added to Banjo.

### Power Control {#power-control}

TODO(andresoportus).

### Peripheral Control {#peripheral-control}

TODO(andresoportus).

### Signal Processing Control {#signal-processing-control}

TODO(andresoportus).

### Content Protection {#content-protection}

TODO(andresoportus).
