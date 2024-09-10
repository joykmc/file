#!/bin/bash

function setup_eth0_bmc() {
    ip link add link eth0 name eth0.4088 type vlan id 4088
    ip addr add 240.1.1.2/30 brd 240.1.1.255 dev eth0.4088
    ip link set dev eth0.4088 up
    return 0
}

function install_dhcp_cdc_ether_bmc()
{
    # post request to BMC for insmoding cdc_ether driver and dhcp eth0
    local mfginit_path="/usr/local/bin/mfginit.sh"
    if [ -f ${mfginit_path} ];then
        chmod u+x ${mfginit_path}
        ${mfginit_path}
        [ $? -eq 0 ] && logger "invoke [${mfginit_path}] successful."
    fi
    return 0
}


function start_mterm()
{
    ##enable BMC to CPU serial
    echo 1 > /sys/bus/platform/devices/bbd_ctrl_cpld/bmc_cpu_serial_en

    logger "Starting mTerm console server..."
    mTerm_server bmc /dev/ttyS1 115200 > /dev/null &
    return 0
}

function main()
{
    if echo "cpld-misc 0x0e" > /sys/bus/i2c/devices/i2c-17/new_device; then
        logger "$0 install cpld_misc driver and create virtual device successful."
    else
        logger "$0 install cpld_misc driver and create virtual device failed."
    fi

    ## operate system boot ok wthen write 0x1 into ths sysfs-node;
    echo 0x1 > /sys/bus/platform/devices/bbd_ctrl_cpld/os_boot_sta
    logger "$0 operate system boot ok"

    ## After system plaform boot ok, set sysled green first
    echo 1 > /sys/bus/platform/devices/bbd_ctrl_cpld/sys_led

    # close psu led and fan led write protect
    echo 1 > /sys/bus/platform/devices/bbd_ctrl_cpld/pwr_led_ctrl
    echo 0 > /sys/kernel/pddf/devices/sysstatus/sysstatus_data/fan_led_ctrl_wp
    logger "$0 close psu led and fan led write protect"

	# set all port-led mode into Normal
    echo 1 > /sys/kernel/pddf/devices/sysstatus/sysstatus_data/port_led_ctrl_en_1
    echo 1 > /sys/kernel/pddf/devices/sysstatus/sysstatus_data/port_led_ctrl_en_2
    echo 1 > /sys/kernel/pddf/devices/sysstatus/sysstatus_data/port_led_ctrl_en_3

    setup_eth0_bmc

    return 0
}

main "$@" &
