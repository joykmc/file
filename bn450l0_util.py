#!/usr/bin/env python


"""
Usage: %(scriptName)s [options] command object

options:
    -h | --help         : this help message
    -d | --debug        : run with debug mode
    -f | --force        : ignore error during installation or clean
command:
    install                       : install and enable platform service list
    clean                         : uninstall and disable platform service list
    fast-reboot-install           : switch to fast-reboot, installing platform service
    list-service | --list-service : switch support service list
    prestart                      : platform module baidu Execstartpre
    poststart                     : platform module baidu ExecStartPost
    poststop                      : platform module baidu ExecStopPost
    start_stage1                  : platform module baidu stage1 ExecStart
    stop_stage1                   : platform module baidu stage1 ExecStop
    start_stage2                  : platform module baidu stage2 ExecStart
    stop_stage2                   : platform module baidu stage2 ExecStop
    prestart_stage1               : platform module baidu stage1 ExecStartPre
    poststart_stage2              : platform module baidu stage2 ExecStartPost
    poststop_stage1               : platform module baidu stage1 ExecStopPost
    poststop_stage2               : platform module baidu stage2 ExecStopPost
"""

from asyncio import FastChildWatcher
import logging
import getopt
import os
import syslog
import click
import sys
import subprocess
import time



INSTALLED_FILE = '/run/platform-modules-baidu.installed'

version = '1.1'
verbose = False
DEBUG = False
args = []
ALL_DEVICE = {}
FORCE = 0


PORT_NUM = 128
# TODO Add service list
SERVICE_LIST = ['ieit-driver-init.service', 's3ip-sysfs.service', 's3ip-sysfs-monitor.service',
                'fan-monitor.service', 'led-monitor.service']

SYSLOG_IDENTIFIER = "BN450L0_UTIL"

def log_info(msg, also_print_to_console=True):
    global DEBUG

    syslog.openlog(SYSLOG_IDENTIFIER)
    syslog.syslog(syslog.LOG_INFO, msg)
    syslog.closelog()

    if also_print_to_console or DEBUG:
        click.echo(msg)


def log_warning(msg, also_print_to_console=True):
    global DEBUG

    syslog.openlog(SYSLOG_IDENTIFIER)
    syslog.syslog(syslog.LOG_WARNING, msg)
    syslog.closelog()

    if also_print_to_console or DEBUG:
        click.echo(msg)


def log_error(msg, also_print_to_console=True):
    global DEBUG

    syslog.openlog(SYSLOG_IDENTIFIER)
    syslog.syslog(syslog.LOG_ERR, msg)
    syslog.closelog()

    if also_print_to_console or DEBUG:
        click.echo(msg)

def log_dbg(msg, also_print_to_console=False):
    global DEBUG

    syslog.openlog(SYSLOG_IDENTIFIER)
    syslog.syslog(syslog.LOG_DEBUG, msg)
    syslog.closelog()

    if also_print_to_console or DEBUG:
        click.echo(msg)

if DEBUG == True:
    print(sys.argv[0])
    print('ARGV      :', sys.argv[1:])

def main():
    global DEBUG
    global args
    global FORCE
    global kos

    if len(sys.argv)<2:
        show_help()

    options, args = getopt.getopt(sys.argv[1:], 'hdfvl', ['help',
                                                       'debug',
                                                       'force',
                                                       'version',
                                                       'list-service'
                                                          ])
    if DEBUG == True:
        print(options)
        print(args)
        print(len(sys.argv))

    for opt, arg in options:
        if opt in ('-h', '--help'):
            show_help()
        elif opt in ('-d', '--debug'):
            DEBUG = True
        elif opt in ('-f', '--force'):
            FORCE = 1
        elif opt in ('-l', '--list-service'):
            print_support_service_list()
        else:
            logging.info('no option')

    for arg in args:
        if arg == 'install':
            do_install()
        elif arg == 'clean':
            do_uninstall()
        elif arg == 'prestart':
            do_prestart()
        elif arg == 'poststart':
            do_poststart()
        elif arg == 'poststop':
            do_poststop()
        elif arg == 'start_stage1':
            do_install()
        elif arg == 'stop_stage1':
            do_stop_stage1()
        elif arg == 'start_stage2':
            do_start_stage2()
        elif arg == 'stop_stage2':
            do_stop_stage2()
        elif arg == 'prestart_stage1':
            do_prestart_stage1()
        elif arg == 'prestart_stage2':
            do_prestart_stage2()
        elif arg == 'poststart_stage1':
            do_poststart_stage1()
        elif arg == 'poststart_stage2':
            do_poststart_stage2()
        elif arg == 'poststop_stage1':
            do_poststop_stage1()
        elif arg == 'poststop_stage2':
            do_poststop_stage2()
        elif arg == 'fast-reboot-install':
            return
        elif arg == 'list-service':
            print_support_service_list()
        elif arg  == 'version':
            print(version)
        else:
            show_help()

    return 0

def get_json_version():
    return 0

def show_help():
    print(__doc__ % {'scriptName' : sys.argv[0].split("/")[-1]})
    sys.exit(0)

def my_log(txt):
    if DEBUG == True:
        print("[PLATFORM]"+txt)
    return

def log_os_system(cmd):
    #status, output = subprocess.getstatusoutput(cmd)
    if DEBUG:
        log_dbg("SHELL CMD: {}".format(cmd))
    status = os.system(cmd)
    if status:
        log_error('Failed :'+cmd)
    return  status

