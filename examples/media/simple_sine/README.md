# Simple Sine Example App

This directory contains an application that plays a 1-second sine wave to the
default audio output. It is intended to show, in the most direct way, audio
playback on Fuchsia using direct calls to the Media FIDL interfaces.

This example omits numerous important aspects of the Media interfaces,
including the retrieval of supported media formats, setting the renderer gain,
packet demand management, and nuanced treatment of timeline transforms).

### USAGE

  simple_sine

### Internals

This example shows, in the simplest possible implementation, how to play audio
by connecting directly to the lowest-layer FIDL interfaces. The highest-level
description of events (as shown in **MediaApp::Run**) is as follows:
1. Open the primary FIDL interfaces to the audio subsystem;
2. Set the MediaRenderer's playback format;
3. Map a shared memory section to transport audio from client to renderer;
4. Write audio data into that memory(in this case a looping sine wave);
5. Submit a series of media packets - when each returns, submit the next one;
6. Begin playback, using Timeline FIDL interfaces;
7. Once the final media packet is submitted, close any resources.

### Details

Below is a more detailed account of the steps taken, and why each is necessary.

##### Open FIDL interfaces

With the provided ApplicationContext, we obtain an interface pointer to
AudioServer, and use that to obtain interface pointers to MediaRenderer and
AudioRenderer. At this point we no longer need our AudioServer interface and
can allow it to go out of scope (and hence automatically closed). Similarly, one
of the two renderer interfaces must be kept open during setup and playback, but
we allow the other to be automatically closed when we exit the AcquireRenderer
method.

If we so chose, we would use an AudioRenderer to _set output gain_ for our audio
stream (not shown in this example). We need a MediaRenderer to _set the playback
format_ (a required step); furthermore we need it to start playback (via the
TimelineControlPoint and TimelineConsumer interfaces). For this reason, we
retain our MediaRenderer.

We must **set_connection_error_handler** on every interface before using it.
These interfaces represent FIDL channels, which can close asynchronously. The
system notifies us of these closures via this error handler callback. In this
example, if an unexpected channel closure occurs we log the problem and begin
the shutdown process.

##### Set Playback Format

Using AudioSampleFormat and MediaType, we create an AudioMediaTypeDetails that
specifies the appropriate sample format and size (packed 16-bit signed
integer), number of channels (stereo) and sample rate (specified by our
constant), within an overall media type of LPCM audio.

Clients often call **MediaRenderer::GetSupportedMediaTypes** before setting the
media type. In this example, this step has been omitted for brevity. The audio
subsystem provides guaranteed support for this format, on all devices.

##### Map a Shared Memory Section to the Renderer

In order to convey audio data to the renderer (and in turn the audio device),
we must map a section of memory that will be shared cross-process to the
renderer. In this example, we use _four_ "payload buffers" to keep our
renderer supplied with audio data, continually reusing these buffers in
sequence in ring-buffer fashion. For this reason, we want the buffers to be
contiguous within a single large address range. To do this, we first create a
Virtual Memory Object of the required size; we then map this address range to
be writable and readable.

With our MediaRenderer, we obtain a PacketConsumer, which we will use to signal
to the audio subsystem that a section of audio buffer is ready to be consumed.
This is used throughout the playback process, so we must retain this interface
for the lifetime of our MediaApp object.

Our PacketConsumer requires a VMO for each shared memory section, so we
duplicate our VMO handle and pass it to **AddPayloadBuffer**. In a more
complex example, we might have multiple memory sections and need to enumerate
them; here the constant kBufferId refers to our one and only payload buffer.

##### Write Audio Data Into the Shared Buffer

Having mapped our shared memory section into our process (at
**mapped_address_**), we can now write audio data into it. In this simple
example we write our audio data only once, during our setup, but clients can
write into the mapped memory at any time **before it is submitted** to the
system, or **after the system returns it**. This is described in more detail in
the following section.

This example plays a sine wave signal, sending identical data to both left and
right channels (as shown in the inner loop of **WriteStereoAudioIntoBuffer**).
Wrapped around that, for each frame in our audio buffer we compute the
appropriate sine value (using float precision) before rounding into a signed
16-bit integer value.

