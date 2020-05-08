# Wav library

This library enables clients to save audio stream(s) to WAV audio files. This is
intended for use during development only: it allows developers to analyze and
improve audio fidelity in their components (including Fuchsia OS itself).

This functionality can be toggled by the bool template parameter on the
WavWriter object itself. When false, it results in zero additional code added
(assuming a reasonable optimizing compiler).

In Fuchsia, this library is used in various locations, including media examples
FX and wav_recorder, as well as the system audio mixer itself. What follows are
details on how to use the WavWriter library, followed by instructions on using
the WavWriter support built into the system mixer to inspect the final mixed
audio output streams -- useful when debugging audio-related code.

### Using WavWriter

See `wav_recorder` in 'audio/tools' for a focused, easy-to-follow example of
using this library.

After creating a WavWriter object (with template parameter 'true' or blank), the
`Initialize` call creates a RIFF-based WAV audio file, leaving it open and ready
for writing the audio data into the file. The file is of non-zero size because
there is a file header describing the contents, but it contains no audio data
yet. The `Reset` call, incidently, returns the file to exactly this state.

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
an uninitialized state, preventing it from being written further. Before it does
so, `Close` first conveniently calls `UpdateHeader`. At any time, `Delete` will
remove the WAV file that has been created, leaving the file system as it was
before. Note that `Delete` can be called at any time after `Initialize` has been
called, even after `Close` has been called.

As mentioned above, by simply switching the template parameter to <false>, all
library code will convert to inlined stubs that are eliminated by a modern
optimizing compiler -- so you can instrument your code without worrying about
any impact on performance when this tracing is disenabled.
