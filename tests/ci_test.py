#!/usr/bin/env python

# Copyright 2020-2022 Espressif Systems (Shanghai) PTE LTD
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#         http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import os
import re
import subprocess
import string
import argparse
import paramiko
import time
from threading import Timer

def esppoolGetFreeDevice(chip):
    command = 'device=$(esppool -l | grep \'' + chip + ' .*Free\' -m 1 | cut -d"|" -f2) && echo "device:"$device'
    p = subprocess.Popen(command, shell=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    output, errors = p.communicate()
    output = output.decode('utf-8')
    if ("device: " in output):
        deviceId = output.partition("device: ")[2]
        deviceId = deviceId.partition(chip)[0] + deviceId.partition(chip)[1]
        return deviceId
    else:
        return ""


def esppoolGrabDevice(deviceId):
    command = 'esppool -g ' + deviceId
    p = subprocess.Popen(command, shell=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    p.communicate()


def esppoolReleaseDevice(deviceId):
    command = 'esppool -r ' + deviceId
    p = subprocess.Popen(command, shell=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    p.communicate()


def esppoolResetDevice(deviceId):
    command = 'esppool -i %s -u regression' % (deviceId)
    p = subprocess.Popen(command, shell=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    output, errors = p.communicate()
    output = output.decode('utf-8')
    devDict = eval(output.strip())

    ssh = paramiko.SSHClient()
    ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    ssh.connect(devDict[deviceId]['ip'], 22, devDict[deviceId]['username'], devDict[deviceId]['password'], timeout=60)
    prefix_cmd = "timeout 60s SerialCmd --port %s --timeout 30" % devDict[deviceId]['port']
    stdin, stdout, stderr = ssh.exec_command('%s connect' % prefix_cmd)
    time.sleep(1)
    stdin, stdout, stderr = ssh.exec_command('%s reset' % prefix_cmd)
    try:
        outlines = stdout.readlines()
        resp = ''.join(outlines)
    except:
        resp = stdout.read()
    time.sleep(1)
    stdin, stdout, stderr = ssh.exec_command('%s disconnect' % prefix_cmd)
    ssh.close()
    return resp


def esppoolFlashDevice(deviceId, address, bin_path):
    command = 'esppool -f ' + deviceId + ' -a ' + address + ' -b ' + bin_path
    p = subprocess.Popen(command, shell=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    p.communicate()


def esppoolMonitorOutput(deviceId, tout=60):
    command = 'esppool -m ' + deviceId
    p = subprocess.Popen(command, shell=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    timer = Timer(tout, p.kill)
    try:
        timer.start()
        output, error = p.communicate()
    finally:
        timer.cancel()

    return output, error


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Run privilege-separation tests')
    parser.add_argument('--build', help='Path to build directory')
    parser.add_argument('--chip', help='Target chip type')

    args = parser.parse_args()
    build = args.build

    freeDevice = esppoolGetFreeDevice(args.chip)
    if (freeDevice == ""):
        raise Exception("No free device available")

    print("Using device " + freeDevice)

    esppoolGrabDevice(freeDevice)
    print("Flashing to device...")
    esppoolFlashDevice(freeDevice, '0x0', (build + '/bootloader/bootloader.bin'))
    esppoolFlashDevice(freeDevice, '0x8000', (build + '/partition_table/partition-table.bin'))
    esppoolFlashDevice(freeDevice, '0x10000', (build + '/violation_test.bin'))
    esppoolFlashDevice(freeDevice, '0x110000', (build + '/user_app/user_app.bin'))

    output = esppoolResetDevice(freeDevice)
    print(output)

    expected_output = [
            'Illegal IRAM access: Fault addr: 0x4038',
            'Illegal DRAM access: Fault addr: 0x3fcd',
            'Illegal RTC access:',
            'Illegal Peripheral access:',
            'Illegal Flash Icache access:',
            'Illegal DRAM access: Fault addr: 0x3fcd',
            'Illegal IRAM access: Fault addr: 0x4037',
            'Failed to take semaphore with handle = 0x1',
            'Failed to give semaphore with handle = 0x1',
            'Failed to give semaphore with handle = handle + 4',
            'Failed to take semaphore with handle = handle - 4',
            'Semaphore handle manipulation test complete',
            'Failed to get priority with handle = handle - 4',
            'Failed to get stack watermark with handle = handle + 1',
            'Task handle manipulation test complete',
            'Failed to create esp_timer with handle = handle - 4',
            'Failed to stop esp_timer with handle = handle + 4',
            'Failed to delete esp_timer_handle with handle = 0x1',
            'ESP-Timer handle manipulation test complete',
            'Failed to start xTimer for handle = SemaphoreHandle_t',
            'Failed to stop xTimer for handle = handle - 4',
            'Failed to delete xTimer for handle = SemaphoreHandle_t',
            'xTimer handle manipulation test complete'
            ]

    for line in expected_output:
        if (line not in output):
            print("Test failed.\nExpected: %s" % line)
            print("Got:\n%s" % output)
            raise RuntimeError("Test failed")

    print("Test passed")
