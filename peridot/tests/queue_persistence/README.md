# queue_persistence integration test

This test exercises the APIs exposed to modules to connect to agents, and for
Modules and Agents to exchange message queues.

It specifically tests that message queues are persistent between invocations of
agents, and that a message sent to the message queue of agent while the agent is
not running can be received by the agent when it's started again.
