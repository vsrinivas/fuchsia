# How Filter works

               Incoming                           Outgoing

     +--------------------------+        +--------------------------+
     |           IP             +<--+    |           IP             |
     +------------+-------------+   |    +------------+-------------+
                  ^                 |                 |
                  |                 |                 v
     +------------+-------------+   |    +------------+-------------+
     |    Filter Rule runner    |   |    |           NAT            |
     +--------------------------+   |    +--------------------------+
     +--------------------------+   |    +--------------------------+
     | Connection state tracker +---+    | Connection state tracker +---+
     +--------------------------+        +--------------------------+   |
     +--------------------------+        +--------------------------+   |
     |      RDR(redirector)     |        |    Filter Rule runner    |   |
     +------------+-------------+        +------------+-------------+   |
                  ^                                   |                 |
                  |                                   v                 |
     +------------+-------------+        +------------+-------------+   |
     |      NIC(interface)      |        |      NIC(interface)      +<--+
     +--------------------------+        +------------+-------------+
                  ^                                   |
                  |                                   v


* For incoming packets
  * If filter is enabled on the NIC
    * Step 1: RDR(Redirector)
      * Try to match all RDR rules.
      * If any RDR rule is matched, the headers are rewritten accoring to the rule.
    * Step 2: Connection state tracker
      * Check if the packet is a part of existing connection.
      * If yes, skip Step 3.
    * Step 3: Filter Rule runner
      * Try to match every rule from top to bottom.
	    * If matched with a rule with the quick flag, the action (pass, drop) is taken immediately.
		* If matched with a rule without the quick flag, remember it as the last matched rule, and move on to the next rule. When reached at the bottom, the action of the last matched rule is taken.
      * If the action is pass, the connection is registered to Connection state tracker, and the packet is passed to IP.

* For outgoing packets
  * If filter is enabled on the NIC
    * Step 1: NAT
      * Try to match every NAT rule.
      * If any NAT rule is matched, the headers are rewritten accoring to the rule.
    * Step 2: Connection state tracker
      * Check if the packet is a part of existing connection.
      * If yes, skip Step 3.
    * Step 3: Filter Rule runner
      * Try to match every rule from top to bottom.
	    * If matched with a rule with the quick flag, the action (pass, drop) is taken immediately.
		* If matched with a rule without the quick flag, remember it as the last matched rule, and move on to the next rule. When reached at the bottom, the action of the last matched rule is taken.
      * If the action is pass, the connection is registered to Connection state tracker, and the packet is passed to NIC.
