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

There is no complex processing in AudioCapturers other than a simple mixer to
merge multiple inputs.
