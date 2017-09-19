# Cobalt

[TOC]

## Summary
Cobalt is a system for collecting metrics from Fuchsia devices, sending
metric observations to servers running in the cloud, aggregating and analyzing
the collected observations and generating useful reports and visualizations.
Cobalt emphasizes the preservation of user privacy while collecting
high-quality, useful analytics.

## Concepts

* **Customer** and **Project**. Cobalt is a multi-tenant system. We partition
the system using the notions of customer and project. For example
**Fuchsia** is a customer and **Ledger** is a project.
* **Metric**: A variable to be measured, e.g. the number of times that event
*E*, *F* or *G* occur within a Fuchsia system.
* **Observation**: A single measurement of a metric, e.g. "Event *E* occurred."
Observations are the units of data transmitted from the client running Fuchsia
to the Cobalt servers.
* **Encoding**: A built-in algorithm for encoding Observations. Cobalt includes
a few encodings and the Cobalt architecture is extensible, allowing new
encodings to be added in the future. The original batch of encodings developed
for Cobalt all revolved around privacy preservation. For example
*randomized response* is an encoding technique that collects category data with
intentional noise added for the sake of privacy-preservation. For example if
the true observation were that event *E* occurred, using randomized response
there is some probability that the encoded observation would indicate instead
that event *F* occurred. For use cases for which privacy-preservation is not
feasible or relevant, we are now developing additional encodings which
emphasize expressiveness, accuracy and ease of use.
* **Encoding Configuration:** A specification of particular parameters of an
encoding to use, e.g. randomized response with p=0.1 and q=0.9 would mean to
use the randomized response encoding with a randomized noise level of 10%.
Also some encodings require that they be configured with a pre-defined list
of possible observation values. For example specifying the set of possible
values *{"Event E", "Event F", "Event G"}* might be part of an encoding
configuration for some encoding types.

As of this writing,  the way to use Cobalt to collect metrics when added noise
for privacy preservation is *not* desired is to pick one of the existing privacy
preserving encodings and use an encoding *configuration* in
which the noise level is set to zero, e.g. randomized response with p=0.0 and
q=1.0. This approach leads to a more awkward interface than one might like.
We will soon have new more convenient and powerful encodings which will make
this situation better.
* **Report Configuration** A specification of a collection of Observations
and a manner of aggregating them into a report, e.g. A histogram of the
number of times that events *E*, *F* and *G* occurred yesterday.

* **Config Registration**. To use Cobalt from within Fuchsia, you must have a
project, and one or more metrics, encoding configs and report configs
registered with Cobalt's configuration system.

## Cobalt's Config Registration System
In the current version of Cobalt, config registration consists of entries in
files checked into source control: `registered_encodings.txt` and
`registered_reports.txt` in `//third_party/cobalt/config/production`. Since the
Cobalt servers also read these files, for now it is necessary to coordinate with
cobalt-hackers@google.com in order to register additional projects, metrics,
encodings and reports. We plan to build an online self-registration system in
the future.

### How to register additional configurations.

You have two options. One option is to send an e-mail to
cobalt-hackers@google.com (or contact one of us personally if you prefer)
and describe what new data you are trying to collect. We would be happy
to discuss with you the different options for how to accomplish what you
want and then add the additional configuration ourselves. This is probably
the right choice for you for your initial couple of interactions with
Cobalt.

If you prefer you may instead edit some files in source control yourself
and send us a CL.

* In `//third_party/cobalt/config/production` make the appropriate changes to
  * `registered_encodings.txt`
  * `registered_metrics.txt`
  * `registered_reports.txt`

If you are not sure what the appropriate changes are then use the first option
mentioned above--ask cobalt-hackers@google.com to do it for you.

These files are consumed by the Cobalt servers and so after the CL is committed,
the Cobalt team needs to push the changes to the servers.

*WARNING:* One potentially confusing issue is that the Fuchsia Jiri manifests
pull a pinned commit of Cobalt rather than pulling Cobalt at head. This is
configured via the "revision" property of the "cobalt" |project| entry in the
file //manifest/cobalt_client. This means that after committing
changes to the above files, if you then delete your local Cobalt branch and
do a `jiri update` then your local copy of //third_party/cobalt will not contain
the edits you just committed. This actually doesn't matter because the files you
just changed are not consumed by Fuchsia's Cobalt client. They are only consumed
by the server.

* In `//garnet/bin/cobalt/config.h` copy your changes from above. This file
contains some C++ string literals that are copies of the contents of the files
you changed above. This is what is consumed by Fuchsia's Cobalt client. This
hacky mechanism will be replaced by something more sensible in due course.

### Is it necessary to pre-register the values sent to Cobalt?
Cobalt employs different types of encodings and for some encodings some amount
of pre-registration of values may be required. We plan to develop additional
encodings where pre-registration will not be necessary. Here we mention a few
cases that are relevant to teams currently using Cobalt:
  * Basic RAPPOR with strings: If you are using this encoding then all string
    values sent to Cobalt must be pre-registered both on the client and on
    the server. When you wish to start sending a new string value you need
    to first register the value in the config files described above.
  * Basic RAPPOR with indexes: It is necessary to pre-register a maximum number
    of indices you will ever need. After that you may use any indices without
    further registration. You may lazily register labels associated to the
    indices in Cobalt's server-side config in the file
    *//third_party/cobalt/config/production/registered_reports.txt*. This will
    allow Cobalt to use the labels when generating reports. Prior to registering
    a label for an index Cobalt will generate reports with labels such as
    `<index 5>`.
  * Forculus: No registration of values is required. You send arbitrary
    strings to Cobalt and they appear in the reports.

## FIDL Interface
After registration, Fuchsia code uses Cobalt through it's FIDL interface.
See `//garnet/public/cobalt/fidl/cobalt.fidl`. Also see
`//garnet/bin/cobalt/cobalt_testapp.cc` for example usage of the FIDL
API.

## Report Client

We are working on building an online site for running and visualizing reports.
For now we offer a command-line client.

### Downloading a pre-built binary
Run the python script `download_report_client.py`. This will detect the platform
on which you are running and download a pre-built binary from Google Cloud
storage. The script will check the sha1 of the downloaded file.

### Build the binary yourself
If we do not have a pre-built binary for your platform you may be able to
build the binary yourself. The Go source code is located in the Cobalt repo.
You will have to follow the instructions in the README.md file their for
building Cobalt. The resulting binary will be located at
`out/tools/report_client`.

### How to use the report client
You invoke the report_client as follows:
```
./report_client -report_master_uri=35.188.119.76:7001 -project_id=<project_id>
```

This will enter an interactive command line interface.
In order to run a report for report config 1 over the two day period
consisting of two-days-ago and yesterday you enter the command:

```
run range -2 -1 1
```

You can also run in a non-interactive mode and generate a CSV file as follows:
```
./report_client -report_master_uri=35.188.119.76:7001  \
-project_id=<project_id> -interactive=false -first_day=-2 -last_day=-1 \
-report_config_id=1 -csv_file=report.csv
```

### Cobalt Test App

The Cobalt test app `cobalt_testapp.cc` serves as an example usage of the Cobalt
FIDL service.

### Building the test app:

For example:

```
$ source scripts/env.sh && envprompt
$ fset x86-64 --modules default,cobalt_client
$ fbuild
```

### Running the test app:

Start Fuchsia. For example:

```
$ frun -m 3000  -k -n
```

From within Fuchsia:

```
$ system/test/cobalt_testapp
```
 or try

```
$ system/test/cobalt_testapp --verbose=3
```
