# Node configuration files

## Adding an entry into a node config file
In most cases, adding a new node entry into a node config file is simple and doesn't require any
extra thought. In some cases, care should be taken regarding the ordering of entries in the config
file. Nodes are created in the order that they are listed within the node config file. Therefore,
there are some rules that should be followed:

### Node dependencies
If a node has a dependency on another node (that is, the node entry contains the "dependencies"
key), then care should be taken that the dependent node comes after those nodes listed under
"dependencies".

### Driver dependencies
If the DriverManagerHandler node is used to connect to drivers in the system, then care should be
taken to list the DriverManagerHandler node before any of the nodes that have driver dependencies.
