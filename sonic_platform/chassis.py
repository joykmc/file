# -*- coding: UTF-8 -*-

"""
Module contains an implementation of SONiC Platform Base API and
provides the Chassis information which are available in the platform
"""

try:
    import time
    import os.path
    from sonic_platform_base.chassis_base import ChassisBase
    from sonic_platform.fan_drawer import FanDrawer
    from sonic_platform.psu import Psu
    from sonic_platform.component import Component
    from sonic_platform.sfp import Sfp
    from sonic_platform.eeprom import Eeprom
    from sonic_platform.watchdog import Watchdog
    from sonic_platform.voltage import Voltage
    from sonic_platform.current import Current
    from sonic_platform.thermal import Thermal
    from sonic_platform.extend import VoltageRegulator
    from sonic_platform.extend import Led
    from sonic_platform.extend import Fru
    from sonic_platform.plat_common import PlatCommon
    from sonic_platform.plat_common import CommonCfg
    from vendor_sonic_platform.device import DeviceCfg
    from vendor_sonic_platform import hooks
except ImportError as e:
    raise ImportError(str(e) + "- required module not found") from e



class Chassis(ChassisBase):
    """Platform-specific Chassis class"""

    CPU_ERROR_THERMALTRIP = "thermaltrip"
    CPU_ERROR_CATERR = "caterr"
    CPU_ERROR_ERROR2 = "error2"
    CPU_ERROR_ERROR1 = "error1"
    CPU_ERROR_ERROR0 = "error0"
    CPU_ERROR_NMI = "nmi"
    CPU_ERROR_SMI = "smi"

    # List of SensorBase-derived objects representing all currents
    # available on the chassis
    _current_list = []

    # List of SensorBase-derived objects representing all voltages
    # available on the chassis
    _voltage_list = []

    # List of fru objects representing all frus
    # available on the chassis
    _fru_list = []

    # List of vr objects representing all voltage-regulators
    # available on the chassis
    _vr_list = []

    # List of led objects representing all LEDs
    # available on the chassis
    _led_list = []

    def __init__(self):
        ChassisBase.__init__(self)
        self.plat_common = PlatCommon(debug=CommonCfg.DEBUG)

        try:
            # fan drawer
            for index in range(0, DeviceCfg.CHASSIS_FAN_INFO["num"]):
                fandrawer = FanDrawer(index, DeviceCfg.CHASSIS_FAN_INFO["rotor_num"], DeviceCfg.CHASSIS_FAN_INFO["method"])
                self._fan_drawer_list.append(fandrawer)
                self._fan_list.extend(fandrawer.get_all_fans())

            # psu
            for index in range(0, DeviceCfg.CHASSIS_PSU_INFO["num"]):
                psu = Psu(index, DeviceCfg.CHASSIS_PSU_INFO["fan_num"], DeviceCfg.CHASSIS_PSU_INFO["method"])
                self._psu_list.append(psu)

            # sfp
            for index in range(0, DeviceCfg.SFP_NUM):
                sfp = Sfp(index)
                self._sfp_list.append(sfp)

            # thermal
            for index, thermal_info in enumerate(DeviceCfg.CHASSIS_THERMAL_INFO):
                thermal = Thermal(thermal_info["name"], thermal_info["slot_idx"],
                                  thermal_info["index"], thermal_info["method"])
                self._thermal_list.append(thermal)

            # voltage
            for index, voltage_info in enumerate(DeviceCfg.CHASSIS_VOLTAGE_INFO):
                voltage = Voltage(voltage_info["name"], voltage_info["slot_idx"],
                                  voltage_info["index"], voltage_info["method"])
                self._voltage_list.append(voltage)

            # current
            for index, current_info in enumerate(DeviceCfg.CHASSIS_CURRENT_INFO):
                current = Current(current_info["name"], current_info["slot_idx"],
                                  current_info["index"], current_info["method"])
                self._current_list.append(current)

            # component:
            for comp_type, comp_info in DeviceCfg.CHASSIS_COMPONENT_INFO.items():
                for index, comp_detail in enumerate(comp_info):
                    component = Component(comp_type, index, comp_detail)
                    self._component_list.append(component)

            # vr
            for index, vr_info in enumerate(DeviceCfg.CHASSIS_VR_INFO):
                vr = VoltageRegulator(index, vr_info["name"], vr_info["method"])
                self._vr_list.append(vr)

            # fru
            for index, fru_info in enumerate(DeviceCfg.CHASSIS_FRU_INFO):
                fru = Fru(index, fru_info["name"], fru_info["method"], fru_info["id"])
                self._fru_list.append(fru)

            # led
            for index, led_info in enumerate(DeviceCfg.CHASSIS_LED_INFO):
                led = Led(index, led_info["name"], led_info["method"])
                self._led_list.append(led)

            # syseeprom
            self._eeprom = Eeprom()
            # watchdog
            self._watchdog = Watchdog(DeviceCfg.HAS_WATCHDOG)

            # slot(linecard)
            self.__init_slot_devices()
        except Exception as error:
            self.plat_common.log_error(str(error))

    def __init_slot_devices(self):
        try:
            for index in range(0, DeviceCfg.SLOT_NUM):
                slot_index = index + 1
                slot_path = os.path.join(CommonCfg.S3IP_SLOT_PATH, "slot{}".format(slot_index))

                thermal_num = self.plat_common.read_file(os.path.join(slot_path,
                                                                          "num_temp_sensors"))
                vol_num = self.plat_common.read_file(os.path.join(slot_path,
                                                                      "num_vol_sensors"))
                curr_num = self.plat_common.read_file(os.path.join(slot_path,
                                                                       "num_curr_sensors"))

                if self.plat_common.is_valid_value(thermal_num):
                    for thermal_index in range(1, int(thermal_num) + 1):
                        name_path = os.path.join(slot_path,
                                                 "temp_sensor{}".format(thermal_index),
                                                 "alias")
                        name = self.plat_common.read_file(name_path)
                        thermal = Thermal(name, slot_index, thermal_index)
                        self._thermal_list.append(thermal)

                if self.plat_common.is_valid_value(vol_num):
                    for vol_index in range(1, int(vol_num) + 1):
                        name_path = os.path.join(slot_path,
                                                 "vol_sensor{}".format(vol_index),
                                                 "alias")
                        name = self.plat_common.read_file(name_path)
                        voltage = Voltage(name, slot_index, vol_index)
                        self._voltage_list.append(voltage)

                if self.plat_common.is_valid_value(curr_num):
                    for curr_index in range(1, int(curr_num) + 1):
                        name_path = os.path.join(slot_path,
                                                 "curr_sensor{}".format(curr_index),
                                                 "alias")
                        name = self.plat_common.read_file(name_path)
                        current = Current(name, slot_index, curr_index)
                        self._current_list.append(current)
        except Exception as error:
            self.plat_common.log_error(str(error))

    def get_presence(self):
        """
        Retrieves the presence of the chassis
        Returns:
            bool: True if chassis is present, False if not
        """
        return True

    def get_name(self):
        """
        Retrieves the name of the chassis
        Returns:
            string: The name of the chassis
        """
        return self._eeprom.get_product_name()

    def get_base_mac(self):
        """
        Retrieves the base MAC address for the chassis

        Returns:
            A string containing the MAC address in the format
            'XX:XX:XX:XX:XX:XX'
        """
        return self._eeprom.get_base_mac()

    def get_serial_number(self):
        """
        Retrieves the hardware serial number for the chassis

        Returns:
            A string containing the hardware serial number for this chassis.
        """
        return self._eeprom.get_serial_number()

    def get_serial(self):
        """
        Retrieves the serial number of the chassis
        Returns:
            string: Serial number of chassis
        """
        return self._eeprom.get_serial_number()

    def get_model(self):
        """
        Retrieves the model number (or part number) of the chassis
        Returns:
            string: Model/part number of chassis
        """
        return self._eeprom.get_part_number()

    def get_revision(self):
        """
        Retrieves the hardware revision of the device

        Returns:
            string: Revision value of device
        """
        return self._eeprom.get_device_version()

    def get_system_eeprom_info(self):
        """
        Retrieves the full content of system EEPROM information for the chassis

        Returns:
            A dictionary where keys are the type code defined in
            OCP ONIE TlvInfo EEPROM format and values are their corresponding
            values.
            Ex. { '0x21':'AG9064', '0x22':'V1.0', '0x23':'AG9064-0109867821',
                  '0x24':'001c0f000fcd0a', '0x25':'02/03/2018 16:22:00',
                  '0x26':'01', '0x27':'REV01', '0x28':'AG9064-C2358-16G'}
        """
        return self._eeprom.get_system_eeprom_info()

    def get_reboot_cause(self):
        """
        Retrieves the cause of the previous reboot

        Returns:
            A tuple (string, string) where the first element is a string
            containing the cause of the previous reboot. This string must be
            one of the predefined strings in this class. If the first string
            is "REBOOT_CAUSE_HARDWARE_OTHER", the second string can be used
            to pass a description of the reboot cause.
        """
        if hasattr(hooks, "get_reboot_cause"):
            return hooks.get_reboot_cause()

        self.plat_common.log_error("get reboot cause is not implemented!")
        return None, ""

    def get_supervisor_slot(self):
        """
        Retrieves the physical-slot of the supervisor-module in the modular
        chassis. On the supervisor or line-card modules, it will return the
        physical-slot of the supervisor-module.

        On the fixed-platforms, the API can be ignored.

        Users of the API can catch the exception and return a default
        ModuleBase.MODULE_INVALID_SLOT and bypass code for fixed-platforms.

        Returns:
            An integer, the vendor specific physical slot identifier of the
            supervisor module in the modular-chassis.
        """
        return NotImplementedError

    def get_my_slot(self):
        """
        Retrieves the physical-slot of this module in the modular chassis.
        On the supervisor, it will return the physical-slot of the supervisor
        module. On the linecard, it will return the physical-slot of the
        linecard module where this instance of SONiC is running.

        On the fixed-platforms, the API can be ignored.

        Users of the API can catch the exception and return a default
        ModuleBase.MODULE_INVALID_SLOT and bypass code for fixed-platforms.

        Returns:
            An integer, the vendor specific physical slot identifier of this
            module in the modular-chassis.
        """
        return NotImplementedError

    def is_modular_chassis(self):
        """
        Retrieves whether the sonic instance is part of modular chassis

        Returns:
            A bool value, should return False by default or for fixed-platforms.
            Should return True for supervisor-cards, line-cards etc running as part
            of modular-chassis.
        """
        return DeviceCfg.IS_MODULAR_CHASSIS

    def init_midplane_switch(self):
        """
        Initializes the midplane functionality of the modular chassis. For
        example, any validation of midplane, populating any lookup tables etc
        can be done here. The expectation is that the required kernel modules,
        ip-address assignment etc are done before the pmon, database dockers
        are up.

        Returns:
            A bool value, should return True if the midplane initialized
            successfully.
        """
        return NotImplementedError

    def get_status(self):
        """
        Retrieves the operational status of the chassis
        Returns:
            bool: A boolean value, True if chassis is operating properly
            False if not
        """
        # default
        return True

    def get_position_in_parent(self):
        """
        Retrieves 1-based relative physical position in parent device. If the agent cannot determine the parent-relative position
        for some reason, or if the associated value of entPhysicalContainedIn is '0', then the value '-1' is returned
        Returns:
            integer: The 1-based relative physical position in parent device or -1 if cannot determine the position
        """
        return -1

    def is_replaceable(self):
        """
        Indicate whether this device is replaceable.
        Returns:
            bool: True if it is replaceable.
        """
        return False


    ##############################################
    # New component methods
    ##############################################
    def get_bios_boot_flash(self):
        """
        Retrieves the bios current booting flash.

        Returns:
            String, "master" or "slave"
        """
        if hasattr(hooks, "get_bios_boot_flash"):
            return hooks.get_bios_boot_flash()

        bios_boot_flash_path = os.path.join(CommonCfg.S3IP_EXTEND_PATH, "fw_ctl", "bios_boot_flash_status")
        boot_flash = self.plat_common.read_file(bios_boot_flash_path)
        if not self.plat_common.is_valid_value(boot_flash):
            return CommonCfg.NULL_VALUE
        try:
            if int(boot_flash, 0) == 1:
                return "master"
            if int(boot_flash, 0) == 0:
                return "slave"
        except Exception as err:
            self.plat_common.log_error(str(err))
        return CommonCfg.NULL_VALUE

    ##############################################
    # No Module methods overridden
    ##############################################
    def get_module_index(self, module_name):
        """
        Retrieves module index from the module name

        Args:
            module_name: A string, prefixed by SUPERVISOR, LINE-CARD or FABRIC-CARD
            Ex. SUPERVISOR0, LINE-CARD1, FABRIC-CARD5

        Returns:
            An integer, the index of the ModuleBase object in the module_list
        """
        index = -1

        for index, module in enumerate(self._module_list):
            if module.get_name() == module_name:
                return index

        self.plat_common.log_warning("{} not found!".format(module_name))
        return index


    ##############################################
    # New Fan methods
    ##############################################
    def get_num_fan_motors(self):
        """
        Retrieves the number of rotor per fan tray.

        Returns:
            An integer, the number of rotor per fantray on this chassis
        """
        fan_drawer = self.get_fan_drawer(0)
        if fan_drawer:
            return fan_drawer.get_num_fans()
        return 0

    ##############################################
    # No PSU methods overridden
    ##############################################

    ##############################################
    # THERMAL methods
    ##############################################
    def get_thermal_manager(self):
        """
        Retrieves thermal manager class on this chassis
        :return: A class derived from ThermalManagerBase representing the
        specified thermal manager. ThermalManagerBase is returned as default
        """
        return None

    ##############################################
    # New Current methods
    ##############################################
    def get_num_currents(self):
        """
        Retrieves the number of currents available on this chassis

        Returns:
            An integer, the number of currents available on this chassis
        """
        return len(self._current_list)

    def get_all_currents(self):
        """
        Retrieves all currents available on this chassis

        Returns:
            A list of objects derived from SensorBase representing all currents
            available on this chassis
        """
        return self._current_list

    def get_current(self, index):
        """
        Retrieves current represented by (0-based) index <index>

        Args:
            index: An integer, the index (0-based) of the current to

        Returns:
            An object dervied from SensorBase representing the specified current
        """
        current = None

        try:
            current = self._current_list[index]
        except IndexError:
            self.plat_common.log_error("Current index {} out of range (0-{})\n".format(
                             index, len(self._current_list)-1))

        return current

    ##############################################
    # New Voltage methods
    ##############################################
    def get_num_voltages(self):
        """
        Retrieves the number of voltages available on this chassis

        Returns:
            An integer, the number of voltages available on this chassis
        """
        return len(self._voltage_list)

    def get_all_voltages(self):
        """
        Retrieves all voltages available on this chassis

        Returns:
            A list of objects derived from SensorBase representing all voltages
            available on this chassis
        """
        return self._voltage_list

    def get_voltage(self, index):
        """
        Retrieves voltage represented by (0-based) index <index>

        Args:
            index: An integer, the index (0-based) of the voltage to

        Returns:
            An object dervied from SensorBase representing the specified voltage
        """
        voltage = None

        try:
            voltage = self._voltage_list[index]
        except IndexError:
            self.plat_common.log_error("Voltage index {} out of range (0-{})\n".format(
                             index, len(self._voltage_list)-1))

        return voltage

    ##############################################
    # New VR methods
    ##############################################
    def get_num_voltage_regulators(self):
        """
        Retrieves the number of voltage regulators available on this chassis

        Returns:
            An integer, the number of voltage regulators available on this chassis
        """
        return len(self._vr_list)

    def get_all_voltage_regulators(self):
        """
        Retrieves all voltage_regulators available on this chassis

        Returns:
            A list of objects representing all voltage_regulators
            available on this chassis
        """
        return self._vr_list

    def get_voltage_regulator(self, index):
        """
        Retrieves voltage_regulator represented by (0-based) index <index>

        Args:
            index: An integer, the index (0-based) of the voltage_regulator to

        Returns:
            An object representing the specified voltage_regulator
        """
        vr = None

        try:
            vr = self._vr_list[index]
        except IndexError:
            self.plat_common.log_error("vr index {} out of range (0-{})\n".format(
                             index, len(self._vr_list)-1))

        return vr

    ##############################################
    # New fru methods
    ##############################################
    def get_num_frus(self):
        """
        Retrieves the number of frus available on this chassis

        Returns:
            An integer, the number of frus available on this chassis
        """
        return len(self._fru_list)

    def get_all_frus(self):
        """
        Retrieves all frus available on this chassis

        Returns:
            A list of objects  representing all frus
            available on this chassis
        """
        return self._fru_list

    def get_fru(self, index):
        """
        Retrieves fru represented by (0-based) index <index>

        Args:
            index: An integer, the index (0-based) of the fru to

        Returns:
            An object representing the specified fru
        """
        fru = None

        try:
            fru = self._fru_list[index]
        except IndexError:
            self.plat_common.log_error("fru index {} out of range (0-{})\n".format(
                             index, len(self._fru_list)-1))

        return fru

    ##############################################
    # New Led methods
    ##############################################
    def get_num_leds(self):
        """
        Retrieves the number of led available on this chassis

        Returns:
            An integer, the number of led available on this chassis
        """
        return len(self._led_list)

    def get_all_leds(self):
        """
        Retrieves all leds available on this chassis

        Returns:
            A list of objects  representing all leds
            available on this chassis
        """
        return self._led_list

    def get_led(self, index):
        """
        Retrieves led represented by (0-based) index <index>

        Args:
            index: An integer, the index (0-based) of the led

        Returns:
            An object representing the specified led
        """
        led = None

        try:
            led = self._led_list[index]
        except IndexError:
            self.plat_common.log_error("led index {} out of range (0-{})\n".format(
                             index, len(self._led_list)-1))

        return led

    ##############################################
    # No SFP methods overridden
    ##############################################
    def get_port_or_cage_type(self, index):
        """
        Retrieves sfp port or cage type corresponding to physical port <index>

        Args:
            index: An integer (>=0), the index of the sfp to retrieve.
                   The index should correspond to the physical port in a chassis.
                   For example:-
                   1 for Ethernet0, 2 for Ethernet4 and so on for one platform.
                   0 for Ethernet0, 1 for Ethernet4 and so on for another platform.

        Returns:
            The masks of all types of port or cage that can be supported on the port
            Types are defined in sfp_base.py
            Eg.
                Both SFP and SFP+ are supported on the port, the return value should be 0x0a
                which is 0x02 | 0x08
        """
        return self.get_sfp(index).get_port_or_cage_type()

    ##############################################
    # System LED methods
    ##############################################
    def initizalize_system_led(self):
        """
        initizalize system status led, used for show system-health
        Returns:
            bool: True if system status LED state is initizalized successfully, False if not
        """
        return True

    def set_status_led(self, color):
        """
        Sets the state of the system LED

        Args:
            color: A string representing the color with which to set the
                   system LED

        Returns:
            bool: True if system LED state is set successfully, False if not
        """
        # system_health manager will set system led; 
        # to avoid conflict with ledcontrol, just do nothing
        self.plat_common.log_debug("Set system led to {}".format(color))
        return False

    def get_status_led(self):
        """
        Gets the state of the system LED

        Returns:
            A string, one of the valid LED color strings which could be vendor
            specified.
        """
        return self.get_sys_led()

    ##############################################
    # Other LED methods, include SYS/PSU/FAN
    ##############################################
    def set_sys_led(self, color):
        """
        Sets the state of the system LED, for LED control

        Args:
            color: A string representing the color with which to set the
                   system LED

        Returns:
            bool: True if system LED state is set successfully, False if not
        """
        if hasattr(hooks, "set_sys_led"):
            return hooks.set_sys_led(color)

        try:
            value = CommonCfg.COLOR_MAP[color]
            led_status_path = os.path.join(CommonCfg.S3IP_LED_PATH, "sys_led_status")
            # led_ctrl_en_path = os.path.join(CommonCfg.S3IP_LED_PATH, "sys_led_ctrl_en")
            # if not self.plat_common.write_file(led_ctrl_en_path, 1):
            #     return False
            return self.plat_common.write_file(led_status_path, value)
        except Exception as error:
            self.plat_common.log_error("Set sys led status fail:{}".format(str(error)))
        return False

    def get_sys_led(self):
        """
        Gets the state of the system LED

        Returns:
            A string, one of the valid LED color strings which could be vendor
            specified.
        """
        if hasattr(hooks, "get_sys_led"):
            return hooks.get_sys_led()

        try:
            led_path = os.path.join(CommonCfg.S3IP_LED_PATH, "sys_led_status")
            if os.path.isfile(led_path):
                result = self.plat_common.read_file(led_path)
                if not self.plat_common.is_valid_value(result):
                    return CommonCfg.NULL_VALUE
                for color, value in CommonCfg.COLOR_MAP.items():
                    if value == int(result):
                        self.plat_common.log_info("Got sys led status={}".format(value))
                        return color
            else:
                ret, out = self.plat_common.exec_restful_rawcmd("led-util --get_info | grep system_led")
                if ret and out:
                    return out.split(":")[1].strip()
        except Exception as error:
            self.plat_common.log_error("Get sys led status fail:{}".format(str(error)))
        return CommonCfg.NULL_VALUE

    def set_psu_led(self, color):
        """
        Sets the state of the panel psu LED

        Args:
            color: A string representing the color with which to set the psu LED
        Returns:
            bool: True if psu LED state is set successfully, False if not
        """
        if hasattr(hooks, "set_psu_led"):
            return hooks.set_psu_led(color)

        try:
            value = CommonCfg.COLOR_MAP[color]
            led_status_path = os.path.join(CommonCfg.S3IP_LED_PATH, "psu_led_status")
            led_ctrl_en_path = os.path.join(CommonCfg.S3IP_LED_PATH, "psu_led_ctrl_en")
            if not self.plat_common.write_file(led_ctrl_en_path, 1):
                return False
            return self.plat_common.write_file(led_status_path, value)
        except Exception as error:
            self.plat_common.log_error("Set psu led status fail:{}".format(str(error)))
        return False

    def get_psu_led(self):
        """
        Gets the state of the psu LED

        Returns:
            A string, one of the valid LED color strings which could be vendor
            specified.
        """
        if hasattr(hooks, "get_psu_led"):
            return hooks.get_psu_led()

        try:
            led_path = os.path.join(CommonCfg.S3IP_LED_PATH, "psu_led_status")
            if os.path.isfile(led_path):
                result = self.plat_common.read_file(led_path)
                if not self.plat_common.is_valid_value(result):
                    return CommonCfg.NULL_VALUE
                for color, value in CommonCfg.COLOR_MAP.items():
                    if value == int(result):
                        self.plat_common.log_info("Got psu led status={}".format(value))
                        return color
            else:
                ret, out = self.plat_common.exec_restful_rawcmd("led-util --get_info | grep system_power_led")
                if ret and out:
                    return out.split(":")[1].strip()
        except Exception as error:
            self.plat_common.log_error("Get psu led status fail:{}".format(str(error)))
        return CommonCfg.NULL_VALUE

    def set_fan_led(self, color):
        """
        Sets the state of the panel fan LED

        Args:
            color: A string representing the color with which to set the fan LED
        Returns:
            bool: True if fan LED state is set successfully, False if not
        """
        if hasattr(hooks, "set_fan_led"):
            return hooks.set_fan_led(color)

        try:
            value = CommonCfg.COLOR_MAP[color]
            led_status_path = os.path.join(CommonCfg.S3IP_LED_PATH, "fan_led_status")
            led_ctrl_en_path = os.path.join(CommonCfg.S3IP_LED_PATH, "fan_led_ctrl_en")
            if not self.plat_common.write_file(led_ctrl_en_path, 1):
                return False
            return self.plat_common.write_file(led_status_path, value)
        except Exception as error:
            self.plat_common.log_error("Set fan led status fail:{}".format(str(error)))
        return False

    def get_fan_led(self):
        """
        Gets the state of the fan LED

        Returns:
            A string, one of the valid LED color strings which could be vendor
            specified.
        """
        if hasattr(hooks, "get_fan_led"):
            return hooks.get_fan_led()

        try:
            led_path = os.path.join(CommonCfg.S3IP_LED_PATH, "fan_led_status")
            if os.path.isfile(led_path):
                result = self.plat_common.read_file(led_path)
                if not self.plat_common.is_valid_value(result):
                    return CommonCfg.NULL_VALUE
                for color, value in CommonCfg.COLOR_MAP.items():
                    if value == int(result):
                        self.plat_common.log_info("Got fan led status={}".format(value))
                        return color
            else:
                ret, out = self.plat_common.exec_restful_rawcmd("led-util --get_info | grep system_fan_led")
                if ret and out:
                    return out.split(":")[1].strip()
        except Exception as error:
            self.plat_common.log_error("Get fan led status fail:{}".format(str(error)))
        return CommonCfg.NULL_VALUE

    ##############################################
    # Other methods
    ##############################################
    def get_change_event(self, timeout=0, scantime=0.5, dev_list=None):
        """
        Returns a nested dictionary containing all devices which have
        experienced a change at chassis level

        Args:
            timeout: Timeout in milliseconds (optional). If timeout == 0,
                this method will block until a change is detected.
            scantime: Scan device change event interval, default is 0.5s

        Returns:
            (bool, dict):
                - bool: True if call successful, False if not;
                - dict: A nested dictionary where key is a device type,
                        value is a dictionary with key:value pairs in the format of
                        {'device_id':'device_event'}, where device_id is the device ID
                        for this device and device_event.
                        The known devices's device_id and device_event was defined as table below.
                         -----------------------------------------------------------------
                         device   |     device_id       |  device_event  |  annotate
                         -----------------------------------------------------------------
                         'fan'          '<fan number>'     '0'              Fan removed
                                                           '1'              Fan inserted
                                                           '2'              Fan OK
                                                           '3'              Fan speed low
                                                           '4'              Fan speed high

                         'psu'          '<psu number>'     '0'              Psu removed
                                                           '1'              Psu inserted
                                                           '2'              Psu ok
                                                           '3'              Psu power loss
                                                           '4'              Psu fan error
                                                           '5'              Psu voltage error
                                                           '6'              Psu current error
                                                           '7'              Psu power error
                                                           '8'              Psu temperature error

                         'sfp'          '<sfp number>'     '0'              Sfp removed
                                                           '1'              Sfp inserted
                                                           '2'              I2C bus stuck
                                                           '3'              Bad eeprom
                                                           '4'              Unsupported cable
                                                           '5'              High Temperature
                                                           '6'              Bad cable
                                                           '7'              Sfp ok

                         'thermal'      '<thermal name>'   '0'              Thermal normal
                                                           '1'              Thermal Abnormal

                         'voltage'     '<monitor point>'   '0'              Vout normal
                                                           '1'              Vout abnormal
                         -----------------------------------------------------------------
        """
        if dev_list is None:
            dev_list = ["sfp"]

        start_ms = time.time() * 1000
        change_event_dict = {}
        dev_change_event_dict = {}
        ret = False

        while True:
            time.sleep(scantime)

            for dev in dev_list:
                change_event_dict.update({dev: {}})
                self.plat_common.log_info('get {} event...'.format(dev))
                d_list = getattr(self, 'get_all_{}s'.format(dev))()
                for d in d_list:
                    (ret, dev_change_event_dict) = d.get_change_event()
                    if ret:
                        change_event_dict[dev].update(dev_change_event_dict[dev])

            for events in change_event_dict.values():
                if len(events):
                    return True, change_event_dict

            if timeout:
                now_ms = time.time() * 1000
                if (now_ms - start_ms) >= timeout:
                    for events in change_event_dict.values():
                        if len(events):
                            return True, change_event_dict
                        else:
                            # return False, change_event_dict
                            # temporary modification fro xcvrd
                            return True, change_event_dict

    def get_polling_interval_factor(self, daemon):
        try:
            factor = DeviceCfg.POLLING_INTERVAL_FACTOR[daemon]
            return factor
        except Exception as error:
            self.plat_common.log_error(str(error))
            return 1

    def get_cpu_warning_state(self):
        """
        Retrive the state of the cpu warning

        Returns:
            list: cpu warning description, eg."caterr", "thermaltrip"
        """
        if hasattr(hooks, "get_cpu_warning_state"):
            return hooks.get_cpu_warning_state()

        thermaltrip_path = os.path.join(CommonCfg.S3IP_EXTEND_PATH, "system/cpu_thermaltrip_out")
        caterr_path = os.path.join(CommonCfg.S3IP_EXTEND_PATH, "system/cpu_caterr_3V3")
        error2_path = os.path.join(CommonCfg.S3IP_EXTEND_PATH, "system/cpu_error2")
        error1_path = os.path.join(CommonCfg.S3IP_EXTEND_PATH, "system/cpu_error1")
        error0_path = os.path.join(CommonCfg.S3IP_EXTEND_PATH, "system/cpu_error0")
        nmi_path = os.path.join(CommonCfg.S3IP_EXTEND_PATH, "system/cpu_nmi")
        smi_path = os.path.join(CommonCfg.S3IP_EXTEND_PATH, "system/cpu_smi")
        cpu_warning_signals = []

        try:
            thermal_trip = self.plat_common.read_file(thermaltrip_path)
            if self.plat_common.is_valid_value(thermal_trip) and int(thermal_trip) == 0:
                self.plat_common.log_warning("CPU thermaltrip occured!")
                cpu_warning_signals.append(self.CPU_ERROR_THERMALTRIP)

            caterr = self.plat_common.read_file(caterr_path)
            if self.plat_common.is_valid_value(caterr) and int(caterr) == 0:
                self.plat_common.log_warning("CPU caterr occured!")
                cpu_warning_signals.append(self.CPU_ERROR_CATERR)

            error2 = self.plat_common.read_file(error2_path)
            if self.plat_common.is_valid_value(error2) and int(error2) == 0:
                self.plat_common.log_warning("CPU error2 occured!")
                cpu_warning_signals.append(self.CPU_ERROR_ERROR2)

            error1 = self.plat_common.read_file(error1_path)
            if self.plat_common.is_valid_value(error1) and int(error1) == 0:
                self.plat_common.log_warning("CPU error1 occured!")
                cpu_warning_signals.append(self.CPU_ERROR_ERROR1)

            error0 = self.plat_common.read_file(error0_path)
            if self.plat_common.is_valid_value(error0) and int(error0) == 0:
                self.plat_common.log_warning("CPU error0 occured!")
                cpu_warning_signals.append(self.CPU_ERROR_ERROR0)

            smi = self.plat_common.read_file(smi_path)
            if self.plat_common.is_valid_value(smi) and int(smi) == 0:
                self.plat_common.log_warning("CPU smi occured!")
                cpu_warning_signals.append(self.CPU_ERROR_SMI)

            nmi = self.plat_common.read_file(nmi_path)
            if self.plat_common.is_valid_value(nmi) and int(nmi) == 0:
                self.plat_common.log_warning("CPU nmi occured!")
                cpu_warning_signals.append(self.CPU_ERROR_NMI)
        except Exception as err:
            self.plat_common.log_notice("Get cpu warning failed:{}".format(str(err)))

        return cpu_warning_signals

    def get_fan_watchdog_status(self):
        """
        Getting the state of the fan watchdog

        Returns:
            bool: True if fan WDT state is Normal, False is abnormal
        """
        if hasattr(hooks, "get_fan_watchdog_status"):
            return hooks.get_fan_watchdog_status()

        try:
            fan_wtd_path = os.path.join(CommonCfg.S3IP_FAN_PATH, "watchdog_status")
            result = self.plat_common.read_file(fan_wtd_path)
            if self.plat_common.is_valid_value(result):
                return int(result) == 0
        except Exception as error:
            self.plat_common.log_error("get fan watchdog status fail:{}".format(str(error)))
        return False

    def set_fan_watchdog(self, enable, timeout=10):
        """
        Set the fan watchdog enable or disable

        Args:
            enable: bool, True or False
            timeout: int, Default is 10, max is 63, unit 5s;

        Returns:
            bool: True if fan watchdog state is set successfully, False if not
        """
        if hasattr(hooks, "set_fan_watchdog"):
            return hooks.set_fan_watchdog(enable, timeout)

        try:
            fan_wtd_path = os.path.join(CommonCfg.S3IP_FAN_PATH, "watchdog_en")
            fan_wtd_time_path = os.path.join(CommonCfg.S3IP_FAN_PATH, "watchdog_time_config")
            self.plat_common.write_file(fan_wtd_time_path, timeout)
            value = 1 if enable else 0
            return self.plat_common.write_file(fan_wtd_path, value)
        except Exception as error:
            self.plat_common.log_error("set fan watchdog fail:{}".format(str(error)))
        return False

    def set_fan_control_mode(self, mode):
        """
        Sets fan control mode

        Args:
            mode:A string representing the control mode, "manual" or "auto"

        Returns:
            bool: True if mode is set successfully
        """
        if mode not in ["auto", "manual"]:
            return False
        if hasattr(hooks, "set_fan_control_mode"):
            return hooks.set_fan_control_mode(mode)

        if os.path.exists(CommonCfg.S3IP_FAN_PATH):
            if mode == "auto":
                ret = self.plat_common.exec_system_cmd("systemctl start fanctrl.service")
            else:
                ret = self.plat_common.exec_system_cmd("systemctl stop fanctrl.service")
        else:
            if DeviceCfg.NEW_BMC_RESTFUL:
                ret = self.plat_common.set_fan_control_mode_by_restful(mode)
            else:
                ret = False
        return ret

    def set_led_control_mode(self, mode):
        """
        Sets led control mode

        Args:
            mode:A string representing the control mode, "manual" or "auto"

        Returns:
            bool: True if mode is set successfully
        """
        if mode not in ["auto", "manual"]:
            return False
        if hasattr(hooks, "set_led_control_mode"):
            return hooks.set_led_control_mode(mode)

        if os.path.exists(CommonCfg.S3IP_LED_PATH):
            if mode == "auto":
                ret = self.plat_common.exec_system_cmd("systemctl start ledctrl.service")
            else:
                ret = self.plat_common.exec_system_cmd("systemctl stop ledctrl.service")
        else:
            return False

        return ret

    ##### chassis reboot operations ####
    def global_reset(self):
        """
        Global reset by control cpu global reset pin

        Returns:
            bool: True if reset successfully
        """
        if hasattr(hooks, "global_reset"):
            return hooks.global_reset()

        try:
            if hasattr(hooks, "power_control_enable"):
                hooks.power_control_enable()
            reset_path = os.path.join(CommonCfg.S3IP_EXTEND_PATH, "system/global_reset")
            return self.plat_common.write_file(reset_path, 1)
        except Exception as error:
            self.plat_common.log_error("global reset fail:{}".format(str(error)))
        return False

    def cold_reboot(self):
        """
        CPU power cycle (POWER_ON/OFF)

        Returns:
            bool: True if cycle successfully
        """
        if hasattr(hooks, "cold_reboot"):
            return hooks.cold_reboot()

        try:
            if hasattr(hooks, "power_control_enable"):
                hooks.power_control_enable()
            reset_path = os.path.join(CommonCfg.S3IP_EXTEND_PATH, "system/cpu_pwr_cycle_ctrl")
            return self.plat_common.write_file(reset_path, 0)
        except Exception as error:
            self.plat_common.log_error("cold reboot fail:{}".format(str(error)))
        return False

    def warm_reboot(self):
        """
        CPU warm reset (CPU_RST_BTN)

        Returns:
            bool: True if cycle successfully
        """
        if hasattr(hooks, "warm_reboot"):
            return hooks.warm_reboot()

        try:
            if hasattr(hooks, "power_control_enable"):
                hooks.power_control_enable()
            reset_path = os.path.join(CommonCfg.S3IP_EXTEND_PATH, "system/cpu_reset_ctrl")
            return self.plat_common.write_file(reset_path, 0)
        except Exception as error:
            self.plat_common.log_error("warm reboot fail:{}".format(str(error)))
        return False

    def power_cycle(self):
        """
        Control psu to power cycle

        Returns:
            bool: True if cycle successfully
        """
        if hasattr(hooks, "psu_power_cycle"):
            return hooks.psu_power_cycle()

        ret = True
        try:
            for psu_obj in self.get_all_psus():
                ret &= psu_obj.power_cycle()
        except Exception as error:
            self.plat_common.log_error("cold reboot fail:{}".format(str(error)))
            return False

        return ret
