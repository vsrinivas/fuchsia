The check-licenses tool checks for compliance with Fuchsia's open source policy.

For information regarding Fuchsia's OSS policy, see public documentation at
https://fuchsia.dev/fuchsia-src/contribute/governance/policy/open-source-licensing-policies
or, for Googlers, http://go/fuchsia-oss-policy.

For information on adding new third party code to Fuchsia, see
https://fuchsia.dev/fuchsia-src/contribute/governance/policy/osrb-process.

Support:

* For policy issues, e.g. regarding compliance, reach out to the contacts
  listed in the OSS policy docs. Please do not contact the tool owners for legal
  support.
* For help resolving common check-licenses errors, see
  http://go/fuchsia-licenses-playbook.
* For technical issues with the check-licenses tool, see the OWNERS file.

Run:

```
$ fx build tools/check-licenses:host
$ fx check-licenses
```

Test:

```
$ fx set <PRODUCT>.<BOARD> --with //tools/check-licenses:tests
$ fx test check-licenses
```
