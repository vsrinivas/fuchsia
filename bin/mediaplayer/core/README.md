# PlayerCore Design

This directory contains the `PlayerCore` class and related classes. These classes
are part of the Fuchsia media player implementation.

`PlayerCore` is concerned with managing a graph that contains the demux, decoders,
renderers and other components that constitute a media player graph. It deals
with graph building, priming, timeline control, flushing, end-of-stream and
related issues. It aggregates *some* aspects of the underlying components,
leaving other aspects to direct communication between those compenents and
the client (`PlayerImpl`, specifically).

`PlayerCore` delegates many of its concerns to subordinate *segments*, each of which
manages a subset (segment) of the graph. A `PlayerCore` has at most one
*source segment* (defined by the `SourceSegment` abstract class) and at most
one *sink segment* (defined by the `SinkSegment` abstract class) per medium
(audio, video, text, subpicture).

The client creates the segments it wants the player to use and registers them
with the player. For example, `PlayerImpl` typically wants the player
to host a demux, so it will create a `DemuxSourceSegment` and register it with
the player using `PlayerCore::SetSourceSegment`.

Likewise, `PlayerImpl` wants content rendered, so it creates one or more
`RendererSinkSegment` objects and registers them using `PlayerCore::SetSinkSegment`.
`RendererSinkSegment` uses the renderer provided to it in its constructor, which
means `PlayerImpl` has to create that, too. This creates the opportunity
for `PlayerImpl` to have its own relationship with a renderer that doesn't
concern the player or the sink segments. For example, `PlayerImpl` can
control gain on the audio renderer directly and doesn't need `PlayerCore` or
`RendererSinkSegment` to help with that. The video renderer reports video
geometry and supports the creation of Scenic views, both aspects of its direct
relationship with `PlayerImpl`.

# Demux Source Segment

`DemuxSourceSegment` is a source segment that hosts a demux. Its job is to
add the demux to the graph, enumerate the demux's elementary streams for the
player and control the demux on behalf of the player. The `DemuxSourceSegment`
constructor accepts a demux, so `PlayerImpl` has to create the demux. This
allows `PlayerImpl` to decide how it wants to set up the demux and allows
`PlayerImpl` to have a relationship with the demux that doesn't involve
the player. For example, the player doesn't concern itself with metadata, which
`PlayerImpl` obtains directly from the demux.

# Renderer Sink Segment

`RenderSinkSegment` is a sink segment that hosts a renderer. Its job is to
add the renderer to the graph and insert whatever type conversion nodes (e.g.
decoder) required to convert the elementary stream to a type that the renderer
can handle. It also controls the renderer on behalf of the player. The
`RenderSinkSegment` accepts a renderer, so `PlayerImpl` has to create the
renderer and can talk to the renderer directly. This allows `RenderSinkSegment`
and `PlayerCore` to be ignorant of the medium-specific aspects of the renderers.
Neither `RenderSinkSegment` nor `PlayerCore` concern themselves with gain control,
view creation or video geometry.

# Future Plans

`PlayerCore` will eventually be dynamic in the sense that it will handle
precisely-timed replacement of segments while playback is underway. This
will enable features such as gapless transitions from one source to another
and gapless rerouting to renderers.

In order to enable routing of elementary streams to multiple renderers
concurrently, a new sink segment will be created that aggregates multiple
subordinate sink segments. This enables fanout to multiple renderers without
complicating the `PlayerCore` class.

Currently, the only source segment implemented is `DemuxSourceSegment`, which
hosts a demux. A new source segment will be implemented that handles FIDL
sources that supply separate elementary streams.
