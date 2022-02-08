# Audio Signal Processing

## Overview

The signal processing interface is available to be potentially used by audio hardware codecs (from
here on referred to as 'codecs') , DAIs and glue drivers. This interface `SignalProcessing` is a
FIDL protocol used by the `Codec`, `Dai` and `StreamConf` protocols to provide audio signal
processing capabilities.

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

A codec or DAI driver can expose their topology by implementing the `SignalProcessing` protocol.
A glue driver can use `Codec` and `Dai` protocols signal processing functionality on the
particular product or system. Finally a `StreamConf` user like `audio_core` can use a `StreamConf`
protocol `SignalProcessing` functionality.

We refer to the server as the driver that is providing the signal processing protocol, e.g. a codec
or DAI driver. We refer to the client as the user of the functionality, e.g. a glue driver or an
application such as `audio_core`.

## Basic operation

The client is responsible for requesting and then configuring any signal processing capabilities.
Once the server provides its PEs by replying to a client's `GetProcessingElements`, the client may
dynamically control the PEs parameters as needed by calling `SetProcessingElementState`.

Also after the server provides its PEs by replying to a client's `GetProcessingElements`, the client
may request available topologies with the `GetTopologies` method. If more than one topology is
returned by `GetTopologies`, then `SetTopology` can be used to pick the topology to use.

### GetProcessingElements

`GetProcessingElements` allows to optionally get a list of all PEs. For instance this method may
be called by a glue driver on a codec. Once the list of PEs is known to the client, the client may
configure the PEs based on the parameters exposed by the PE types.

### SetProcessingElementState

`SetProcessingElementState` allows a client to control the state of a PE using an id returned by
`GetProcessingElements`. PEs of different types may have different state exposed to clients, the
`SetProcessingElementState` parameter `state` has a different type depending on the type of PE.

### GetTopologies

`GetTopologies` allows to optionally get a list of topologies. For instance this method may be
called by a glue driver on a codec. Once the list of topologies is known to the client, the client
may configure the server to use a particular topology.

### SetTopology

`SetTopology` allows a client to control the which topology is used by the server. Only one
topology can be selected at any time.

## Processing elements types

