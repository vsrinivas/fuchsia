# Simple Sine Example App

This directory contains an example application that shows, in simplest possible
implementation, how to play audio via the lowest-layer FIDL interfaces.

### USAGE

  simple_sine

### Asynchronous applications and Fuchsia

This example uses asynchronous interface calls, establishing an asynchronous
message loop before calling into its MediaApp object. Once the MediaApp performs
initial setup -- including establishing a callback that will be triggered later
-- we run the loop. The loop accepts and dispatches incoming messages until
instructed to quit (at which point loop.Run() returns, and this example app
exits). The MediaApp knows when the last piece of audio has played, so it posts
a 'quit' message to our loop. It does so using the closure that we provided it,
in the MediaApp constructor.

### Internals

Note: this example omits numerous important aspects of the Media interfaces,
including the retrieval of supported media formats, setting the renderer gain,
packet demand management, and nuanced treatment of timeline transforms.

Once the app object is created, we handle any command-line options, call
`MediaApp::Run` (which sets everything up and starts playback), then run the
asynchronous message loop (which allows asynchronous callbacks from the system
to flow to our app, so that playback can continue to the end). Once all of the
audio has been played, the app posts the 'quit' task as detailed earlier, and
the app subsequently exits.

Focusing on this example's media-specific setup and asynchronous tasks, the
highest-level description of events (shown in `MediaApp::Run`) is as follows:
1. Open the primary FIDL interface to the audio subsystem (AudioRenderer);
2. Set the audio playback format;
3. Map a shared memory section through which we transport audio to the renderer;
4. Write audio data into that memory (in this case a looping sine wave);
5. Submit a set of media packets (and when each returns, submit the next one);
6. Signal that we have provided enough audio, and playback can begin;
7. Once the final media packet is submitted, exit (which closes any resources).

### Details

Below is a more detailed account of the steps taken, and why each is necessary.

##### Open FIDL interfaces

With the provided StartupContext, we obtain an Audio interface pointer to, and
use that to obtain interface pointers to AudioRenderer. At that point we no
longer need our Audio interface and can allow it to go out of scope (and hence
be automatically closed).

We use the AudioRenderer interface to _set playback format_ and start
playback. If we so desired, we could also use AudioRenderer to set output
gain for our audio stream (not shown in this example).

We must **set_error_handler** on each asynchronous interface before using it.
These interfaces represent FIDL channels, which can close asynchronously. The
system notifies us of closures via an error handler callback. In this example,
if an unexpected channel closure occurs we log the problem and begin the
shutdown process.

##### Set Playback Format

We populate an `AudioStreamType` struct with the appropriate number of channels,
sample rate, and sample format. This example uses 32-bit floating-point format,
playing a 1-channel signal at 48 kHz.

##### Map a Shared Memory Section to the Renderer

In order to convey audio data to the renderer (and in turn the audio device),
we must map a section of memory that will be shared cross-process to the
renderer. In this example, we use _100_ different "payload buffers" to keep our
renderer supplied with audio data, continually reusing these buffers in
sequence in ring-buffer fashion. For this reason, we want the buffers to be
contiguous within a single large address range. To do this, we first create a
Virtual Memory Object of the required size; we then map this address range to
be writable and readable.

This example uses the VMO mapper object from FBL, to create and map this memory
section, before instructing the audio renderer to use this section via the
`AddPayloadBuffer` call.

##### Write Audio Data Into the Shared Buffer

Having mapped our shared memory section into our process (at
`payload_buffer_.start()`), we can now write audio data into it. In this
simple example, we write our audio data only once during setup, but clients
can write into mapped memory at any time **before it is submitted** to the
system, or **after the system returns it**. This is described in more detail
in the following section.

This example plays a sine wave signal, sending identical data to both left and
right channels (as shown in the inner loop of `WriteAudioIntoBuffer`). For each
frame in our audio buffer we compute the appropriate sine value.

In order for the audio subsystem to use our buffer as a wraparound ring-buffer,
we must subdivide it and continuously submit these pieces in sequence. This
example uses 100 "payload buffers" that essentially represent each subsequent
piece of the overall mapped buffer. Nonetheless, this is still a single
contiguous memory range, so when writing our audio data we can think of it as a
single large buffer. The audio subsystem guarantees that it will never write
into your payload buffer (it maps the VMO that you have provided to it as
__read-only__), so in a looped-signal scenario such as this one, audio buffers
can be resubmitted without rewriting them.

##### Create and Submit Media Packets

As we prepare to begin playback, we provide an initial set of audio data to the
renderer. We do this by supplying a set of media packets. Following that, as
each packet returns, we submit a new packet that points to a portion of that
same overall memory section. In this way, the example keeps the same number of
media packets with the renderer at all times, each packet pointing to a
different piece of the overall mapped buffer.

In this example, we do not specify presentation timestamps. This means that the
audio subsystem should start playback immediately when ready, and play each
buffer successively, without overlap or intervening gap. All media packets
created in this example are identical except for one field: `payload_offset`.
This field (along with `payload_size`) specifies the buffer location that this
packet represents. Once we call `SendPacket`, we should not access the
associated payload memory until we receive a signal from the audio subsystem --
specifically the callback that we provided to `SendPacket`.

Once playback has begun, the sequence of [`SendPacket` -> callback] is our
primary mechanism for ongoing conversation with the audio subsystem. For this
reason, most interactive clients will generate the next set of audio data
within the `SendPacket` callback itself, immediately before submitting it. In
this example, data has been pre-written and payloads can simply be resubmitted
without being rewritten.

This example plays a sine wave for one second. It knows the exact number of
media packets needed, and submits exactly that number. Once the callback is
received for that last packet, it begins an orderly shutdown (as there are no
additional packets outstanding). An interactive application might do this same
thing upon receiving a signal to stop, from its UI.

##### Begin Playback

Once we tell the audio renderer to begin playback, we will almost immediately
begin to receive `SendPacket` callbacks. For this reason, we do not need to
receive a separate callback when playback completes; this is why we call
`PlayNoReply`. The parameters to `PlayNoReply` indicate that playback should
begin as soon as the system can responsibly do so, using a media timestamp of
the first packet that we provided (thus, 0 here). The callbacks (and subsequent
sending of the next packets) will continue until we stop submitting them, or
until we stop the system.

Even though we have not completed playback -- even though audio packets are
still outstanding at the audio renderer, and others have not yet been sent --
we can nonetheless exit our `MediaApp::Run` method at this point, returning to
main.cc while our message loop dutifully waits until it receives each callback,
and eventually the Quit task. Note: our thread *must* be a message loop thread,
for FIDL interface calls (and callbacks) to function.

##### Shutdown

As stated above, once we receive a `SendPacket` callback signaling that we have
played the right number of packets, we then call Shutdown to close all
remaining resources and exit our app.

There are actually two scenarios that can lead to our calling Shutdown.
1. In the normal case described above, after completing the correct number
of packets, we call Shutdown to complete an orderly winding-down.
2. If at any time during setup or playback we receive a connection error from
any of the FIDL interfaces, we call Shutdown (after logging the problem).
Connection errors indicate that the channel has been closed and the interface
pointer is unusable, so our best course of action is to close everything else
and exit.

Our Shutdown function unmaps our section of shared memory and closes (resets)
our VMO object. It also posts a Quit task to our message loop thread, allowing
it to exit its "dutiful" wait (and immediately thereafter, the app). Once the
main function exits, our MediaApp object is destroyed, and at this point any
remaining FIDL interface (`AudioRenderer`, in case of normal shutdown) is
released.
