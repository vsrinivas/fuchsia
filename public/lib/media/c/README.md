# Fuchsia Media Client Library

This directory contains a shared library for clients of the Fuchsia media
subsystem. The Fuchsia media subsystem implements this helper library as a "C"
Shared Object lib (libmedia_client.so), on top of the native FIDL interfaces.

This library enables clients to discover audio output devices, query their
support for audio formats, open output streams in the specified formats, and
write audio data to those streams.


## System description
Via the Media Client APIs, clients open an fuchsia_audio_manager and use it to
enumerate the devices present in the system. Using the unique device IDs, the
client can discover their preferred parameters and formats. The client
determines the parameters it wants for that device, and opens an output stream
with those parameters. Fuchsia uses output streams to play back audio data;
accordingly, a client must send a sequence of buffers to an output stream in
order for this audio to be played.

Once an output stream is opened, the client can query the stream's minimum
delay (in nanoseconds). Specifically, this delay is the interval from 1) the
instant when the client sends the system a buffer of audio data, to 2) the
instant when the first sample in that buffer is presented at the physical
output. Clients must attach a presentation time to each audio data buffer that
it sends to an output stream, and clients should include the stream's
min_delay value when calculating presentation timestamps. That said, once the
client has written the initial buffer of audio data to the output stream
(which effectively starts playback), the client may then present any
subsequent buffers with a special timestamp value that indicates "gapless
playback" -- this directs the audio system to calculate and set the
appropriate timestamps to play the sequence of audio data buffers without gap
or overlap.

Once the client has presented all the audio data for an output stream, it can
free the output stream. To completely close the connection between the client
and the Fuchsia subsystem, the client can free the audio_manager.


## APIs
Below are input parameters, return values and usage notes for the Media Client
APIs:

### fuchsia_audio_manager_create
```
fuchsia_audio_manager* fuchsia_audio_manager_create();
```
##### Inputs
- None

##### Returns
- Pointer to an opaque, system-allocated *fuchsia_audio_manager* struct.

### fuchsia_audio_manager_free
```
int fuchsia_audio_manager_free(
    fuchsia_audio_manager* audio_manager
  );
```
##### Inputs
- **audio_manager**: pointer to an opaque *fuchsia_audio_manager* struct
obtained from the system via a *fuchsia_audio_manager_create()* call.

##### Returns
- **ZX_OK**: call succeeded; audio_manager has been deregistered and freed.

##### Notes
By freeing the audio manager struct, the client closes its connection to the
Fuchsia audio system. Any audio output streams that are open when this call is
made will be stopped and freed before this call returns.

### fuchsia_audio_manager_get_output_devices
```
int fuchsia_audio_manager_get_output_devices(
    fuchsia_audio_manager*            audio_manager,
    fuchsia_audio_device_description* device_desc_buffer,
    int                               num_device_descriptions
  );
```
##### Inputs
- **audio_manager**: pointer to an opaque *fuchsia_audio_manager* struct
obtained from the system via a *fuchsia_audio_manager_create()* call.
- **device_desc_buffer**: pointer to memory allocated by the client, where
the system should copy device descriptions.
- **num_device_descriptions**: maximum number of device description structs for
the system to copy to the supplied *device_desc_buffer* location.

##### Returns
- **Positive or zero value**: call succeeded; return value is either
  1. total number of devices in the system (usage 1), or
  2. number of device descriptions copied into the provided buffer (usage 2:
see notes).

In usage case 2), **device_desc_buffer** is an out parameter. If the return
value is positive, the system copies into device_desc_buffer one or more
*fuchsia_audio_device_description* structs.

##### Notes
This call has two usage modes. The first is to determine the number of
devices, and the second is to actually retrieve the information about all or
some of those devices.

If the client calls this API with **zero-NULL values for both
device_desc_buffer and num_device_descriptions**, this instructs the
system to return the total number of devices found in the system. A client
would do this in order to determine how large a buffer it needs to allocate,
to contain the appropriate number of *fuchsia_audio_device_description*
structs. The client would then include this buffer (as well as the number of
device descriptions requested) in a subsequent call to this API (see below),
to retrieve the actual device descriptions.

If the client calls this API with **non-zero values for both
*device_desc_buffer* and *num_device_descriptions***, this instructs the
system to copy the requested number of *fuchsia_audio_device_description*
structs into the provided buffer. Accordingly, this buffer must have a size of
at least *num_device_descriptions x sizeof(fuchsia_audio_device_description)*.
The system will copy as many device descriptions as are specified (up to the
total number in the system); the return value is the number of device
description structs copied.

