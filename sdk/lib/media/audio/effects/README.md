# Device Effects

The _audio effects_ interface enables device makers and system builders to add custom audio
processing at a location in the audio pipeline that is unavailable to normal audio clients. This
processing can be performed on the final mixed output audio stream right before it is consumed by
playback hardware, and/or on an incoming stream immediately after it is produced by input hardware
before it is provided to clients of the audio capture API.

The following interfaces, structures and constants are specified in the **audio_effects.h** file.


## **System description**
The Fuchsia audio system calls these device effects using the "C" ABI specified by the following
interface calls and their related structs and consts. The system dynamically loads the library
calls the synchronous functions described below to query and configure the effect, and subsequently
to process audio while it is actively streaming.

The audio effects library can contain numerous different effects (such as an equalizer, a volume
limiter, or a reverb); these are called _effect types_. For a given effect type, at any time there
may be multiple independent copies of the effect that are active; each is called an
_effect instance_. This is not unlike an object hierarchy; effect types equate to classes, and
effect instances are analogous to instantiated objects of that class.

Additionally, an effect can be configured by settings that govern its behavior (such as EQ
parameters or reverb decay time). In aggregate, these settings are referred to as the effects
_configuration_. This _configuration_ is provided to the effect in the form of a string, the schema
of which is determined by each effect internally. Mirroring the object-oriented design mentioned
above, the configuration and schema are defined across a given effect type, but each instance of
that effect has an independent configuration values.

This low-level interface does not provide effects with any knowledge of where they are being
inserted in the processing chain. This allows the system to be flexibly configured. However, a
settings file (discussed elsewhere, TBD) is used to instruct the system which effects should be
used, in which topology, with which configuration blobs.

## **APIs**
Below are input parameters, return values and usage notes for the audio effects APIs. The library
must export a **fuchsia_audio_effects_module_v1** structure that contains data and function
pointers that comprise that modules implementation:

```
typedef struct {
  uint32_t num_effects;

  bool (*get_info)(uint32_t effect_id, fuchsia_audio_effects_description* effect_desc);

  fuchsia_audio_effects_handle_t (*create_effect)(uint32_t effect_id, uint32_t frame_rate,
                                                 uint16_t channels_in, uint16_t channels_out,
                                                 const char* config, size_t config_length);

  bool (*update_effect_configuration)(fuchsia_audio_effects_handle_t handle, const char* config,
                                      size_t config_length);

  bool (*delete_effect)(fuchsia_audio_effects_handle_t handle);

  bool (*get_parameters)(fuchsia_audio_effects_handle_t handle,
                         fuchsia_audio_effects_parameters* effect_params);

  bool (*process_inplace)(fuchsia_audio_effects_handle_t handle, uint32_t num_frames,
                          float* audio_buff_in_out);

  bool (*process)(fuchsia_audio_effects_handle_t handle, uint32_t num_frames,
                  const float* audio_buff_in, float* audio_buff_out);

  bool (*flush)(fuchsia_audio_effects_handle_t handle);
} fuchsia_audio_effects_module_v1;

```

### **fuchsia_audio_effects_module_v1::get_info**
```
bool (*get_info)(
    uint32_t effect_id,
    fuchsia_audio_effects_description* effect_desc
  );
```
#### Inputs
- **effect_id**: a *uint32_t* representing the effect type being queried. This value must be less
than the number of effect types reported by **fuchsia_audio_effects_module_v1::num_effects**.
- **effect_desc**: pointer to a *fuchsia_audio_effects_description* struct allocated by the system.
The implementation should copy information about this device effect type into this struct.

#### Returns
- **true**: call succeeded; the struct `*effect_desc` contains information about the specified
effect type.
- **false**: call failed; the contents of struct `*effect_desc` are undefined.

