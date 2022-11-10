# WLAN Policy

## Module Layout

This document lays out the main responsibilities of the modules within the WLAN Policy codebase. It is not intended to capture all nuances of the modules' behaviors, but rather provide an orientation to the layout.

Note: module names (e.g. Interface Manager) are capitalized throughout for clarity.

### Main Loop

Implemented in: [`main.rs`](./src/main.rs), [`client/mod.rs`](./src/client/mod.rs), [`access_point/mod.rs`](./src/access_point/mod.rs)

Responsiblities:

- Serves the FIDL protocols.
- Handles FIDL requests by making calls to other modules, mainly the Interface Manager and the Saved Networks Manager.

### Interface Manager

Implemented in: [`mode_management/iface_manager.rs`](./src/mode_management/iface_manager.rs)

Responsiblities:

- Asks Phy Manager to create interfaces as needed, creates a State Machine for each interface.
- Handles FIDL requests (e.g. Connect()) by taking action (e.g. starting scans, setting configs, etc.), and forwarding the call to a specific interface's State Machine.
- Monitors State Machines, handles exits (e.g. bring up new interface, reconfigure existing networks).
    - When a Client State Machine exits, uses the Network Selection Manager to find a new network to connect to.
    - Periodically checks for idle Client State Machines, if any are present, triggers a reconnect.

### Phy Manager

Implemented in: [`mode_management/phy_manager.rs`](./src/mode_management/phy_manager.rs)

Responsiblities:

- Monitors hardware additions/removals and keeps track of the hardware's capabilities.
- Creates client/ap interfaces for the PHYs.
- Selects which interfaces to tear down if more of another type of interface is needed (e.g. which client to tear down if more APs are needed).

### AP State Machine

Implemented in: [`access_point/state_machine.rs`](./src/access_point/state_machine.rs)

Responsiblities:

- Instantiated by the Interface Manager to manage a specific interface.
- Can be asked to StartAp(ApConfiguration) or StopAp().
- Can be asked to Exit().

### Client State Machine

Implemented in: [`client/state_machine.rs`](./src/client/state_machine.rs)

Responsiblities:

- Instantiated by the Interface Manager to manage a specific interface.
- Can be asked to Connect(NetworkId).
    - Notifies the Saved Networks Manager about connection failures / successes.
    - Periodically notifies the Saved Networks Manager about network statistics.
    - Attempts to reconnect to the configured network for a number of times before abandoning and exiting.
- On Error, exits while logging debug data.
- Can be asked to Disconnect(), which causes a graceful exit.
- On graceful exiting of the underlying interface, exits gracefully.

### Network Selection Manager

Implemented in: [`client/network_selection.rs`](./src/client/network_selection.rs)

Responsiblities:

- Provides an interface to query the best available network for connecting to.
- Performs scans as needed to find best available networks.
- Uses both an in-memory cache of network stats, as well as long-term info from the Saved Networks Manager.

### Saved Network Manager

Implemented in: [`config_management/config_manager.rs`](./src/config_management/config_manager.rs)

Responsiblities:

- Stores saved networks.
- Stores statistics about networks (e.g. has it ever connected or failed to connect).

### Scan Manager

Implemented in: [`client/scan/mod.rs`](./src/client/scan/mod.rs)

Responsiblities:

- Performs scans via the Interface Manager.
- Distributes scan results to the requester and Emergency Location provider.

### Telemetry

Implemented in: [`telemetry/mod.rs`](./src/telemetry/mod.rs)

Responsibilities:

- Receives TelemetryEvent on network selection and network state, and keep track of
  stats about connection uptime and downtime.
- Maintains stats for the last one day and seven days separately, discarding stale
  data when enough time has passed.
- Exposes stats via Inspect.

## Examples of data flow in common situations

The situations below illustrate how the modules cooperate to handle common scenarios. Similar to above, these situations aren't intended to capture every nuance of behavior.

### Application requests connection to a network

- Application sends a "FIDL::Connect(network: foo)" request.
- Main Loop dispatches it to Interface Manager.
- Network Selection Manager scans for compatible APs on network "foo". If found:
    - Interface Manager asks Phy Manager for an interface.
    - Phy Manager selects a PHY and creates an interface for it, if needed, preferring unused interfaces.
    - Interface Manager uses the existing Client State Machine or creates a new Client State Machine to perform the connection.
