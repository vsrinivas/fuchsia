# link_data integration test

Tests the machinery that allows modules to coordinate through shared link data,
and that these link data are persisted across story stop and resume.

This test is invoked as a user shell from basemgr and executes a
predefined sequence of steps, rather than to expose a UI to be driven by user
interaction, as a user shell normally would.

The user shell creates a story with Module0. Module0 in turn starts Module1 and
Module2 and sets them up such that they can exchage data. Module1 and Module2
alternate in incrementing a counter.

The aspects examined by this test sequence are:

1. Creating a story with initial data for the fist module.
2. Verifying that the initial data are actually in the fuchsia::modular::Link presented to the
   first module.
3. Verifying that the initial data can be changed through the fuchsia::modular::Link interface.
4. Observe the running module changing its own links.
5. Stopping the story.
6. Using the fuchsia::modular::StoryProvider API to verify that it shows no running stories.
7. Starting the story again.
8. Observing the running module resume modification of its own links at the
   point it left off.