Note that the client does not have to retrieve *all* of the device
descriptions. If for example the client is only interested in the first device
description, it may specify a *num_device_descriptions* value of 1. In this
case, only a single device description would be copied into the
client-supplied buffer - that of the first (default) device - and this
function call's return value would be 1.

### fuchsia_audio_manager_get_output_device_default_parameters
```
int fuchsia_audio_manager_get_output_device_default_parameters(
    fuchsia_audio_manager*    audio_manager,
    char*                     device_id,
    fuchsia_audio_parameters* params_out
  );
```
##### Inputs
- **audio_manager**: pointer to an opaque *fuchsia_audio_manager* struct
obtained from the system via a *fuchsia_audio_manager_create()* call.
- **device_id**: string representing the unique device identifier, received
from the system as part of that device's fuchsia_audio_device_description.  If
NULL or empty, parameters for the default output device are returned.
- **params_out**: pointer to memory allocated by the client, where system
should copy default parameters for the specified device, via a
*fuchsia_audio_parameters* struct.

##### Returns
- **ZX_ERR_NOT_FOUND**: device_id does not match to any device currently found
in the system.
- **ZX_OK**: call succeeded.

Additionally, **params_out** is an out parameter. if the call succeeds, the
system copies into *params_out* a *fuchsia_audio_parameters* struct
representing the default parameters for this device.

##### Notes
The default parameters returned are *recommended* values. In particular, the
specified buffer_size is only a hint from the system to the client, about the
optimal size of the buffer to be used in Write calls (in number of frames).
This is intended to help the application determine its optimal timer frequency
and buffer size, to appropriately balance low latency and low power
consumption.

As with fuchsia_audio_manager_create_output_stream, if the provided *device_id*
is NULL or empty, the default device will be used (index 0 when retrieving the
complete list of devices).

### fuchsia_audio_manager_create_output_stream
```
int fuchsia_audio_manager_create_output_stream(
    fuchsia_audio_manager*        audio_manager,
    char*                         device_id,
    fuchsia_audio_parameters*     stream_params,
    fuchsia_audio_output_stream** stream_out
  );
```
##### Inputs
- **audio_manager**: pointer to an opaque *fuchsia_audio_manager* struct
obtained from the system via a *fuchsia_audio_manager_create()* call.
- **device_id**: string representing the unique device identifier, received
from the system as part of that device's fuchsia_audio_device_description. If
this parameter is NULL or empty then the stream should be created for the
default output device.
- **stream_params**: pointer to an *fuchsia_audio_parameters* struct,
containing the intended parameters/format for the output stream.
- **stream_out**: pointer to memory allocated by the client. If the call is
successful, this is where the system will copy a pointer to an
*fuchsia_audio_output_stream* that it has allocated and populated.

##### Returns
- **ZX_ERR_NOT_FOUND**: device_id does not match to any device currently found
in the system.
- **ZX_OK**: call succeeded; the output stream has been created.

Additionally, **stream_out** is an out parameter, populated with a pointer to a
system-allocated *fuchsia_audio_output_stream* struct if the call succeeds.

##### Notes
This call creates a new audio output stream and returns it to the client.

As with fuchsia_audio_manager_create_output_stream, if the provided *device_id*
is NULL or empty, the default device will be used (index 0 when retrieving the
complete list of devices).

The *buffer_size* parameter specified by the client (within *stream_params*)
is a hint to the system about the likely size of the buffer it will use in
Write calls (in number of frames). This is intended to help the system
determine an optimal timer frequency and/or device buffer size to
appropriately balance low latency and low power consumption. If needed, the
client can specify a different *num_samples* value in the
*fuchsia_audio_output_stream_write* calls that follow.

### fuchsia_audio_output_stream_free
```
int fuchsia_audio_output_stream_free(
    fuchsia_audio_output_stream* stream
  );
```
##### Inputs
- **stream**: pointer to an *fuchsia_audio_output_stream* received from the
system.

##### Returns
- **ZX_OK**: call succeeded; stream has been stopped and freed.

##### Notes
This call stops and closes an audio output stream. To the extent possible,
this call will flush any audio samples that have been written to the stream
but not yet played, and will do so before the call returns.

### fuchsia_audio_output_stream_get_min_delay
```
int fuchsia_audio_output_stream_get_min_delay(
    fuchsia_audio_output_stream* stream,
    zx_duration_t*               delay_nsec_out
  );
```
##### Inputs
- **stream**: pointer to an *fuchsia_audio_output_stream* received from the
system.
- **delay_nsec_out**: pointer to a *zx_duration_t* allocated by client. This
is where the system will copy the minimum delay (in nanoseconds) of this
output stream.

