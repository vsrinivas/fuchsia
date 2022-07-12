LoWPAN monitor
==============

`lowpan-monitor` is a watchdog process which monitors the LoWPAN driver
(`lowpan-ot-driver` in this case) and makes sure that it is restarted
properly on the rare occasion that it experiences a crash or panic.

`lowpan-monitor` is in the same package as lowpan-ot-driver.