- Else:
    - Interface Manager may re-trigger scanning for an AP several times, eventually giving up.

### Interface Manager is notified of idle Client State Machine

- The Interface Manager requests a new "best network" from the Network Selection Manager.
- The Network Selection Manager performs scans as needed to find the best available network.
- If the Network Selection Manager provides a new network, the Interface Manager connects to this new network.
    - See separate section for ["Application requests connection to a network"](#application-requests-connection-to-a-network).

### Client State Machine detects network disonnect

- Client State Machine is connected to a network.
- Client State Machine detects that the interface is no longer connected to the network.
- It tries to reconnect a few times (telling the Saved Network Manager on each failure).
- After failing to reconnect, the Client State Machine exits.
    - See separate section for ["Interface Manager is notified of idle Client State Machine"](#interface-manager-is-notified-of-idle-client-state-machine).

### Application deletes a saved network

- Application sends a "FIDL::RemoveSavedNetwork(network Foo)" request.
- Main Loop requests the Saved Network Manager to delete Network Foo.
- Main Loop requests the Interface Manager to disconnect from Network Foo.
- For each Client State Machine connected to Network Foo, Interface Manager sends a "Disconnect()".
- Client State Machines that got disconnected exit.
    - See separate section for ["Interface Manager is notified of idle Client State Machine"](#interface-manager-is-notified-of-idle-client-state-machine).

### Bootup

- System starts wlancfg service.
- wlancfg service instantiates managers:
    - Phy Manager
    - Iface Manager
    - Saved Networks Manager
    - Scan Manager
- wlancfg creates futures to run:
    - Client + AP Main Loops
    - Device monitoring service
    - Legacy API
- Main Loop calls StartClientConnections() on the Interface Manager.
    - No interfaces are discovered at this point since the device monitoring service has not had an opportunity to run.
    - Just ensures that the "client connection enabled" flag is set, so client interfaces are created when new PHYs are discovered.
- A PHY should be discovered at some point by the device monitoring service.
    - An interface and Client State Machine will be created for it since client connections are enabled.
    - The Client State Machine will move to idle and notify the Main Loop.
    - See separate section for ["Interface Manager is notified of idle Client State Machine"](#interface-manager-is-notified-of-idle-client-state-machine).

### Application requests to Start Client Connections

- FIDL::StartClientConnections call comes in to Main Loop.
- Main Loop checks if clientConnections are already enabled. If yes, exit now with no-op.
- Main Loop tells Interface Manager to StartClientConnections().
- Interface Managers asks Phy Manager to CreateAllClientInterfaces().
- Phy Manager creates interface and Client State Machines for each interface, which will each move to idle.
    - See separate section for ["Interface Manager is notified of idle Client State Machine"](#interface-manager-is-notified-of-idle-client-state-machine).

### Application requests to Stop Client Connections

- FIDL::StopClientConnections call goes into Main Loop.
- Main Loop passes it to the Interface Manager.
- Interface Manager asks each Client State Machine to Disconnect().
- Interface Manager asks Phy Manager to stop all client connections.
- Phy Manager destroys all client interfaces gracefully by closing the SME connection gracefully.

### Regulatory domain change

- Note: this module is documented and implemented in [`src/connectivity/location/regulatory_region/README.md`](../../location/regulatory_region/README.md)
- Regulatory domain module uses the Interface Manager to stopClientConnections() and stopSoftAp().
- Regulatory domain module send new regulatory domain to driver.
- Driver should cause a removePhy / addPhy event sequence.
- If client connections were previously started, regulatory domain module asks the Interface Manager to startClientConnections().

### Application requests a SoftAP startup

- FIDL request for starting SoftAP comes in to Main Loop.
- Main Loop requests Interface Manager to start SoftAP.
- Interface Manager asks for an AP interface from Phy Manager.
- Phy Manager attempts to find an AP interface.
    - If all AP-capable interfaces are currently in use as clients, the Phy Manager will select one gracefully shut it down in order to repurpose it as an AP.
- Phy Manager returns an AP interface.
- Interface Manager creates an AP State Machine for that interface, and configures it.
