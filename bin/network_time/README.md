Time-Service
=======================================

This service uses roughtime service to set the time of the machine at startup.

It tries to get and update system time 3 times (sleeping for 10 seconds between
re-tries) and exits.
