The util package holds utility functions that are used throughout the
check-licenses project. These are standalone tools that can be called from
anywhere.

**git.go**: Helper functions used to run git commands. e.g. retrieving the URL
or git hash for a given repository.

**gn.go**: Helper functions used to run gn commands. e.g. generate a file that
describes the build graph for the current workspace or gn target.
