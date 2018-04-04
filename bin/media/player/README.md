# Player Design

This directory contains the `Player` class and related classes. These classes
are part of the Fuchsia media player implementation.

`Player` is concerned with managing a graph that contains the demux, decoders,
renderers and other components that constitute a media player graph. It deals
with graph building, priming, timeline control, flushing, end-of-stream and
related issues. It aggregates *some* aspects of the underlying components,
leaving other aspects to direct communication between those compenents and
the client (`MediaPlayerImpl`, specifically).

`Player` delegates many of its concerns to subordinate *segments*, each of which
manages a subset (segment) of the graph. A `Player` has at most one
*source segment* (defined by the `SourceSegment` abstract class) and at most
one *sink segment* (defined by the `SinkSegment` abstract class) per medium
(audio, video, text, subpicture).

The client creates the segments it wants the player to use and registers them
with the player. For example, `MediaPlayerImpl` typically wants the player
to host a demux, so it will create a `DemuxSourceSegment` and register it with
the player using `Player::SetSourceSegment`.

Likewise, `MediaPlayerImpl` wants content rendered, so it creates one or more
`RendererSinkSegment` objects and registers them using `Player::SetSinkSegment`.
`RendererSinkSegment` uses the renderer provided to it in its constructor, which
means `MediaPlayerImpl` has to create that, too. This creates the opportunity
for `MediaPlayerImpl` to have its own relationship with a renderer that doesn't
concern the player or the sink segments. For example, `MediaPlayerImpl` can
control gain on the audio renderer directly and doesn't need `Player` or
`RendererSinkSegment` to help with that. The video renderer reports video
geometry and supports the creation of Scenic views, both aspects of its direct
relationship with `MediaPlayerImpl`.

# Demux Source Segment

`DemuxSourceSegment` is a source segment that hosts a demux. Its job is to
add the demux to the graph, enumerate the demux's elementary streams for the
player and control the demux on behalf of the player. The `DemuxSourceSegment`
constructor accepts a demux, so `MediaPlayerImpl` has to create the demux. This
allows `MediaPlayerImpl` to decide how it wants to set up the demux and allows
`MediaPlayerImpl` to have a relationship with the demux that doesn't involve
the player. For example, the player doesn't concern itself with metadata, which
`MediaPlayerImpl` obtains directly from the demux.

# Renderer Sink Segment

`RenderSinkSegment` is a sink seguemth that hosts a renderer. Its job is to
add the renderer to the graph and insert whatever type conversion nodes (e.g.
decoder) required to convert the elementary stream to a type that the renderer
can handle. It also controls the renderer on behalf of the player. The
`RenderSinkSegment` accepts a renderer, so `MediaPlayerImpl` has to create the
renderer and can talk to the renderer directly. This allows `RenderSinkSegment`
and `Player` to be ignorant of the medium-specific aspects of the renderers.
Neither `RenderSinkSegment` nor `Player` concern themselves with gain control,
view creation or video geometry.

# Future Plans

`Player` will eventually be dynamic in the sense that it will handle
precisely-timed replacement of segments while playback is underway. This
will enable features such as gapless transitions from one source to another
and gapless rerouting to renderers.

In order to enable routing of elementary streams to multiple renderers
concurrently, a new sink segment will be created that aggregates multiple
subordinate sink segments. This enables fanout to multiple renderers without
complicating the `Player` class.

Currently, the only source segment implemented is `DemuxSourceSegment`, which
hosts a demux. A new source segment will be implemented that handles FIDL
sources that supply separate elementary streams.
