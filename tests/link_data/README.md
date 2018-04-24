# link_data integration test

Tests the machinery that allows modules to coordinate through shared link data,
and that these link data are persisted across story stop and resume. This is
invoked as a user shell from device runner and executes a predefined sequence of
steps, rather than to expose a UI to be driven by user interaction, as a user
shell normally would.