##### Returns
- **ZX_OK**: call succeeded.

##### Notes
This call returns the minimum downstream delay for this output stream, taking
into account any "outboard" delays caused by analog or digital hardware after
the system mixer, as well as the periodicity of the system mixer itself. When
the client is sending buffers of audio data to the system, specifying a
presentation timestamp greater than [now + delay] will guarantee that they are
played without glitches or excessive latency.

Note that *delay_nsec_out* is specified in the same units as the system clock
(ZX_CLOCK_MONOTONIC).

### fuchsia_audio_output_stream_set_gain
```
int fuchsia_audio_output_stream_set_gain(
  fuchsia_audio_output_stream* stream,
  float                        db_gain);
  );
```
##### Inputs
- **stream**: pointer to an *fuchsia_audio_output_stream* received from the
system.
- **db_gain**: float value, representing playback gain for this output stream.

##### Returns
- **ZX_ERR_OUT_OF_RANGE**: the client specified a db_gain value that was greater
than the maximum allowed output gain *FUCHSIA_AUDIO_MAX_OUTPUT_GAIN*.
- **ZX_OK**: call succeeded.

##### Notes
This call sets the output gain for this specific stream. This setting is
per-stream and does not affect any other output streams. Note that this output
gain stage is applied during final system mixing. As a result, all audio samples
provided by the *fuchsia_audio_output_stream_write* API must be no greater than
1.0 and no less than -1.0, regardless of the output stream's gain.

### fuchsia_audio_output_stream_write
```
int fuchsia_audio_output_stream_write(
    fuchsia_audio_output_stream* stream,
    float*                       sample_buffer,
    int                          num_samples,
    zx_time_t                    pres_time
  );
```
##### Inputs
- **stream**: pointer to an *fuchsia_audio_output_stream* received from the
system.
- **sample_buffer**: pointer to memory allocated by the client, containing
samples of audio data to be played. Audio samples are in *float* format, and
each must have a value no greater than 1.0 and no less than -1.0.
- **num_samples**: total number of audio samples found in sample_buffer. This
should be a multiple of the num_channels used when creating this stream.
- **pres_time**: when to present the first audio sample in this buffer, as
specified by ZX_CLOCK_MONOTONIC. If this is the first buffer sent to this
output_stream, a future value that incorporates the stream's *min_delay*
should be specified. Otherwise, a value of FUCHSIA_AUDIO_NO_TIMESTAMP is
allowed -- this indicates that the buffer should be presented immediately
following the previous one, and that the system can calculate the appropriate
pres_time.

##### Returns
- **ZX_ERR_BAD_STATE**: the client specified FUCHSIA_AUDIO_NO_TIMESTAMP in
the first buffer sent to this stream (or the first buffer after a gap in
playback). As a result, the buffer has not been submitted, and the API must
be called again with a true presentation timestamp.
- **ZX_ERR_IO_MISSED_DEADLINE**: the client specified a pres_time that was
too soon (for example, 0). The buffer has not been submitted, and the API must
be called again with an updated presentation timestamp.
- **ZX_OK**: call succeeded; audio data has been queued to the output stream
to be played.

##### Notes
This call submits a buffer of audio data to the output stream to be played at
the specified presentation time. The presentation time must be calculated by
adding the current time (in ZX_CLOCK_MONOTONIC terms) to the min_delay for
this output stream.

For subsequent calls to fuchsia_audio_output_stream_write, a value of
FUCHSIA_AUDIO_NO_TIMESTAMP may be used, which indicates that the system should
use the appropriate presentation timestamp so that the buffer being submitted
will be presented immediately after the previous one, with no gap in playback.

Any error code returned from the system means that it has not queued any of
the audio data from the client-supplied buffer. In these cases, the client
must call *fuchsia_audio_output_stream_write()* again with correct parameters.

Specifically if the error code *ZX_ERR_IO_MISSED_DEADLINE* is returned, then
the client must recalculate an appropriate presentation timestamp and include
it instead of using *FUCHSIA_AUDIO_NO_TIMESTAMP*. Note: this may require the
client to drop audio data (likely from the front of the buffer used in the
failing API call) in order to keep the audio synchronized with the intended
timeline. If this is less important than playing every single sample, then the
previously-submitted buffer can be resubmitted with the later timestamp.
Either way, once this amended *fuchsia_audio_output_stream_write()* call
succeeds, then all subsequent calls to *fuchsia_audio_output_stream_write()*
can again send a value of *FUCHSIA_AUDIO_NO_TIMESTAMP* for *pres_time*.

