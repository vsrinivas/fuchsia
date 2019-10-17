# [Dockyard](README.md)

## Terms

This is an alphabetical glossary of terms related to the Dockyard.

##### Dockyard ID
A number used to uniquely refer to a Sample Stream or other named entity. The
Dockyard ID is 1:1 with a specific Dockyard Path.

##### Dockyard Path
A UTF-8, case sensitive text string referring to a Sample Stream or other named
entity. The path value is primarily used in the UI, internally the Dockyard ID
is used. E.g. "cpu:0", "physMem", "procCount" are Dockyard Paths.

##### Normalize
A graph rendering filter.
Scales the graph output so that the high and low values are framed nicely on
the screen. The data is scaled dynamically.

##### Sample
A sample is a piece of data from the device. A Sample has a time (when it was
gathered), a path (like a name), and a value. E.g. Sample z: (<time T>,
"memory:used", 300000).

##### Sample Category
The named idea of where Samples come from. E.g. "cpu", "memory", "process" are
Sample Categories.

##### Sample Set
A portion of a Sample Stream. E.g. a Sample Stream might have 100,000 Samples
and a Sample Set might only consist of 1,200 of those Samples.

##### Sample Stream Request
A request for samples sent to the dockyard. It specifies the time range,
sample streams, and filtering desired.

##### Sample Stream Response
A message sent from the dockyard in response to a Sample Stream Request.
It includes the filtered sample data (a Sample Set) for each of the requested
Sample Streams. A filtered sample is a mathematically generated representation
of 0 to many samples.

Example 1: a filtered sample might have the value 900 if filter was averaging
samples of 700, 800, 1000, and 1100. Note that the filtered sample doesn't match
any of the actual samples in this example.

Example 2: when rendering 3 columns on the screen for 12 Samples, each filtered
sample will be computed from 4 Samples.

##### Sample Stream
An ordered set of Samples for a given Sample Category. E.g. all the Samples for
"cpu0".

##### Smooth
A graph rendering filter that blends neighboring samples. The resulting graph
will have have less dramatic highs and lows (i.e. with less noise).

Without smoothing, a set of samples like [100, 0, 100, 0, 100] will look like a
letter 'W'. With smoothing, that data will look like a wavy line with values
closer to 50. This makes overall trends easier to see, though it sacrifices
detail. I.e. Smooth is like an average, but it includes data from wider range of
Samples.
