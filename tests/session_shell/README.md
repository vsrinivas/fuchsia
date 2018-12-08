# session_shell integration test

This test exercises the APIs exposed to the session shell:

* Create, start, stop, delete stories.

* See existing stories as long as they are not deleted.

* Add modules to existing stories.

* Be notified when story state changes.

* Be notified through SessionShell.AttachView() of a new story view when story
  start is requested by RequestStart() rather than Start().

* Be notified through SessionShell.DetachView() of a story going away.

The test code is invoked as a session shell from basemgr and executes a
predefined sequence of steps, rather than to expose a UI to be driven by user
interaction, as a session shell normally would. I.e. the test is implemented as a
session shell component.