In order for the audio subsystem to use our buffer as a wraparound ring-buffer,
we must subdivide it and continuously submit these pieces in sequence. This
example uses four "payload buffers" that essentially represent the four
quadrants of the overall mapped buffer. Nonetheless, this is still a single
contiguous memory range, so when writing our audio data we can think of it as a
single large buffer. The audio subsystem guarantees that it will never write
into your payload buffer, so in a looped-signal scenario such as this one,
audio buffers can be resubmitted without rewriting them.

##### Create and Submit Media Packets

As we prepare to begin playback, we provide an initial set of audio data to the
renderer. We do this by supplying four media packets: one packet for each
payload (quadrant) in our overall ring buffer. Following that, as each packet
is returned, we submit a new packet that points to that same payload. In this
way, the example keeps four media packets of data with the renderer at all
times, each packet pointing to a different quadrant of the overall mapped
buffer.

In this example, all the media packets that we submit have a presentation
timestamp (PTS) of *kNoTimestamp*, which means that the audio subsystem should
play each buffer successively, without overlap or intervening gap. Even the
first packet is submitted with this PTS, meaning that the system should place
this packet far enough in the future so that no initial audio data will be
lost when playback begins. All media packets created in this example are
identical except for one field: *payload_offset*. This field (along with
payload_size) specifies the buffer quadrant that this packet represents.
Once we call SupplyPacket, the associated payload range cannot be accessed
until we receive a signal from the audio subsystem -- specifically the
SupplyPacket callback.

Once playback has begun, the sequence of [SupplyPacket -> callback] is your
primary mechanism for ongoing conversation with the audio subsystem. For this
reason, most interactive clients will generate the next set of audio data in
the SupplyPacket callback itself, immediately before submitting it. In this
example, the data has been pre-written and payload ranges can simply be
resubmitted without being rewritten.

This example plays a sine wave for one second. The example knows the exact
number of media packets needed, and submits exactly that number. Once the
callback is received for that last packet, we begin an orderly shutdown (as
there are no additional packets outstanding).

##### Begin Playback

All Fuchsia media components have an associated Timeline, running at a certain
rate (relative to a reference clock). Up until now, the Timeline for our
renderer has been running at a rate of 0 -- in other words, time is not yet
moving forward (which is why the packets we have submitted have not yet
started to play). To start playback, we must create a TimelineTransform
specifying that our timeline should change its rate to 1:1 (meaning that our
presentation clock will start), at a certain point in the future. Rather than
specify an exact moment in time, this example uses kUnspecifiedTime ("as soon
as safely possible"), which allows the audio subsystem to set the time offset
for us.

Leading up to this, we use our MediaRenderer to obtain a TimelineControlPoint.
This interface is useful for retrieving the state of the presentation clock at
any time, but for this example we only use it to obtain a TimelineConsumer
interface. With a TimelineConsumer, in turn, we call *SetTimelineTransform*
with the transform that we have created, to transition into playback.

Once we make this call, we should expect SupplyPacket callbacks to begin, as
packets are completed. These will continue until we stop submitting packets,
or until we stop the system.

Until then, we exit the MediaApp::Run method, returning to main.cc while our
message loop dutifully waits until it receives a Quit task. Note that our thread
*must* be a message loop thread, for FIDL interface calls (and callbacks) to
function.

##### Shutdown

As stated above, once we receive a callback signaling that we have played the
right number of packets, we then call Shutdown to close all remaining resources
and exit our app. (If we intended to keep the interfaces alive, but needed all
packets to be returned, we would have called Flush and waited for its callback.)

There are actually two scenarios that can lead to our calling Shutdown.
1. In the normal case, as described above, after completing the correct number
of packets, we call Shutdown to complete an orderly winding-down.
2. If at any time during setup or playback we receive a connection error from
any of the FIDL interfaces, we call Shutdown (after logging the problem).
Connection errors indicate that the channel has been closed and the interface
pointer is unusable, so our best course of action is to close everything else
and exit.

Our Shutdown function unmaps our section of shared memory and closes (resets)
our VMO object. It also closes our PacketConsumer and MediaRenderer interfaces,
which are the only ones that this simple example needed to persist for the
lifetime of the app. Finally, we post a Quit task to our message loop thread,
allowing it to exit its "dutiful" wait (and immediately thereafter, the app).