The PEs returned by `GetProcessingElements` support a number of different types of signal processing
defined by the PE types and parameters. PE types define standard signal processing (e.g. `GAIN`,
`DELAY`, `EQUALIZER`, etc), vendor specific signal processing (`VENDOR_SPECIFIC` e.g. a type not
defined in the `SignalProcessing` protocol) and `CONNECTION_POINT`s/`END_POINT`s used to construct
multi-pipelines topologies (allow for pipelines start, end, routing and mixing definitions, see
[Connection points](#connection-points) and [End points](end-points} below).

Each individual PE may have one or more inputs and one or more output channels. For routing and
mixing, PEs may make the number of output channels different from the number of input channels.

Data in each channel (a.k.a. the signal that is processed) may be altered by the PE. For instance
if there is a single PE of type `AGL` in a `Codec` protocol with a `DaiFormat` `number_of_channels`
set to 2, then AGL (Automatic Gain Limiting) can be enabled or disabled for these 2 channels by a
client calling `SetProcessingElementState` with `state` `enable` set to true or false (this assumes
the AGL `ProcessingElement`s `can_disable` was set to true).

## Topologies {#topologies}

The topologies returned by `GetTopologies` support different arrangements for the PEs returned by
`GetProcessingElements`. `GetTopologies` may advertise one or multiple topologies.

### One topology

If one topology is advertised, i.e. `GetTopologies` returns a vector with one element, then all PEs
are part of this explicit single pipeline. Ordering in this case is explicit. For instance, if
`GetProcessingElements` returns 2 PEs:

1. `ProcessingElement`: id = 1, type = `AUTOMATIC_GAIN_LIMITER` (AGL)
1. `ProcessingElement`: id = 2, type = `EQUALIZER` (EQ)

The one `Topology` element returned by `GetTopologies` will list an `id` and a
`processing_elements_edge_pairs` vector explicitly advertising the order in which signal processing
is performed, in this example:

1. `Topology`: id = 1, `processing_elements_edge_pairs` = vector with one element with
`processing_element_id_from` = 1 and `processing_element_id_to` = 2.

This advertises this one topology with one pipeline:

                    +-------+    +-------+
    Input signal -> |  AGL  | -> +  EQ   | -> Output signal
                    +-------+    +-------+

In this topology the beginning (where the input signal is input into the pipeline) and the end of
the pipeline (where the output signal is output from the pipeline) are implicit. They can be made
explicit with PEs of type `END_POINT` (see [End points](#end-points) below).

If only one topology is advertised, then the contents are informational only since the client can't
change the use of one and only topology.

### Multiple topologies {#multiple-topologies}

If multiple topologies are advertised, i.e. `GetTopologies` returns a vector with multiple element,
then PEs may be used in multiple configurations, i.e. topologies. Each topology explicitly lists
a number of PEs and their ordering, i.e. ordering in this case is explicit. The arrangement and
ordering of PEs define a pipeline.

By listing only the specific arrangements and ordering of PEs supported, servers restrict what
combination of pipelines are valid.

For instance, if `GetProcessingElements` returns 6 PEs:

1. `ProcessingElement`: id = 1, type = `AUTOMATIC_GAIN_LIMITER` (AGL)
1. `ProcessingElement`: id = 2, type = `EQUALIZER` (EQ)
1. `ProcessingElement`: id = 3, type = `SAMPLE_RATE_CONVERSION` (SRC)
1. `ProcessingElement`: id = 4, type = `GAIN`
1. `ProcessingElement`: id = 5, type = `DYNAMIC_RANGE_COMPRESSION` (DRC1)
1. `ProcessingElement`: id = 6, type = `DYNAMIC_RANGE_COMPRESSION` (DRC2) parameters different from
DRC1 parameters.

The `Topology` elements returned by `GetTopologies` will list an `id` and a
`processing_elements_edge_pairs` for each topology, in this example:

1. `Topology`: id = 1, `processing_elements_edge_pairs` =
 *. processing_element_id_from` = 3 and `processing_element_id_to` = 2.
 *. processing_element_id_from` = 2 and `processing_element_id_to` = 4.
 *. processing_element_id_from` = 4 and `processing_element_id_to` = 5.
 *. processing_element_id_from` = 5 and `processing_element_id_to` = 1.
1. `Topology`: id = 2, `processing_elements_edge_pairs` =
 *. processing_element_id_from` = 2 and `processing_element_id_to` = 4.
 *. processing_element_id_from` = 4 and `processing_element_id_to` = 6.

This advertises two topologies with one pipeline each:

                    +-------+    +-------+    +-------+    +-------+    +-------+
    Input signal -> |  SRC  | -> +  EQ   | -> + GAIN  | -> +  DRC1 | -> +  AGL  | -> Output signal
                    +-------+    +-------+    +-------+    +-------+    +-------+

                    +-------+    +-------+    +-------+
    Input signal -> |  EQ   | -> + GAIN  | -> +  DRC2 | -> Output signal
                    +-------+    +-------+    +-------+

## Connection points {#connection-points}

The PEs of type `CONNECTION_POINT` allow for:

1. Mixing multiple channels within a single pipeline.
1. Mixing multiple channels from different pipelines.
1. Repeating channels.
1. Expanding a single pipeline into multiple pipelines ones (scatter).

// TODO(fxbug.dev/64877): Add extra context for multi-pipeline construction.

## End points {#end-points}

The PEs of type `END_POINT` are optional (even in the presence of `CONNECTION_POINT`s) and allow for
completing the pipelines structures with a clear starting input(s) and ending output(s).
If no `END_POINT` is specified, then a PE with no incoming edges is an input and a PE with no
outgoing edges is an output. For instance, the example in
[Multiple topologies](#multiple-topologies) above includes two topologies each with a single
pipeline, the single pipeline in topology id 1 starts with PE id 3 and ends with PE id 1, and the
single pipeline in topology id 2 starts with PE id 2 and ends with PE id 6.

// TODO(fxbug.dev/64877): Add extra-context for end points usage.

<!-- Reference links -->

[pipeline]: https://en.wikipedia.org/wiki/Pipeline_(computing)
