# Mixer templates

The mixer templates implement core LPCM audio mixing functionality. Features
include:

* Any number of output channels
* Parameterized output sample type
* Any number of inputs
* Any number of input channels, specified per input
* Parameterized input sample type, specified per input
* Schedule of MxN mixdown tables per input
* Parameterized linear volume level type
* Linear fade

The mixer itself has a template parameter that determines the type of the output
samples. Inputs to the mixer have template parameters for input sample type,
output sample type (which must match the mixer's) and the type underlying
linear audio levels.

The combinations of template parameters that are supported depend on what
template specializations have been written.

*Currently, the only specializations that have been implemented are for float
input samples, float output samples and float levels.*

## Design

Mixer works by enlisting zero or more MixerInputs, which do the heavy lifting.
The mix operation (Mixer::Mix) mixes all inputs to a specified output buffer.
For each call to Mix, the Mixer calls each MixerInput's Mix method, allowing
the input to mix its contribution into the output buffer.

The MixerInput class itself is an abstract base class that is only concerned
with an input's relationship with the mixer. MixerInputImpl is currently the
only concrete implementation of MixerInput.

MixerInputImpl's job is to mix a stream of packets into output buffers under
the direction of the mixer. Levels are controlled by a schedule of mixdown
tables, each of which contains a matrix of Levels, one for each input/output
channel pair. Each entry in the schedule specifies either an instantaneous
transition to the the new table or a linear fade from the previous table.

The frame rate of input packets must match the frame rate of the output
buffers.

MixerInputImpl has template parameters for input sample format, output sample
format and the type underlying a Level. The Mixer itself and MixerInput have
only an output sample format template parameter.

The MixerInputImpl::Mix method mixes input packets into a single output buffer.
It does this by breaking the output buffer into the largest contiguous regions
that can be mixed using a fixed set of Jobs and a single input buffer. A Job
specifies an input channel, an output channel and the applicable levels for
those channels. A Job is 'active' if its levels aren't silent. Active jobs are
kept in a list so that inactive jobs can be efficiently ignored.

The Mix method determines how many frames can be mixed before the end of
the output buffer, the end of the input buffer (obtained from a packet), or
the next MixTable transition is encountered. Once that region is identified,
all active Jobs are executed against the input and output buffer regions.
This involves mixing a Job's input channel into its output channel adjusting
volume according to the current mixdown tables. In some cases, this means
applying a linear fade from one level to another.

When the end of the output buffer is encountered, the Mix method returns.
When the end of the input buffer is encountered, the current input packet is
released and the next one is staged. When a MixTable transition occurs, the
active jobs list is updated according to the new current tables.

MixerInputImpl employs a few simple strategies to minimize the amount of work
required to update the active jobs list:
1. Jobs are pre-allocated for each input/output channel pair.
2. The active jobs list uses an intrusive doubly-linked list implementation
   so that the required operations (push_front and erase) are constant-time
   and don't involve the memory manager.
3. The Jobs table and MixdownTable are organized such that, when iterating
   across them with an outer out_channel loop and an inner in_channel loop,
   only a pointer increment is required to move from one Job/Level to the
   next.

Jobs have separate mix implementations for unity gain, constant gain and
fade. Template specializations are implemented for each supported combinations
of template parameters.

All PTS values are assumed to be in a time base equal to the frame rate.
E.g. for a frame rate of 48k, a PTS change of 48,0000 represents one second.
