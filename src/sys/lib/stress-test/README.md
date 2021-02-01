# Stress Test Library

This library provides convenience functions to create and run stress tests.

A stress test comprises of the following public concepts:
* Environment: Exists for the entire duration of the stress test
* Actor: Performs operations of a particular type

Test writers must write an environment and multiple actors.

This library offers functions to host the environment and the actors
in it for:
* a given time limit
* a given number of operations

Actors can either do work or attempt to interfere with the state of the environment.
As a result, an actor can request the environment to be reset after an operation.