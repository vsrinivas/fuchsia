# Virtual Audio Device

The intention of the Virtual Audio Device is to provide a flexibly-configurable audio device on all
Fuchsia systems (even in the absence of audio hardware) for end-to-end testing of the audio
subsystem.

The Virtual Audio Device and its drivers are provided by virtual_audio_driver.so and are only
included in Fuchsia as part of the "media/tests" component group.

## Driver Entities

When virtual_audio_driver.so is installed (as part of media/tests), the VirtualAudioBus driver
attaches to the /dev/test device node, automatically adding a device for controlling virtual audio
devices. This device node is exposed at /dev/test/virtual_audio and is backed by a controller
driver, implemented by a singleton of the VirtualAudioControlImpl class. Virtual audio devices are
created by interacting with this virtual audio controller, via FIDL (more on the FIDL connection and
interfafces below).

The controller can be instructed to create a device configuration, and to create a virtual audio
device using this configuration. To Add a device, it creates a VirtualAudioStreamIn or
VirtualAudioStreamOut object, which inherits significant functionality from parent class
SimpleAudioStream - this parent class is located in Zircon and is used by other audio drivers as
well. VirtualAudioStream methods provide very basic support for gain, position notification, and
supported formats.

## Debugging the Virtual Audio Device Drivers

TRACE and SPEW levels of logging are disabled by default. To enable them, use an 'fx set' command\
like the following:

    fx set x64 --args=dev_bootfs_labels=[\"//src/media/audio/drivers/virtual_audio:kernel_logging\"]

## FIDL Interfaces

Currently, a separate virtual_audio_service accepts FIDL binding requests and forwards them onward
to the driver. In addition to writing test client code that directly generates FIDL calls to these
interfaces, the cmdline utility 'virtual_audio' interactively exercises these. The driver fully
implements the three fuchsia.virtualaudio FIDL interfaces:
* Control
* Input
* Output

### virtualaudio.Control

The virtualaudio.Control interface is used for top-level activation or deactivation of virtual audio
devices. The Disable function deactivates/removes any active virtual audio devices and disallows any
subsequent virtual device activations. Conversely, the Enable function once again allows virtual
audio devices to be activated/added (although it does not automatically re-activate any previously
active devices).

### virtualaudio.Input and virtualaudio.Output

These FIDL interfaces are used to configure and add virtual audio input and output devices.
virtualaudio.Configuration is a subset of the top-level input and output interfaces, and includes
methods to statically configure these virtual devices before they are created, specifically to set
the following properties:
* Device name
* Manufacturer name
* Product name
* Unique ID
* Supported format ranges (single or multiple)
  - Min/max channels
  - Sample type
  - Min/max frame rate
  - Frame rate family
* FIFO depth (static or per-range)
* External delay
* Ring buffer size and/or restrictions
* Gain capabilities
  - Min/max gain in dB
  - Number of gain steps
  - Can mute
  - Can AGC
* Initial gain state
  - Gain in dB
  - Mute on/off
  - AGC on/off
* Plug capabilities
  - Hardwired
  - Can asynchronously notify
* Initial plug state
  - Plugged
  - Plug time
Together, these properties comprise a virtual audio device _config_. Finally, the
virtualaudio.Configuration interface contains methods to:
* Reset the device configuration, returning it to a default state.
* Clear the previously-added format ranges, retaining other configuration state.

The virtualaudio.Device interface (another subset of Input and Output) contains methods to make the
following dynamic changes:
* Add a virtual audio device (from the config at the moment of Add).
And then subsequently, for an active virtual audio device, to
* Remove this device, or to
* Change the plug state of this device.

Once virtual audio input and output devices are created, they can interact with the audio subsystem,
including accepting/completing packets and transitioning into/out of playback mode. This
functionality is in large part provided by the SimpleAudioStream library.

## Future directions

### Feature Testing

These features may eventually be added to the virtualaudio driver/service:
* Data received by a virtual audio output driver could be sent to:
  - A specified WAV file (done by the svc, not the drv), and/or
  - A virtual audio Input capture channel, and/or
  - Some other sideband channel that conveys this audio to a test client.
* Data transmitted from a virtual audio input driver could originate from:
  - A specified WAV file (sent to drv from the svc),
  - A specified generated signal (sinusoid, square, ramp, triangle, noise),
  - A virtual audio Output render channel, or
  - Some other sideband channel, as provided by a test client.
* Dynamic gain changes (originating externally, e.g. from AVRCP or HID)
* Devices with more than 15 supported ranges

* Static configuration of the following:
  - Delay from Start request received, to actual start of playback
  - Imprecision in the rate that position advances (monotonic or drift)
  - Imprecision in the position reported

* On-the-fly (post-device-creation) configurability of the following:
  - supported_formats (?),
  - fifo_depth (triggered on format change),
  - external_delay_nsec (triggered on format change?),
  - ring_buffer size (triggered on format change?),
  - gain_state,
  - position notification frequency.

### Resiliency Testing

The validation that AudioCore performs on audio drivers can be validated by a badly-behaved
synthetic driver. We should add "bad actor" modes for the virtual driver, to verify our resiliency
to common driver errors in which they "fail to honor the driver contract". These may include failing
to provide the notifications promised, etc.

To exercise error handling and other resiliency in audio_core, test clients should be able to
specify that a driver emit malformed versions of the following messages (including error result, for
those marked with +):
* GetFormats response
* SetFormat response (+)
* GetGain response
* SetGain response (+)
* PlugDetect response
* Async plug state notification
* GetUniqueId response
* GetString response (+)
* GetFifoDepth response (+)
* GetBuffer response (+)
* Start response (+)
* Stop response (+)
* Position notification

Malformed patterns may include:
* Unknown command (val not present in `audio_cmd_t` enum as `message.hdr.cmd`)
* Supplying payload with mismatched size for the given command
* Using `AUDIO_INVALID_TRANSACTION_ID` as val of `message.hdr.transaction_id`
* No format ranges supported
* Format ranges out of order
* Inconsistent "min > max" ranges for channels, frame rate, gain
* Out-of-range current/min/max values
* Unknown flags for sample format, frame rate family, gain, plug
* Impossible combinations of flags (frame rate, plug state)
* Unexpectedly blank, zero or invalid-float (NAN/inf/+-0) values
* Delayed/no response
* Unsolicited response (particularly upon NO_ACK)
* Delayed/no notification
* Unsolicited/invalid notification (particularly when disabled)
* Hot-pluggable devices that use builtin unique_id values
* Incorrectly encoded (non UTF8) strings
