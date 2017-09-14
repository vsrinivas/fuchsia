listen
======

This implements a simple program that listens for connections on a particular TCP port and spawns a program for each
connection. Connecting the socket to fdio stdin and passing the parent's stdout & stderr.

It's currently not possible to pass the socket as more than one start-up handle.
