# Debug Agent Test Data

This directory contains all test applications and libraries developed for
testing and debugging the agent. This means utilities for things like spawning
processes, keeping threads looping and having a .so that is also linked to
generated binary.

Every new functionality that is not meant to be consumed by the end users but
rather by the developers of zxdb should put that code in here.