### **fuchsia_audio_effects_module_v1::create_effect**
```
fuchsia_audio_effects_handle_t (*create_effect)(
    uint32_t effect_id,
    uint32_t frame_rate,
    uint16_t channels_in,
    uint16_t channels_out,
    const char* config,
    size_t config_length
  );
```
#### Inputs
- **effect_id**: a *uint32_t* representing the effect type being queried. This value must be less
than the number of effect types reported by **fuchsia_audio_effects_module_v1::num_effects**.
- **frame_rate**: a *uint32_t* representing the frame rate (in Hertz) for the audio stream that
will flow through the created audio device effect. The created effect must be capable of operating
at this rate.
- **channels_in**: a *uint16_t* representing the number of channels in the audio stream that will
flow into the created audio device effect. The created effect must be capable of accepting an input
stream with this number of channels.
- **channels_out**: a *uint16_t* representing the number of channels in the audio stream that will
flow out of the created audio device effect. The created effect must be capable of generating an
output stream with this number of channels.
- **config**/**config_length**: a string containing configuration parameters for the newly created
effect.

#### Returns
- **FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE**: call failed; no resources should be allocated.
- **Otherwise**: call succeeded; the returned `fuchsia_audio_effects_handle_t` is a unique handle
representing the just-created active instance of this effect type.

#### Notes
The value that this call returns will be used as `fuchsia_audio_effects_handle_t`, in subsequent API
calls that reference a particular effect instance.

When the values of `channels_in` and `channels_out` are equal, the system will require the created
device effect to process audio in-place.

### **fuchsia_audio_effects_module_v1::delete_effect**
```
bool (*delete_effect)(
    fuchsia_audio_effects_handle_t handle
  );
```
#### Inputs
- None

#### Returns
- **true**: the specified effect instance has been deleted, and this handle is no longer valid.
- **false**: invalid effect handle; no further action was taken.

### **fuchsia_audio_effects_module_v1::get_parameters**
```
bool (*get_parameters)(
    fuchsia_audio_effects_handle_t handle,
    fuchsia_audio_effects_parameters* effect_params
  );
```
#### Inputs
- **handle**: a `fuchsia_audio_effects_handle_t` representing an active effect instance.
- **effect_params**: pointer to a *fuchsia_audio_effects_parameters* struct allocated by the
system. The implementation should copy information about the specified active effect instance into
this struct.

#### Returns
- **true**: the struct `*effect_params` contains information about the specified effect
instance.
- **false**: call failed; contents of struct `*effect_params` are undefined.

#### Notes
This interface returns the operational parameters for this instance of the device effect. These
parameters are invariant for the lifetime of this effect, based on the initial values provided when
the client created the effect.

### **fuchsia_audio_effects_module_v1::process_inplace**
```
bool (*process_inplace)(
    fuchsia_audio_effects_handle_t handle,
    uint32_t num_frames,
    float* audio_buff_in_out
  );
```
#### Inputs
- **handle**: a `fuchsia_audio_effects_handle_t` representing an active effect instance.
- **num_frames**: a `uint32_t` containing the number of frames that the specified effect instance
must process. This value cannot exceed the previously-set frame rate for this effect instance.
- **audio_buff_in_out**: a pointer to an audio buffer containing samples of type `float`.

#### Returns
- **true**: call succeeded; the effect instance successfully processed `num_frames` of audio
samples, in-place within the buffer specified by `audio_buff_in_out`.
- **false**: call failed; the contents of buffer `audio_buff_in_out` are undefined.

#### Notes
This interface synchronously processes the buffer of `num_frames` audio data in-place. The total
number of samples processed is equal to `num_frames` multiplied by the value of `channels_out`,
from the earlier **fuchsia_audio_effects_module_v1::create** call.

**fuchsia_audio_effects_module_v1::process_inplace** is used in all cases except where
rechannelization occurs. It is preferred over **fuchsia_audio_effects_module_v1::process** because
of its reduced memory consumption and related system impact.

The upper limit on the value of `num_frames` guarantees that no
**fuchsia_audio_effects_module_v1::process_inplace** call will ever entail more than one second of
audio.

### **fuchsia_audio_effects_module_v1::process**
```
bool (*process)(
    fuchsia_audio_effects_handle_t handle,
    uint32_t num_frames,
    const float* audio_buff_in,
    float* audio_buff_out
  );
```
#### Inputs
- **handle**: a `fuchsia_audio_effects_handle_t` representing an active effect instance.
- **num_frames**: a `uint32_t` containing the number of frames that the specified effect instance
must process. This value cannot exceed the previously-set frame rate for this effect instance.
- **audio_buff_in**: a pointer to a buffer containing audio samples of type `float`, from which
`num_frames` audio frames must be ingested by the effect instance.
- **audio_buff_out**: a pointer to a buffer where `num_frames` audio frames of type `float` must be
produced, based on the data ingested from `audio_buff_in`.

#### Returns
- **true**: call succeeded; the effect instance successfully processed `num_frames` of audio
samples from the buffer specified by `audio_buff_in`, to the buffer specified by `audio_buff_out`.
- **false**: call failed; the contents of buffer `audio_buff_out` are undefined.

#### Notes
This interface synchronously processes ‘num_frames’ of audio data in type float, from
`audio_buff_in` to `audio_buff_out`. The total number of input samples ingested is equal to
`num_frames` multiplied by the value of `channels_in` from the earlier
**fuchsia_audio_effects_module_v1::create** call. The total number of output samples generated and
populated into `audio_buff_out` is equal to `num_frames` multiplied by the value of `channels_out`
from that same **fuchsia_audio_effects_module_v1::create** call.

This interface is necessary for cases where rechannelization occurs; it is not used otherwise.
Where possible, the system instead uses **fuchsia_audio_effects_module_v1::process_inplace**,
because of its reduced memory consumption and related system impact.

The upper limit on the value of `num_frames` guarantees that no
**fuchsia_audio_effects_module_v1::process** call will ever entail more than one second of audio.

## **Typedefs and Structs**

Below are fields and usages for the audio effects structs:

### **fuchsia_audio_effects_handle_t**
```
typedef void* fuchsia_audio_effects_handle_t;
```
This handle represents an active instance of an effect. They are guaranteed to be unique, although
new values might recycle those of previously-deleted effect instances.

### **fuchsia_audio_effects_description**
```
typedef struct {
  char name[FUCHSIA_AUDIO_EFFECTS_MAX_NAME_LENGTH];
  uint16_t incoming_channels;
  uint16_t outgoing_channels;
} fuchsia_audio_effects_description;
```

#### name
This string is a descriptive (user-facing) name for the specified audio device effect type.

### **fuchsia_audio_effects_parameters**
```
typedef struct {
  uint32_t frame_rate;
  uint16_t channels_in;
  uint16_t channels_out;
  uint32_t signal_latency_frames;
  uint32_t suggested_frames_per_buffer;
} fuchsia_audio_effects_parameters;
```

#### frame_rate
This is the sampling rate (in Hertz) of the incoming and outgoing frames of audio data.

#### channels_in
This is the number of channels in the incoming stream that is passed to the device effect instance
via **fuchsia_audio_effects_module_v1::process** or
**fuchsia_audio_effects_module_v1::process_inplace** calls. This value is important for calculating
the correct number of samples to ingest (specifically, multiply this value by `num_frames`). See
`audio.fidl` for the current maximum number of input channels currently supported.

#### channels_out
This is the number of channels in the outgoing stream produced by the device effect instance via
**fuchsia_audio_effects_module_v1::process** or
**fuchsia_audio_effects_module_v1::process_inplace** calls. This value is important for calculating
the correct number of samples to generate and populate (specifically, multiply this value by
`num_frames`).

Note that channel order is not explicitly specified here; where appropriate, the generally-accepted
channel-ordering will be assumed (such as 5.1 ordering if 6 channels is specified). We acknowledge
that this is inadequate specification for many use cases, but suggest that for now it may be
adequate.

#### signal_latency_frames
This is the group delay (in frames) caused by the audio processing algorithm itself, considering
the parameters specified by **fuchsia_audio_effects_module_v1::create** (most notably `frame_rate`)
when this effect instance was created. As an example, a basic volume effect should report a value
of 0, since it will adds no "right-shift" of any kind when considering the incoming audio signal
within the audio buffer; restated, this example effect needs no additional information beyond the
first input sample itself, in order to produce the first output sample (hence a reported value of 0
frames of delay added). As another example, an effect that operates in the frequency domain would
set this value based on the window size it uses (512, for example).

#### suggested_frames_per_buffer
The effect uses this value to indicate a `num_frames` value at which it most effectively executes
the calls **fuchsia_audio_effects_module_v1::process** or
**fuchsia_audio_effects_module_v1::process_inplace**.

In general, this value represents the effect-creator's suggested balance between latency (which is
minimized by smaller values) and efficiency (which is maximized by larger values). Effects that use
frequency-domain processing can use this parameter to indicate the optimal window size for their
algorithm. With this in mind, this value indicates not merely the ideal size of any single process
call, but rather the ongoing alignment stride from the very start of processing.

This value is advisory-only. The system is not obligated to honor this request (indeed, in the case
of multiple effects chained in series, this may not even be possible). Rather, the system makes
decisions of this kind with the goal of optimizing overall system behavior, including
responsiveness, fidelity, throughput, power consumption and other factors.

## **Constants**
Below are constant values used with the audio effects interfaces and structs:

### **FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE**
```
const fuchsia_audio_effects_handle_t FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE = 0;
```
This value is returned by **fuchsia_audio_effects_module_v1::create**, if an effect instance with
the specified parameters _cannot_ be created.

### **FUCHSIA_AUDIO_EFFECTS_CHANNELS_ANY**
```
const uint16_t FUCHSIA_AUDIO_EFFECTS_CHANNELS_ANY =
    std::numeric_limits<uint16_t>::max();
```
This value can be returned in a **fuchsia_audio_effects_description** struct, to indicate that an
effect can potentially accommodate any number of channels. This value may NOT be used in
**fuchsia_audio_effects_module_v1::create** calls or **fuchsia_audio_effects_parameters** structs,
as `channels_in` and `channels_out` in those contexts indicate the actual number of channels in the
effect instance, rather than the possible values.

### **FUCHSIA_AUDIO_EFFECTS_CHANNELS_SAME_AS_IN**
```
const uint16_t FUCHSIA_AUDIO_EFFECTS_CHANNELS_SAME_AS_IN =
    std::numeric_limits<uint16_t>::max() - 1;
```
This value can be returned in a **fuchsia_audio_effects_description** struct for the value of
`outgoing_channels`, to indicate that the number of output channels must equal the number of input
channels. An effect should only use this if it reports its `incoming_channels` as
**FUCHSIA_AUDIO_EFFECTS_CHANNELS_ANY**.

### **FUCHSIA_AUDIO_EFFECTS_CHANNELS_MAX**
```
const uint16_t FUCHSIA_AUDIO_EFFECTS_CHANNELS_MAX = 256;
```
This is the maximum value allowed in **fuchsia_audio_effects_create** calls or in the
**fuchsia_audio_effects_parameters** struct. It can also be used in
**fuchsia_audio_effects_description** structs, but note that in practice, effects are constrained
by the number of channels supported by actual audio hardware. Currently the Fuchsia audio system
limits this to MAX_PCM_CHANNEL_COUNT, which is less than this value.

### **FUCHSIA_AUDIO_EFFECTS_MAX_NAME_LENGTH**
```
const size_t FUCHSIA_AUDIO_EFFECTS_MAX_NAME_LENGTH = 255;
```
This value is used as an outer limit on the lengths of names of effects. This constant is important
because it determines the effective size of the struct **fuchsia_audio_effects_description**.