The client must provide audio samples in the float format, within the range of
[-1.0, 1.0]. Values greater than 1.0 will be clamped to 1.0, and values less
than -1.0 will be clamped to -1.0, without any notification to the client. Note
that these limits apply *regardless* of the per-stream gain specified by the
*fuchsia_audio_output_stream_set_gain* call.


## Structs
Below are fields and usages for the Media Client structs:

### fuchsia_audio_manager
```
struct _fuchsia_audio_manager;
typedef struct _fuchsia_audio_manager fuchsia_audio_manager;
```
This is an opaque struct used by the system to represent a connection between
the Fuchsia audio subsystem and an application.

### fuchsia_audio_device_description
```
typedef struct {
  char name[FUCHSIA_AUDIO_MAX_DEVICE_NAME_LENGTH];
  char id[FUCHSIA_AUDIO_MAX_DEVICE_NAME_LENGTH];
} fuchsia_audio_device_description;
```
##### name
This string is a descriptive (user-facing) name for this audio device.
##### id
This string is a unique identifier for this device, to be used by client code.
##### Notes
At this time, both of these fields have a length of
FUCHSIA_AUDIO_MAX_DEVICE_NAME_LENGTH, which is currently 256 characters.

### fuchsia_audio_parameters
```
typedef struct {
  int sample_rate;
  int num_channels;
  int buffer_size;
} fuchsia_audio_parameters;
```
##### sample_rate
When used by *fuchsia_audio_manager_get_output_device_default_parameters()* to
communicate a device's default parameters, this represents the optimal sample
rate for this device.

When used by *fuchsia_audio_manager_create_output_stream()* as a client opens
an output stream, this represents the sample rate of the audio data that will
be written to the stream.

For now there is a soft limit of 96000 (96kHz) for this value.

##### num_channels
When used by *fuchsia_audio_manager_get_output_device_default_parameters()* to
communicate a device's default parameters, this represents the optimal number
of channels for this device.

When used by *fuchsia_audio_manager_create_output_stream()* as a client opens
an output stream, this represents the number of channels in the audio data
that client will write to the stream.

For this release, the system expects only mono (1) or stereo (2) values.

##### buffer_size
When used by *fuchsia_audio_manager_get_output_device_default_parameters()* to
communicate a device's default parameters, this represents a hint from the
system about the ideal device buffer size and/or timer periodicity, striking a
balance between output latency and power consumption.

When used by *fuchsia_audio_manager_create_output_stream()* as a client opens
an output stream, this represents a hint about the buffer size it is likely to
use in Write calls (in number of frames).

##### Notes
**buffer_size** is always purely a suggestion. Clients are free to use other
**num_samples** values in the *fuchsia_audio_output_stream_write* calls that
follow.

### fuchsia_audio_output_stream
```
struct _fuchsia_audio_output_stream;
typedef struct _fuchsia_audio_output_stream fuchsia_audio_output_stream;
```
This is an opaque struct used by the system to represent an active output
stream on one of the devices in the system.


## Constants
Below are constant values used with the Media Client structs and APIs:

### FUCHSIA_AUDIO_MAX_DEVICE_NAME_LENGTH
```
const size_t FUCHSIA_AUDIO_MAX_DEVICE_NAME_LENGTH = 256;
```
This value is used as an outer limit on the lengths of the name and unique ID
for each audio output device found. This const is important because it
determines the effective size of the *fuchsia_audio_device_description* struct
(see above).

### FUCHSIA_AUDIO_MAX_OUTPUT_GAIN
```
const float FUCHSIA_AUDIO_MAX_OUTPUT_GAIN = 20.0f;
```
This is the maximum value accepted by the *fuchsia_audio_output_stream_set_gain*
call. If a client sends values greater than this to
*fuchsia_audio_output_stream_set_gain*, the call will fail with error
*ZX_ERR_OUT_OF_RANGE*, and no change will be made to the stream's output gain.

### FUCHSIA_AUDIO_NO_TIMESTAMP
```
const zx_time_t FUCHSIA_AUDIO_NO_TIMESTAMP = INT64_MAX;
```
This value is used to signify that the system should calculate and apply the
appropriate presentation timestamp so that the buffer of audio data being
written should be queued immediately subsequent to the previously submitted
audio data, for "gapless playback".

Although the range of zx_time_t is the entire unsigned int64 range, we limit
this to only the bottom half of that range, to smoothly interface with
signed int64 timestamps elsewhere in the system. Hence the highest possible
timestamp (and the one that we reserve to signify 'system should just
calculate the next timestamp value, for gapless playback') is the max value
of the SIGNED range.
