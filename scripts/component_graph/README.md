# Component Graph
The component graph provides a HTTP server written in python designed to be run on
developer machines to view the complete component graph for their system state.
This component graph is generated from data provided by the Fuchsia package
manager system.

All packages provided by the Fuchsia Package manager are parsed and all
available meta/ files are added as json objects to each package. By deep
scanning every single package the tool is able to retrieve almost all component
files that run on the system.

Currently parsing componentns from the ZBI is not supported. However this is planned future work.

## Getting Started
The component graph is a tool that can be run from any fuchsia development
machine. It is accessed from the webbrowser at localhost:8080

```
fx serve
python3 -m server
# For more information on configuring the server see.
python3 -m server --help
>>>>>>> [tools] - Component Graph.
```

## Testing
```
python3 -m unittest
```
=======

## APIs
* localhost:8080/api/component/graph - Complete graph for Fuchsia.
* localhost:8080/api/component/packages - Packages in Fuchsia with metadata extracted already.
* localhost:8080/api/component/services - Services in Fuchsia with their components.

## Far Reader
The Far Reader will take as input a far package and output the decoded result as a json file.

```
python3 far_reader.py package.far
```
