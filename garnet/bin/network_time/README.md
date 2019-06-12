Network Time
============

This service uses roughtime service to set the time of the machine at
startup. (Specifically, `network_time` is launched at startup by `sysmgr`. See
`//garnet/bin/sysmgr/config/network.config: "apps"`.)

It tries to get and update system, sleeping briefly between re-tries, and exits.
