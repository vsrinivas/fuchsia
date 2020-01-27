# System Monitor Dockyard

The Dockyard is used as a library (with no GUI itself) that collects data from
the Fuchsia device through the transport system. It responds to requests from
the GUI for digestible (i.e. small) sections of data that will be presented to
the user.

In the future, the Dockyard may execute as an independent process on the Host
machine.

## For the GUI

To get started with the Dockyard API, here's a brief overview for getting
sample data for rendering in the GUI.

If something is unclear or if you prefer examples, consider looking in
./dockyard_test.cc to get an idea of how the pieces work together.

- Create a Dockyard instance.
- Add handlers for new dockyard paths and stream sets:
  - dockyard.SetDockyardPathsHandler(MyDockyardPathHandler);
  - dockyard.SetStreamSetsHandler(MyStreamSetHandler);
- For testing, create some pseudo-random samples
  - Create a RandomSampleGenerator
  - Pass it and the dockyard to GenerateRandomSamples()
- Try requesting some Samples
  - Create a StreamSetsRequest
  - Pass it to dockyard.GetStreamSets()
  - Kick back and wait for MyStreamSetHandler to be called
    - (for testing ProcessRequests() can be used to force it).
- When MyStreamSetHandler is called, loop over the data_sets to get the
  sample values.

### RandomSampleGenerator

The RandomSampleGenerator has several options that allow for creating a wide
variety of samples. It may take some tinkering with different time style and
value generation styles to get the hang of it. Experimentation is encouraged.

### StreamSetsRequest

The StreamSetsRequest includes settings for the rendering style. This affects
how data is summarized. If every column in the GUI graph exactly matched a
Sample, then no render style would be needed. That's expected to be very
uncommon. So often, some kind of filtering will be necessary to approximate the
actual data.

One reasonable approach is to take the average of the values for each column of
the graph. Though this can be odd because some peaks and valleys may not reach
their extremes. E.g. if the values 90 and 100 are averaged, the result is 95. If
that is the peak of a graph, the rendered value can fall slightly short of what
we might expect.

Using the rendering style SCULPTING pulls the highs to their peaks and the lows
to their valleys. Of course, this is also an approximation and not a 100% true
representation of the data.

Or maybe there's a desire to smooth a rather fuzzy looking rendering. If the
data is rapidly jumping between values it can be hard to visualize the trend.
Using the rendering style WIDE_SMOOTHING will blend neighboring values to
produce a graph that is less jagged.

There are other rendering styles, again experimentation is encouraged.