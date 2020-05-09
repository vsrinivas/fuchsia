#!/usr/bin/env python3

### Copyright 2020 The Fuchsia Authors. All rights reserved.
### Use of this source code is governed by a BSD-style license that can be
### found in the LICENSE file.

from pprint import pformat as pp

import datetime
import json
import requests
import time
import subprocess
### Run this program by install python and running 'python(3) battery_simulator_sl4f.py'
# Make sure 'fx serve' is running and 'fx shell run sl4f.cmx' is running. It is not necessary
# for the CLI to be running in the background.

#Initialize script requirements
bash_command = "fx get-device-addr"
ip = subprocess.check_output(['bash', '-c', bash_command])[:-1].decode('UTF-8')
port = 80
address = u'http://[{}]:{}'.format(ip, port)

# Check if `fx shell run sl4f.cmx` is running
bash_command = "fx shell ps"
is_running = str(subprocess.check_output(['bash', '-c', bash_command])[:-1])
if(is_running.find('sl4f.cmx') == -1):
    print("Error: `fx shell run sl4f.cmx` is not running")

init_address = address + "/init"
cleanup_address = address + "/cleanup"

client_id = 0
test_id = 0


def gen_id():
    global test_id
    test_id += 1
    return "{}.{}".format(client_id, test_id)


### INIT sl4f (note sl4f must be running on your Fuchsia device).
# Run SL4F by running `fx shell run sl4f.cmx`
init_data = json.dumps({
    "jsonrpc": "2.0",
    "id": gen_id(),
    "method": "sl4f.sl4f_init",
    "params": {
        "client_id": str(client_id)
    }
})
# Increment test_id
print("{}\n".format(pp(requests.get(url=init_address, data=init_data))))

### Now to test your Function
#Change function to fit your usecase. This current example

### Initialize battery simulator SL4F
data = json.dumps({
    "jsonrpc": "2.0",
    "id": gen_id(),
    "method": "battery_simulator.SimulatorInit",
    "params": {}
})
result = requests.get(url=address, data=data).json()
error = result.get('error')
if error is not None:
    print("Error: {}".format(error))

### Disconnect the Real Battery
data = json.dumps({
    "jsonrpc": "2.0",
    "id": gen_id(),
    "method": "battery_simulator.DisconnectRealBattery",
    "params": {}
})
result = requests.get(url=address, data=data).json()
error = result.get('error')
if error is not None:
    print("Error: {}".format(error))

### Decrease Battery Percentage by `percent_decrement_step`% every `wait_seconds` seconds `num_iterations` times.
percent_decrement_step = 1
wait_seconds = 1
num_iterations = 10
percent = 50  # Starting battery percentage
for i in range(num_iterations):
    data = json.dumps({
        "jsonrpc": "2.0",
        "id": gen_id(),
        "method": "battery_simulator.BatteryPercentage",
        "params": {
            "message": percent
        }
    })
    result = requests.get(url=address, data=data).json()
    error = result.get('error')
    if error is not None:
        print("Error: {}".format(error))
    else:
        print("Battery percentage now at", percent)
    time.sleep(wait_seconds)
    percent -= percent_decrement_step

### Reconnect Real Battery

data = json.dumps({
    "jsonrpc": "2.0",
    "id": gen_id(),
    "method": "battery_simulator.ReconnectRealBattery",
    "params": {}
})
result = requests.get(url=address, data=data).json()
error = result.get('error')
if error is not None:
    print("Error: {}".format(error))

### Now to clean up sl4f
data = json.dumps({
    "jsonrpc": "2.0",
    "id": gen_id(),
    "method": "sl4f.sl4f_cleanup",
    "params": {}
})

print("{}\n".format(pp(requests.get(url=cleanup_address, data=data).json())))
