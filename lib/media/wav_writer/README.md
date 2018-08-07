# WavWriter library #

This library enables clients to save audio stream(s) to WAV audio files. This
is intended for use during development only: it allows developers to analyze
and improve audio fidelity in their components (including Fuchsia OS itself).

This functionality can be toggled by the bool template parameter on the
WavWriter object itself. When false, it results in zero additional code added
(assuming a reasonable optimizing compiler).

In Fuchsia, this library is used in various locations, including media examples
FX and wav_record, as well as the system audio mixer itself. What follows are
details on how to use the WavWriter library, followed by instructions on using
the WavWriter support built into the system mixer to inspect the final mixed
audio output streams -- useful when debugging audio-related code.


### Using WavWriter ###

See `wav_record` in 'examples/media' for a focused, easy-to-follow example
of using this library.

After creating a WavWriter object (with template parameter 'true' or blank),
the `Initialize` call creates a RIFF-based WAV audio file, leaving it open
and ready for writing the audio data into the file. The file is of non-zero
size because there is a file header describing the contents, but it contains
no audio data yet. The `Reset` call, incidently, returns the file to exactly
this state.

Subsequently, the `Write` call accepts a buffer of audio data, as well as the
number of bytes in that buffer, and it transfers those bytes directly into the
file. Note that for performance reasons, the file headers are not updated upon
every single `Write` call. Because the headers contain fields that denote the
amount of audio data, they must be updated eventually, but this could be done
only occasionally, or even just once at the end of the recording process. The
`UpdateHeaders` call does exactly this, writing the two fields that change as
the file grows in size, and returning the write cursor to the end of the open
file. Clients can call `UpdateHeader` as frequently as they wish (after every
_Write_, or every few seconds, or only once).

The `Close` call finalizes (closes) the file handle and returns the library to
an uninitialized state, preventing it from being written further. Before it
does so, `Close` first conveniently calls `UpdateHeader`. At any time, `Delete`
will remove the WAV file that has been created, leaving the file system as it
was before. Note that `Delete` can be called at any time after `Initialize` has
been called, even after `Close` has been called.

As mentioned above, by simply switching the template parameter to <false>, all
library code will convert to inlined stubs that are eliminated by a modern
optimizing compiler -- so you can instrument your code without worrying about
any impact on performance when this tracing is disenabled.


### Inspecting final output from the system mixer ###

In development builds of the Fuchsia OS, the WavWriter can be used to examine
the audio streams emitted by the system mixer (of course, this functionality is
removed from official production builds where this might pose a media security
or overall resource consumption risk).

To enable the WavWriter support in the system audio mixer, change the bool
(kWavWriterEnabled) found toward the top of driver_output.cc to 'true'. The
Fuchsia system mixer produces a final mix output stream (potentially
multi-channel) for every audio output device found. Thus, enabling the
WavWriter will cause an audio file to be created for each output device.

These files are created on the target (Fuchsia) device at location
'/tmp/wav_writer_N.wav', where N is a unique integer for each output. One can
copy these files back to the host with:
```
  fx scp <ip of fuchsia device>:tmp/mixer-*.wav ~/Desktop/
```
At this time, once audio playback begins on any device, the system audio mixer
produces audio for ALL audio output devices (even if no client is playing
audio to that device). The wave files for to these devices will naturally
contain silence.

Additionally, at this time Fuchsia continues mixing (once it has started) to
output devices indefinitely, even after all clients have closed. This will
change in the future. Until then, however, the most effective way to use this
tracing feature is to `killall audio_core` on the target, once playback is
complete. (The _audio_core_ process restarts automatically when needed, so
this is benign.) The mixer calls `UpdateHeader` to update the 'length'
parameter in both RIFF Chunk and WAV header after every successive file write,
so the files should be complete and usable even if you kill audio_core during
audio playback (which means that `Close` is never called).