def handle_transceiver_init():
    if not os.path.isfile(INSTALLED_FILE):
        ## enable port power
        cmd = 'echo 1 > /sys/bus/i2c/devices/17-000e/all_port_power_on'
        log_os_system(cmd)

        # Set all cpld write_enable enable, one global control in every cpld
        cmd = "echo 89 > /sys_switch/transceiver/eth1/write_enable"
        log_os_system(cmd)

        cmd = "echo 89 > /sys_switch/transceiver/eth5/write_enable"
        log_os_system(cmd)

        cmd = "echo 89 > /sys_switch/transceiver/eth67/write_enable"
        log_os_system(cmd)

        # set lpmode and reset
        for i in range(0, PORT_NUM):
            cmd = "echo 1 > /sys_switch/transceiver/eth{}/low_power_mode".format(i+1)
            log_os_system(cmd)

            cmd = "echo 0 > /sys_switch/transceiver/eth{}/reset".format(i+1)
            log_os_system(cmd)


def handle_transceiver_deinit():
    if not os.path.isfile(INSTALLED_FILE):
        ## disable port power
        cmd = 'echo 0 > /sys/bus/i2c/devices/17-000e/all_port_power_on'
        log_os_system(cmd)

        # Set all cpld write_enable enable
        cmd = "echo 89 > /sys_switch/transceiver/eth1/write_enable"
        log_os_system(cmd)

        cmd = "echo 89 > /sys_switch/transceiver/eth5/write_enable"
        log_os_system(cmd)

        cmd = "echo 89 > /sys_switch/transceiver/eth67/write_enable"
        log_os_system(cmd)

        # set lpmode and reset
        for i in range(0, PORT_NUM):
            cmd = "echo 0 > /sys_switch/transceiver/eth{}/low_power_mode".format(i+1)
            log_os_system(cmd)

            cmd = "echo 1 > /sys_switch/transceiver/eth{}/reset".format(i+1)
            log_os_system(cmd)

def exec_syscmd(cmd):
    try:
        sub_process = subprocess.Popen(cmd, shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        out, err = sub_process.communicate()
        return sub_process.returncode, str(out, encoding="utf-8").strip()
    except Exception as err:
        log_error("Command: {0} exec fail, get{1}.".format(cmd, str(err)))

    return 1, None

def is_service_deactivating(name):
    ret, is_active = exec_syscmd("systemctl is-active {}".format(name))
    if is_active == "deactivating":
        return True

    return False

def is_service_active(name):
    ret, is_active = exec_syscmd("systemctl is-active {}".format(name))
    if is_active == "active":
        return True

    return False

def is_service_enable(name):
    ret, is_enabled = exec_syscmd("systemctl is-enabled {}".format(name))
    if is_enabled == "enabled":
        return True

    return False

def is_service_masked(name):
    try:
        result = subprocess.run(
            ['systemctl', 'status', name],
            capture_output=True,
            text=True
        )
        return 'masked' in result.stdout
    except Exception as e:
        log_error("Service adjust if masked exec fail")
        return False

def wait_for_systemd_service_ready(name):
    for i in range(60):
        if is_service_active(name):
            break
        time.sleep(0.5)

    if i >= 60:
        log_error("Service already enabled, wait active failed...")
        return False

    return True

def start_systemd_service(service):
    ret = 0

    if is_service_masked(service):
        ret = log_os_system("systemctl unmask {}".format(service))

    if not is_service_enable(service):
        ret += log_os_system("systemctl enable {}".format(service))
        ret += log_os_system("systemctl start {}".format(service))
        return ret
    wait_for_systemd_service_ready(service)
    return ret

def stop_systemd_service(service):
    log_os_system("systemctl disable {}".format(service))
    if service == 'ieit-driver-init.service':
        log_os_system("systemctl stop {}".format(service))
    else:
        log_os_system("systemctl stop {}".format(service))


def print_support_service_list():
    for service in SERVICE_LIST:
        print(service)


def do_uninstall():
    global FORCE
    log_info("BN450L0_util stop....")

    if not os.path.isfile(INSTALLED_FILE) and not FORCE:
        log_info("Missing installed file, skip clean action...")
        return

    handle_transceiver_deinit()

    for service in SERVICE_LIST[::-1]:
        ret = stop_systemd_service(service)
        if ret:
            log_error("start systemd service fail: {}".format(service))

    return

def do_prestart():
    log_os_system("")

def do_poststart():
    return

def do_poststop():
    return

def do_poststart():
    return

def do_poststart():
    return

# def do_start_stage1():
def do_install():
    global FORCE
    log_info("BN450L0_util start....")

    if os.path.isfile(INSTALLED_FILE) and not FORCE:
        log_info("Exist installed file, skip install action...")
        return

    for service in SERVICE_LIST:
        ret = start_systemd_service(service)
        if ret:
            log_error("start systemd service fail: {}".format(service))

    handle_transceiver_init()

    return

def do_start_stage2():
    # Install driver extend stage2 include vol and curr sensor
    log_info("start driver stage2 ")
    log_os_system("/usr/local/bin/driver_stage2.py")

    ret = start_systemd_service('sensor-monitor.service')
    if ret:
        log_error("start sensor monitor service fail")
    return

def do_stop_stage1():
    return

def do_stop_stage2():
    ret = stop_systemd_service('sensor-monitor.service')
    if ret:
        log_error("stop sensor monitor service fail")
    return

def do_prestart_stage1():
    return

def do_prestart_stage2():
    return

def do_poststart_stage1():
    return

def do_poststart_stage2():
    return

def do_poststop_stage1():
    return

def do_poststop_stage2():
    return


if __name__ == "__main__":
    main()
