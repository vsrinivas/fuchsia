# Audio Signal Processing

## Overview

The signal processing interface is available to be potentially used by audio hardware codecs (from
here on referred to as 'codecs') , DAIs and glue drivers. This interface `SignalProcessing` is a
FIDL protocol composed by the `Codec`, `Dai` and `StreamConf` protocols.

The `SignalProcessing` interface is defined to control signal processing hardware and their
topologies. We define processing elements (PEs) as a logical unit of audio data processing provided
by an audio driver, and we define topologies as the arrangement of PEs in [pipelines][pipeline] and
controls associated with them.

The `SignalProcessing` interface allows hardware vendors to implement drivers with stable
application binary interfaces (ABIs), and allow system integrators to configure drivers to perform
differently based on system or product requirements using these interfaces for run-time
configurations.

### Topologies

Each driver can have its own topology. Glue drivers can abstract from applications the topologies
exposed by DAI or codec drivers as needed for a particular configuration or product. Note that it is
possible although not required to expose topologies to applications, in particular to `audio_core`.

In cases where the topologies are static, it is not mandatory to expose the one and only topology
since there is no way to change it. This is the case for simpler hardware codecs PEs.

Note that topologies are not meant to fully describe the audio pipeline state/format/configuration
in and out of every PE. The intent is to describe what can be changed/rearranged by the client (for
instance a glue driver) based on its knowledge, configuration (for instance from metadata) and
specific business logic.

### Processing Elements

A PE is expected to be hardware-provided functionality managed by a particular driver (but it could
be emulated in software, as any other driver functionality). A pipeline is composed of one or more
PEs and a topology is composed of one or more pipelines.

A codec or DAI driver can expose their topology by implementing the composed `SignalProcessing`
protocol. A glue driver can use `Codec` and `Dai` protocols `SignalProcessing` functionality on the
particular product or system. Finally a `StreamConf` user like `audio_core` can use a `StreamConf`
protocol `SignalProcessing` functionality.

We refer to the server as the driver that is providing the signal processing protocol, e.g. a codec
or DAI driver. We refer to the client as the user of the functionality, e.g. a glue driver or an
application such as `audio_core`.

## Basic operation

The client is responsible for requesting and then configuring any signal processing capabilities.

## GetProcessingElements

`GetProcessingElements` allows to optionally get a list of all PEs (a server not supporting PEs may
return an empty vector). For instance this method may be called by a glue driver on a codec. Once
the list of PEs is known to the client, the client may configure the PEs based on the parameters
exposed by the PE types.

<!-- Reference links -->

[pipeline]: https://en.wikipedia.org/wiki/Pipeline_(computing)
