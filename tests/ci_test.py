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
from random import randint

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


def esppoolGrabDevice(deviceId, user):
    command = 'esppool -g %s -u %s' % (deviceId, user)
    p = subprocess.Popen(command, shell=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    output, errors = p.communicate()
    output = output.decode('utf-8')
    if ("Oops" in output):
        print("Resource %s is already grabbed by another user" % deviceId)
        return 0
    elif("Successfully" in output):
        return 1
    else:
        raise Exception(output)


def esppoolReleaseDevice(deviceId, user):
    command = 'esppool -r %s -u %s' % (deviceId, user)
    p = subprocess.Popen(command, shell=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    p.communicate()


def esppoolResetDevice(deviceId, user):
    command = 'esppool -i %s -u %s' % (deviceId, user)
    p = subprocess.Popen(command, shell=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    output, errors = p.communicate()
    output = output.decode('utf-8')
    devDict = eval(output.strip())

    ssh = paramiko.SSHClient()
    ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    ssh.connect(devDict[deviceId]['ip'], 22, devDict[deviceId]['username'], devDict[deviceId]['password'], timeout=60)
    prefix_cmd = "timeout 60s SerialCmd --port %s --timeout 60" % devDict[deviceId]['port']
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


def esppoolFlashDevice(deviceId, user, address, bin_path):
    command = 'esppool -f %s -a %s -b %s -u %s' % (deviceId, address, bin_path, user)
    p = subprocess.Popen(command, shell=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    p.communicate()


def esppoolMonitorOutput(deviceId, user, tout=60):
    command = 'esppool -m %s -u %s' % (deviceId, user)
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

    if (args.chip == "esp32c3"):
        iram_mask = "0x4038"
        dram_mask = "0x3fc8"
        reserved_rom_dram_mask = "0x3fcd"
    elif (args.chip == "esp32s3"):
        iram_mask = "0x4037"
        dram_mask = "0x3fc9"
        reserved_rom_dram_mask = "0x3fce"

    # Generate a random user so that the CI instance doesn't grab the same device
    user_id = randint(1000, 9999)
    user = "regression_priv_sep_ci_%d" % user_id
    device_grabbed = 0
    freeDevice = ""
    try:
        while(not device_grabbed):
            freeDevice = esppoolGetFreeDevice(args.chip)
            if (freeDevice == ""):
                raise Exception("No free device available")

            print("Using device " + freeDevice)

            device_grabbed = esppoolGrabDevice(freeDevice, user)
        print("Flashing to device...")
        esppoolFlashDevice(freeDevice, user, '0x0', (build + '/bootloader/bootloader.bin'))
        esppoolFlashDevice(freeDevice, user, '0x8000', (build + '/partition_table/partition-table.bin'))
        esppoolFlashDevice(freeDevice, user, '0x10000', (build + '/violation_test.bin'))
        esppoolFlashDevice(freeDevice, user, '0x110000', (build + '/user_app/user_app.bin'))

        output = esppoolResetDevice(freeDevice, user)
    finally:
        esppoolReleaseDevice(freeDevice, user)

    print(output)

    expected_output = [
            'Illegal IRAM access: Fault addr: 0x4000',
            'Deleting irom_task',
            'Illegal IRAM access: Fault addr: 0x4037',
            'Deleting icache_access_t',
            'Deleting icache_execute_',
            'Illegal IRAM access: Fault addr: %s' % iram_mask,
            'Deleting iram_execute_ta',
            'Deleting iram_write_task',
            'Deleting iram_read_task',
            'Illegal DRAM access: Fault addr: 0x3ff1',
            'Deleting drom_task',
            'Illegal DRAM access: Fault addr: %s' % dram_mask,
            'Deleting dram_write_task',
            'Deleting dram_read_task',
            'Illegal DRAM access: Fault addr: %s' % reserved_rom_dram_mask,
            'Deleting rom_reserve_wri',
            'Deleting rom_reserve_rea',
            'Illegal RTC access:',
            'Deleting rtc_task',
            'Illegal Peripheral access:',
            'Deleting pif_task',
            'Illegal Flash Icache access:',
            'Deleting flash_icache_ta',
            'Illegal Flash Dcache access',
            'Deleting flash_dcache_ta',
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
