# Trace Json To CSV for playback

This Go program takes in a tracing JSON file, parses it and
outputs a csv file for a playback utility
//zircon/system/uapp/blk-playback/blk-playback.c
which re-creates events that have previously occured on a block device.

## Format of Input Tracing JSON File

A typical tracing data looks like this；
```{"cat":"sdmmc","name":"sdmmc_do_txn","ts":182697199.10074628,"pid":2759,"tid":3625,"ph":"b","id":683,"args":{"command":1,"extra":0,"length":16,"offset_vmo":0,"offset_dev":26137104,"pages":"0x0"}}```
Here, "sdmmc" is the category, "sdmmc_do_txn" is the task name. It starts on 182697199.10074628 microsecond with pid 2759 and tid 3625.

## driver-trace-parsing.go

A Go program that takes the following parameters in this order:
* driver_trace_parsing
* trace_category
* trace_in_file_name
* csv_out_file_name

The output is a CSV file which contains information needed by the playback utility.

## Format of the output CSV file

A CSV file with the following information is generated:
* Category
* Name
* Timestamp start (microsecond)
* Timestamp end (microsecond)
* Duration (micosecond)
* Tid
* Length (bytes)
* Device Offset (blocks)
* Task command
* VMO Offset (blocks)

Example:
category,name,start_time,end_time,duration,tid,length,offset_dev,command,offset_vmo
sdmmc,sdmmc_do_txn,55833382.11069652,55833810.5920398,428.481343,3496,8192,4177922,417792