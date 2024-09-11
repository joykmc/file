#!/usr/bin/env python

import os
import time
import subprocess
import json
import glob

def wait_for_device(path):
    print(f"Waiting for {path}...")
    while not os.path.exists(path):
        time.sleep(1)

def log_os_system(cmd):
    status = os.system(cmd)
    if status:
        print('Failed :'+cmd)
    return  status

def wait_for_service(service_name):
    print(f"Waiting {service_name} active...")
    while True:
        result = subprocess.run(f"systemctl is-active {service_name}", shell=True, capture_output=True, text=True)
        if result.stdout.strip() == "active":
            break
        time.sleep(1)

def init_sensor_device():
    log_os_system("echo ina3221 0x42 > /sys/bus/i2c/devices/i2c-151/new_device")
    log_os_system("echo ina3221 0x42 > /sys/bus/i2c/devices/i2c-152/new_device")
    log_os_system("echo pxe1410 0x5a > /sys/bus/i2c/devices/i2c-155/new_device")
    log_os_system("echo pxe1410 0x5a > /sys/bus/i2c/devices/i2c-156/new_device")
    log_os_system("echo pxe1410 0x5a > /sys/bus/i2c/devices/i2c-157/new_device")
    log_os_system("echo pxe1410 0x5a > /sys/bus/i2c/devices/i2c-158/new_device")
    log_os_system("echo ina3221 0x41 > /sys/bus/i2c/devices/i2c-182/new_device")
    log_os_system("echo ina3221 0x41 > /sys/bus/i2c/devices/i2c-183/new_device")
    log_os_system("echo ina3221 0x41 > /sys/bus/i2c/devices/i2c-184/new_device")
    log_os_system("echo ina3221 0x41 > /sys/bus/i2c/devices/i2c-185/new_device")
    log_os_system("echo mp2975 0x76 > /sys/bus/i2c/devices/i2c-190/new_device")
    log_os_system("echo mp2975 0x76 > /sys/bus/i2c/devices/i2c-191/new_device")
    log_os_system("echo mp2975 0x76 > /sys/bus/i2c/devices/i2c-192/new_device")
    log_os_system("echo ina3221 0x40 > /sys/bus/i2c/devices/i2c-193/new_device")
    log_os_system("echo mp2975 0x76 > /sys/bus/i2c/devices/i2c-194/new_device")
    log_os_system("echo mp2975 0x76 > /sys/bus/i2c/devices/i2c-195/new_device")
    log_os_system("echo mp2975 0x76 > /sys/bus/i2c/devices/i2c-196/new_device")
    log_os_system("echo xdpe12284 0x74 > /sys/bus/i2c/devices/i2c-197/new_device")
    log_os_system("echo ina3221 0x40 > /sys/bus/i2c/devices/i2c-198/new_device")
    log_os_system("echo ina3221 0x40 > /sys/bus/i2c/devices/i2c-199/new_device")
    log_os_system("echo xdpe12284 0x74 > /sys/bus/i2c/devices/i2c-200/new_device")
    log_os_system("echo ina3221 0x40 > /sys/bus/i2c/devices/i2c-206/new_device")
    log_os_system("echo ina3221 0x40 > /sys/bus/i2c/devices/i2c-207/new_device")
    log_os_system("echo mp2975 0x76 > /sys/bus/i2c/devices/i2c-208/new_device")
    log_os_system("echo mp2975 0x76 > /sys/bus/i2c/devices/i2c-209/new_device")
    log_os_system("echo mp2975 0x76 > /sys/bus/i2c/devices/i2c-210/new_device")
    log_os_system("echo mp2975 0x76 > /sys/bus/i2c/devices/i2c-211/new_device")
    log_os_system("echo xdpe12284 0x74 > /sys/bus/i2c/devices/i2c-212/new_device")
    log_os_system("echo mp2975 0x76 > /sys/bus/i2c/devices/i2c-213/new_device")

def link_path_fuc(original_path, link_path):
    if os.path.exists(link_path):
        if os.path.exists(original_path):
            command = "rm" + " " + original_path + " 2>/dev/null"
            log_os_system(command)
        os.symlink(link_path, original_path)
    else:
        log_os_system("rm -rf {} 2>/dev/null".format(original_path))
        command = "echo NA > " + original_path
        log_os_system(command)

def init_s3ip_sysfs_stage2():
    json_file = '/usr/share/sonic/device/x86_64-baidu-r0/BN450L0/s3ip_sysfs_conf.json'
    with open(json_file, 'r', encoding='utf-8') as file:
        json_string = json.load(file)
        for s3ip_sysfs_path in json_string['s3ip_syfs_paths']:
            if (s3ip_sysfs_path['type'] != "path"):
                continue
            if (('vol_sensor/vol' in s3ip_sysfs_path['path']) or ('curr_sensor/curr' in s3ip_sysfs_path['path'])):
                if 'hwmon' in s3ip_sysfs_path['value']:
                    link_path = glob.glob(s3ip_sysfs_path['value'])[0]
                else:
                    link_path = s3ip_sysfs_path['value']
                link_path_fuc(s3ip_sysfs_path['path'], link_path)

def main():
    device_path = "/sys/bus/i2c/devices/i2c-213"
    service_name = "s3ip-sysfs.service"

    wait_for_device(device_path)

    init_sensor_device()

    wait_for_service(service_name)

    init_s3ip_sysfs_stage2()


if __name__ == "__main__":
    main()
