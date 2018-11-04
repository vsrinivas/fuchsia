# session_shell integration test

This test exercises the APIs exposed to the session shell:

* Create, start, stop, delete stories.

* See existing stories as long as they are not deleted.

* Add modules to existign stories.

* Be notified when story state changes.

The test code is invoked as a session shell from basemgr and executes a
predefined sequence of steps, rather than to expose a UI to be driven by user
interaction, as a session shell normally would. I.e. the test is implemented as a
session shell component.
