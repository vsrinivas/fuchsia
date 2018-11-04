# trigger integration test

This test verifies that Agents are woken up on triggers for:
  1. New messages on a queue.
  2. An explicitly deleted message queue.
  3. A message queue being deleted as part of story tear down.

The general structure of the test is a session shell is created, which
in turn creates a story and adds the test module to it.

The test module then verifies (1) and (2) by interacting with the
test agent. It also creates a message queue that it does not delete
explicitly and hands the token to the session shell.

Once (1) and (2) have been verified, the session shell deletes the story
and waits for the token it was handed by the module to appear in the
test store. This signals that the agent was woken by the deletion trigger
for that queue.
