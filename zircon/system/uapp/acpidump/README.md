# acpidump

This tool performs a dump of the system's ACPI tables.

Usage:

*   `acpidump`: Perform a full dump of the ACPI tables, printing a hexdump of
    results.
*   `acpidump -s`: Print a summary of available ACPI tables.
*   `acpidump -t <table>`: Print the named ACPI table.
*   `acpidump -t <table> -b`: Print the named ACPI table in raw, binary form.

Raw binary form may be useful when using other tools to analyse the ACPI tables.
For example, to parse the `DSDT` ACPI table of a machine, run from your host
machine:

```sh
fx acpidump -t DSDT -b > dsdt.bin
iasl dsdt.bin
cat dsdt.dsl
```
