# Device settings service

This service exists to hold persistent state for services in Garnet. An instance
of it will be running in the critical path of verified execution, and a
separate instance will be started for services once a user has logged in to the
system.

The first instance does not support setting/getting arbitrary byte blobs, as a
form of discouraging clients from attempting to deserialize unsigned data.
