# Audio Core

This is the core of the audio system. It implements the core FIDL APIs
fuchsia.media.AudioCapturer and fuchsia.media.AudioRenderer. At a high level,
audio core is organized as follows:

```plain
+---------------------+        +---------------------+
|   AudioRenderers    |        |    AudioCapturers   |
| r1   r2   ...   rN  |        | c1   c2   ...   cN  |
+---\---|----------|--+        +--Λ---ΛΛ-------------+
     \  |          |      +-------+  / |
      \ |          |   loopback     /  |
+------VV----------V--+   |    +---/---|-------------+
| o1   o2   ...   oN--+---+    | i1   i2   ...   iN  |
|    AudioOutputs     |        |     AudioInputs     |
+---------------------+        +---------------------+
```

The relevant types are:

*   AudioRenderers represent channels from applications that want to play audio.
*   AudioCapturers represent channels to applications that want to record audio.
*   AudioOutputs represent hardware outputs (speakers).
*   AudioInputs represent hardware inputs (microphones).

To control output routing, we use an enum called an AudioRenderUsage, which has
values like BACKGROUND, MEDIA, COMMUNICATION, etc. We maintain a many-to-one
mapping from AudioRenderUsage to AudioOutput, then map AudioRenderers to
AudioOutputs based on this type. For example, if two AudioRenderers `r1` and
`r2` are created with AudioRenderUsage MEDIA, they are both routed to the
AudioOutput assigned to MEDIA (`o2` in the above graph).

Input routing works similarly, using a type called AudioCaptureUsage.
Additionally, special "loopback" inputs are routed from AudioOutputs.

## Output Pipelines

At each AudioOutput, an OutputPipeline controls the mixing of input streams into
a single output stream. This mixing happens in a graph structure that combines
MixStage nodes (which mix multiple input streams into a single output) and
EffectsStage nodes (which apply a sequence of transformations to an input
stream). MixStage nodes also perform basic transformations: source format
conversion, rechannelization, sample rate conversion, gain scaling, and
accumulation. Gain scaling can be configured at both a per-stream level
(per-AudioRenderer) and a per-AudioRenderUsage level (usually corresponding to
volume controls on the device).

For example, the following graph illustrates an OutputPipeline created for four
renderers, three of which (r1, r2, r3) are mixed first because they share the
same AudioRenderUsage.

```plain
     r1  r2  r3
     |   |   |
  +--V---V---V---+
  |  m1  m2  m3  |
  |   MixStage   |
  +------|-------+
         |
  +------V-------+
  | EffectsStage |
  | 1. high pass |
  | 2. compress  |
  +---------\----+   r4
             \       |
           +--V------V---+
           |   m1    m2  |
           |   MixStage  |
           +------|------+
                  V
                device
```

If loopback capability is enabled for the given device, then a specific pipeline
stage can be designated as the loopback stage. For example, if certain
hardware-specific effects should not be included in the loopback stream, the
loopback stream can be injected at an early stage before the final output is
sent to the device.

## Input Pipelines

There is no complex processing in AudioCapturers other than a simple mixer.

## Inspecting final output from the system mixer

In development builds of the Fuchsia OS, the WavWriter can be used to examine
the audio streams emitted by the system mixer (of course, this functionality is
removed from official production builds where this might pose a media security
or overall resource consumption risk).

To enable the WavWriter support in the system audio mixer, change the bool
(kWavWriterEnabled) found toward the top of driver_output.cc to 'true'. The
Fuchsia system mixer produces a final mix output stream (potentially
multi-channel) for every audio output device found. Thus, enabling the WavWriter
will cause an audio file to be created for each output device.

These files are created on the target (Fuchsia) device at location
`/tmp/r/sys/<pkg>/wav_writer_N.wav`, where N is a unique integer for each output
and `<pkg>` is the name of the `audio_core` package (such as
`fuchsia.com:audio_core:0#meta:audio_core.cmx`). One can copy these files back
to the host with: `fx scp <ip of fuchsia device>:/tmp/.../wav_writer_*.wav
~/Desktop/` At this time, once audio playback begins on any device, the system
audio mixer produces audio for ALL audio output devices (even if no client is
playing audio to that device). The wave files for these devices will, naturally,
contain silence.

Additionally, at this time Fuchsia continues mixing (once it has started) to
output devices indefinitely, even after all clients have closed. This will
change in the future. Until then, however, the most effective way to use this
tracing feature is to `killall audio_core` on the target, once playback is
complete. (The _audio_core_ process restarts automatically when needed, so this
is benign.) The mixer calls `UpdateHeader` to update the 'length' parameter in
both RIFF Chunk and WAV header after every successive file write, so the files
should be complete and usable even if you kill audio_core during audio playback
(which means that `Close` is never called).
