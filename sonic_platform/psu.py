# -*- coding: UTF-8 -*-

"""
Module contains an implementation of SONiC Platform Base API and
provides the psu information which are available in the platform
"""

try:
    import os.path
    from sonic_platform_base.psu_base import PsuBase
    from sonic_platform.fan import Fan
    from sonic_platform.plat_common import PlatCommon
    from sonic_platform.plat_common import CommonCfg
except ImportError as e:
    raise ImportError(str(e) + "- required module not found") from e


AC_INPUT = "AC"
DC_INPUT = "DC"


class Psu(PsuBase):
    """Platform-specific Psu class"""

    def __init__(self, index, fan_num=1, method="sysfs"):
        """
        Psu initial

        Args:
            index: int, start from 0
            fan_num: int, the fan count of psu
            method: str, eg. 'sysfs', 'restful', 'cache'
        """
        PsuBase.__init__(self)
        self.index = index + 1    # psu index start from 1
        self.plat_common = PlatCommon(debug=CommonCfg.DEBUG)
        self.sysfs_path = None
        self.method = method
        self._old_presence = False
        if fan_num > 0:
            for fan_index in range(0, fan_num):
                self._fan_list.append(Fan(fan_index, self, True, method))
        if self.method == CommonCfg.BY_SYSFS:
            self.sysfs_path = os.path.join(CommonCfg.S3IP_PSU_PATH, "psu{}".format(self.index))

    def __get_file_path(self, file_name):
        if self.sysfs_path is not None:
            return os.path.join(self.sysfs_path, file_name)
        return None

    def get_sysfs_path(self):
        """
        Get PSU sysfs path
        Returns:
            string
        """
        return self.sysfs_path

    def get_index(self):
        """
        Get psu index
        Returns:
            int
        """
        return self.index

    def get_name(self):
        """
        Get PSU name
        Returns:
            string
        """
        return "PSU{}".format(self.index)

    def get_presence(self):
        """
        Retrieves the presence of the PSU

        Returns:
            bool: True if PSU is present, False if not
        """
        if self.method == CommonCfg.BY_RESTFUL:
            return self.plat_common.get_psu_presence_by_restful(self.index)
        if self.method == CommonCfg.BY_CACHE:
            return self.plat_common.get_psu_presence_by_cache(self.index)

        value = self.plat_common.read_file(self.__get_file_path("present"))
        if value == "1":
            return True

        return False

    ###  Manufacture information ###
    def get_model(self):
        """
        Retrieves the model number (or part number) of the device

        Returns:
            string: Model/part number of device
        """
        if not self.get_presence():
            return CommonCfg.NULL_VALUE

        if self.method == CommonCfg.BY_RESTFUL:
            mfr_info = self.plat_common.get_psu_mfr_by_restful(self.index)
            if mfr_info:
                return mfr_info.get("PN")

        if self.method == CommonCfg.BY_CACHE:
            psu_info = self.plat_common.get_psu_info_by_cache(self.index)
            if psu_info:
                return psu_info.get("PN")

        if self.method == CommonCfg.BY_SYSFS:
            model = self.plat_common.read_file(self.__get_file_path("part_number"))
            if self.plat_common.is_valid_value(model):
                return model
        return CommonCfg.NULL_VALUE

    def get_serial(self):
        """
        Get the serial number of current psu
        Returns:
            string: serial number
        """
        if not self.get_presence():
            return CommonCfg.NULL_VALUE

        if self.method == CommonCfg.BY_RESTFUL:
            mfr_info = self.plat_common.get_psu_mfr_by_restful(self.index)
            if mfr_info:
                return mfr_info.get("SN")

        if self.method == CommonCfg.BY_CACHE:
            psu_info = self.plat_common.get_psu_info_by_cache(self.index)
            if psu_info:
                return psu_info.get("SN")

        if self.method == CommonCfg.BY_SYSFS:
            serial = self.plat_common.read_file(self.__get_file_path("serial_number"))
            if self.plat_common.is_valid_value(serial):
                return serial
        return CommonCfg.NULL_VALUE

    def get_vendor(self):
        """
        Retrieves vendor of psu

        Returns:
            A string, psu vendor
        """
        if not self.get_presence():
            return CommonCfg.NULL_VALUE

        if self.method == CommonCfg.BY_RESTFUL:
            mfr_info = self.plat_common.get_psu_mfr_by_restful(self.index)
            if mfr_info:
                return mfr_info.get("Vender")

        if self.method == CommonCfg.BY_CACHE:
            psu_info = self.plat_common.get_psu_info_by_cache(self.index)
            if psu_info:
                return psu_info.get("Vender")

        if self.method == CommonCfg.BY_SYSFS:
            vendor = self.plat_common.read_file(self.__get_file_path("vendor"))
            if self.plat_common.is_valid_value(vendor):
                return vendor
        return CommonCfg.NULL_VALUE

    def get_hardware_version(self):
        """
        Retrieves hardware version of psu

        Returns:
            A string, psu hardware version
        """
        if not self.get_presence():
            return CommonCfg.NULL_VALUE

        if self.method == CommonCfg.BY_RESTFUL:
            mfr_info = self.plat_common.get_psu_mfr_by_restful(self.index)
            if mfr_info:
                return mfr_info.get("HW_Version")

        if self.method == CommonCfg.BY_CACHE:
            psu_info = self.plat_common.get_psu_info_by_cache(self.index)
            if psu_info:
                return psu_info.get("HW_Version")

        if self.method == CommonCfg.BY_SYSFS:
            hw_version = self.plat_common.read_file(self.__get_file_path("hardware_version"))
            if self.plat_common.is_valid_value(hw_version):
                return hw_version

        return CommonCfg.NULL_VALUE

    def get_firmware_version(self):
        """
        Retrieves PSU firmware version

        Returns:
            String, e.g. '1.0'
        """
        if not self.get_presence():
            return CommonCfg.NULL_VALUE

        if self.method == CommonCfg.BY_RESTFUL:
            mfr_info = self.plat_common.get_psu_mfr_by_restful(self.index)
            if mfr_info:
                return mfr_info.get("FW_Version")

        if self.method == CommonCfg.BY_CACHE:
            psu_info = self.plat_common.get_psu_info_by_cache(self.index)
            if psu_info:
                return psu_info.get("FW_Version")

        if self.method == CommonCfg.BY_SYSFS:
            fw_version = self.plat_common.read_file(self.__get_file_path("firmware_version"))
            if self.plat_common.is_valid_value(fw_version):
                return fw_version

        return CommonCfg.NULL_VALUE

    def get_revision(self):
        """
        Retrieves the hardware revision of the device

        Returns:
            string: Revision value of device
        """
        return self.get_hardware_version()

    ###  Device status ###
    def get_status(self):
        """
        Get the status of the psu
        Returns:
            bolean:True if the psu is ok, False if not
        """
        if not self.get_presence():
            return False

        return self.get_input_status() and self.get_powergood_status()

    def get_position_in_parent(self):
        """
        Retrieves 1-based relative physical position in parent device. If the agent cannot determine the parent-relative position
        for some reason, or if the associated value of entPhysicalContainedIn is '0', then the value '-1' is returned
        Returns:
            integer: The 1-based relative physical position in parent device or -1 if cannot determine the position
        """
        return self.index

    def is_replaceable(self):
        """
        Indicate whether this device is replaceable.
        Returns:
            bool: True if it is replaceable.
        """
        return True

    def get_voltage(self):
        """
        Retrieves current PSU voltage output

        Returns:
            A float number, the output voltage in volts,
            e.g. 12.1
        """
        if not self.get_presence():
            return None

        if self.method in [CommonCfg.BY_RESTFUL, CommonCfg.BY_CACHE]:
            if self.method == CommonCfg.BY_RESTFUL:
                power_info = self.plat_common.get_psu_power_status_by_restful(self.index)
            else:
                power_info = self.plat_common.get_psu_info_by_cache(self.index)
            try:
                if power_info is not None and \
                    "Outputs" in power_info.keys() and \
                    "Voltage" in power_info.get("Outputs").keys() and \
                    self.plat_common.is_float(power_info.get("Outputs").get("Voltage").get("Value")) and \
                    self.plat_common.is_valid_value(power_info.get("Outputs").get("Voltage").get("Value")):
                    return round(float(power_info.get("Outputs").get("Voltage").get("Value")), 3)
            except Exception as error:
                self.plat_common.log_error("Get psu out vol error:{}".format(str(error)))
        else:
            try:
                value = self.plat_common.read_file(self.__get_file_path("out_vol"))
                if self.plat_common.is_valid_value(value):
                    return round(float(value) / CommonCfg.VOLTAGE_FACTOR, 3)
            except Exception as error:
                self.plat_common.log_error("Get psu out voltage error:{}".format(str(error)))
        return None

    def get_current(self):
        """
        Retrieves present electric current supplied by PSU

        Returns:
            A float number, the electric current in amperes, e.g 15.4
        """
        if not self.get_presence():
            return None

        if self.method in [CommonCfg.BY_RESTFUL, CommonCfg.BY_CACHE]:
            if self.method == CommonCfg.BY_RESTFUL:
                power_info = self.plat_common.get_psu_power_status_by_restful(self.index)
            else:
                power_info = self.plat_common.get_psu_info_by_cache(self.index)
            try:
                if power_info is not None and \
                    "Outputs" in power_info.keys() and\
                    "Current" in power_info.get("Outputs").keys() and \
                    self.plat_common.is_float(power_info.get("Outputs").get("Current").get("Value")) and \
                    self.plat_common.is_valid_value(power_info.get("Outputs").get("Current").get("Value")):
                    return round(float(power_info.get("Outputs").get("Current").get("Value")), 3)
            except Exception as error:
                self.plat_common.log_error("Get psu out curr error:{}".format(str(error)))
        else:
            try:
                value = self.plat_common.read_file(self.__get_file_path("out_curr"))
                if self.plat_common.is_valid_value(value):
                    return round(float(value) / CommonCfg.CURRENT_FACTOR, 3)
            except Exception as error:
                self.plat_common.log_error("Get psu out current error:{}".format(str(error)))
        return None

    def get_power(self):
        """
        Retrieves current energy supplied by PSU

        Returns:
            A float number, the power in watts, e.g. 302.6
        """
        if not self.get_presence():
            return None

        if self.method in [CommonCfg.BY_RESTFUL, CommonCfg.BY_CACHE]:
            if self.method == CommonCfg.BY_RESTFUL:
                power_info = self.plat_common.get_psu_power_status_by_restful(self.index)
            else:
                power_info = self.plat_common.get_psu_info_by_cache(self.index)
            try:
                if power_info is not None and \
                    "Outputs" in power_info.keys() and \
                    "Power" in power_info.get("Outputs").keys() and \
                    self.plat_common.is_float(power_info.get("Outputs").get("Power").get("Value")) and \
                    self.plat_common.is_valid_value(power_info.get("Outputs").get("Power").get("Value")):
                    return round(float(power_info.get("Outputs").get("Power").get("Value")), 3)
            except Exception as error:
                self.plat_common.log_error("Get psu out power error:{}".format(str(error)))
        else:
            try:
                value = self.plat_common.read_file(self.__get_file_path("out_power"))
                if self.plat_common.is_valid_value(value):
                    return round(float(value) / CommonCfg.POWER_FACTOR, 3)
            except Exception as error:
                self.plat_common.log_error("Get psu out power error:{}".format(str(error)))
        return None

    def get_powergood_status(self):
        """
        Retrieves the powergood status of PSU

        Returns:
            A boolean, True if PSU has stablized its output voltages and passed all
            its internal self-tests, False if not.
        """
        if not self.get_presence():
            return False

        if self.method == CommonCfg.BY_RESTFUL:
            status_info = self.plat_common.get_psu_status_by_restful(self.index)
            if status_info is not None and "OutputStatus" in status_info.keys():
                return status_info.get("OutputStatus") == "Normal"
        elif self.method == CommonCfg.BY_CACHE:
            status_info = self.plat_common.get_psu_info_by_cache(self.index)
            if status_info is not None and \
                "Outputs" in status_info.keys() and \
                "Status" in status_info.get("Outputs"):
                return status_info.get("Outputs").get("Status") == "Normal"
        else:
            return self.plat_common.read_file(self.__get_file_path("out_status")) == "1"

        return False

    def set_status_led(self, color):
        """
        Sets the state of the PSU status LED

        Args:
            color: A string representing the color with which to set the PSU status LED
                   Note: Only support green and off
        Returns:
            bool: True if status LED state is set successfully, False if not
        """
        return False

    def get_status_led(self):
        """
        Gets the state of the PSU status LED

        Returns:
            A string, one of the predefined STATUS_LED_COLOR_* strings above
        """
        if not self.get_presence():
            return CommonCfg.NULL_VALUE

        in_status = self.get_input_status()
        out_status = self.get_powergood_status()

        if in_status and out_status:
            return self.STATUS_LED_COLOR_GREEN

        return self.STATUS_LED_COLOR_OFF

    def get_temperature(self):
        """
        Retrieves current temperature reading from PSU

        Returns:
            A float number of current temperature in Celsius up to nearest thousandth
            of one degree Celsius, e.g. 30.125
        """
        if not self.get_presence():
            return None

        if self.method in [CommonCfg.BY_RESTFUL, CommonCfg.BY_CACHE]:
            if self.method == CommonCfg.BY_RESTFUL:
                status_info = self.plat_common.get_psu_status_by_restful(self.index)
            else:
                status_info = self.plat_common.get_psu_info_by_cache(self.index)
            try:
                if status_info is not None and \
                    "Temperature" in status_info.keys() and \
                    self.plat_common.is_float(status_info.get("Temperature").get("Value")) and \
                    self.plat_common.is_valid_value(status_info.get("Temperature").get("Value")):
                    return round(float(status_info.get("Temperature").get("Value")), 3)
            except Exception as error:
                self.plat_common.log_error("Get psu temp error:{}".format(str(error)))
        else:
            try:
                temp_path = os.path.join(self.sysfs_path, "temp1", "value")
                value = self.plat_common.read_file(temp_path)
                if self.plat_common.is_valid_value(value):
                    return round(float(value) / CommonCfg.TEMPERATURE_FACTOR, 3)
            except Exception as error:
                self.plat_common.log_error("Get psu temperature error:{}".format(str(error)))
        return None

    def get_temperature_high_threshold(self):
        """
        Retrive the max temperature threshold of psu

        Returns:
            An float, return max temperature threshold of psu
        """
        if self.method in [CommonCfg.BY_RESTFUL, CommonCfg.BY_CACHE]:
            if self.method == CommonCfg.BY_RESTFUL:
                status_info = self.plat_common.get_psu_status_by_restful(self.index)
            else:
                status_info = self.plat_common.get_psu_info_by_cache(self.index)
            try:
                if status_info is not None and \
                    "Temperature" in status_info.keys() and\
                    self.plat_common.is_float(status_info.get("Temperature").get("Warning_High")) and \
                    self.plat_common.is_valid_value(status_info.get("Temperature").get("Warning_High")):
                    return round(float(status_info.get("Temperature").get("Warning_High")), 3)
            except Exception as error:
                self.plat_common.log_error("Get psu temp high error:{}".format(str(error)))
        else:
            try:
                temp_path = os.path.join(self.sysfs_path, "temp1", "max")
                value = self.plat_common.read_file(temp_path)
                if self.plat_common.is_valid_value(value):
                    return round(float(value) / CommonCfg.TEMPERATURE_FACTOR, 3)
            except Exception as error:
                self.plat_common.log_error("Get psu temperature max error:{}".format(str(error)))
        return None

    def get_temperature_low_threshold(self):
        """
        Retrive the min temperature threshold of psu

        Returns:
            An float, return min temperature threshold of psu
        """
        if self.method in [CommonCfg.BY_RESTFUL, CommonCfg.BY_CACHE]:
            if self.method == CommonCfg.BY_RESTFUL:
                status_info = self.plat_common.get_psu_status_by_restful(self.index)
            else:
                status_info = self.plat_common.get_psu_info_by_cache(self.index)
            try:
                if status_info is not None and \
                    "Temperature" in status_info.keys() and\
                    self.plat_common.is_float(status_info.get("Temperature").get("Warning_Low")) and \
                    self.plat_common.is_valid_value(status_info.get("Temperature").get("Warning_Low")):
                    return round(float(status_info.get("Temperature").get("Warning_Low")), 3)
            except Exception as error:
                self.plat_common.log_error("Get psu temp low error:{}".format(str(error)))
        else:
            try:
                temp_path = os.path.join(self.sysfs_path, "temp1", "min")
                value = self.plat_common.read_file(temp_path)
                if self.plat_common.is_valid_value(value):
                    return round(float(value) / CommonCfg.TEMPERATURE_FACTOR, 3)
            except Exception as error:
                self.plat_common.log_error("Get psu temperature min error:{}".format(str(error)))
        return None

    def get_maximum_supplied_power(self):
        """
        Retrieves the maximum supplied power by PSU

        Returns:
            A float number, the maximum power output in Watts.
            e.g. 1200.1
        """
        if not self.get_presence():
            return None

        if self.method in [CommonCfg.BY_RESTFUL, CommonCfg.BY_CACHE]:
            if self.method == CommonCfg.BY_RESTFUL:
                power_info = self.plat_common.get_psu_power_status_by_restful(self.index)
            else:
                power_info = self.plat_common.get_psu_info_by_cache(self.index)
            try:
                if power_info is not None and \
                    "Outputs" in power_info.keys() and \
                    "Power" in power_info.get("Outputs").keys() and \
                    self.plat_common.is_float(power_info.get("Outputs").get("Power").get("Critical_High")) and \
                    self.plat_common.is_valid_value(power_info.get("Outputs").get("Power").get("Critical_High")):
                    return round(float(power_info.get("Outputs").get("Power").get("Critical_High")), 3)
            except Exception as error:
                self.plat_common.log_error("Get psu max supplied power error:{}".format(str(error)))
        else:
            try:
                value = self.plat_common.read_file(self.__get_file_path("out_max_power"))
                if self.plat_common.is_valid_value(value):
                    return round(float(value) / CommonCfg.POWER_FACTOR, 3)
            except Exception as error:
                self.plat_common.log_error("Get psu max supplied power error:{}".format(str(error)))
        return None

    def get_psu_power_warning_suppress_threshold(self):
        """
        Retrieve the warning suppress threshold of the power on this PSU
        The value can be volatile, so the caller should call the API each time it is used.

        Returns:
            A float number, the warning suppress threshold of the PSU in watts.
        """
        return self.get_power_high_threshold()

    def get_psu_power_critical_threshold(self):
        """
        Retrieve the critical threshold of the power on this PSU
        The value can be volatile, so the caller should call the API each time it is used.

        Returns:
            A float number, the critical threshold of the PSU in watts.
        """
        return self.get_power_high_critical_threshold()

    #### extend api: input/output voltage threshold ###
    def get_voltage_high_threshold(self):
        """
        Retrieves the high threshold value of voltage output

        Returns:
            A float number, the high threshold value of voltage in Celsius
            up to nearest thousandth of one Volts, e.g. 30.125
        """
        if self.method in [CommonCfg.BY_RESTFUL, CommonCfg.BY_CACHE]:
            if self.method == CommonCfg.BY_RESTFUL:
                power_info = self.plat_common.get_psu_power_status_by_restful(self.index)
            else:
                power_info = self.plat_common.get_psu_info_by_cache(self.index)
            try:
                if power_info is not None and \
                    "Outputs" in power_info.keys() and \
                    "Voltage" in power_info.get("Outputs").keys() and \
                    self.plat_common.is_float(power_info.get("Outputs").get("Voltage").get("Warning_High")) and \
                    self.plat_common.is_valid_value(power_info.get("Outputs").get("Voltage").get("Warning_High")):
                    return round(float(power_info.get("Outputs").get("Voltage").get("Warning_High")), 3)
            except Exception as error:
                self.plat_common.log_error("Get psu out voltage high error:{}".format(str(error)))
        else:
            try:
                value = self.plat_common.read_file(self.__get_file_path("out_vol_max"))
                if self.plat_common.is_valid_value(value):
                    return round(float(value) / CommonCfg.VOLTAGE_FACTOR, 3)
            except Exception as error:
                self.plat_common.log_error("Get psu voltage high threshold error:{}".format(str(error)))
        return None

    def get_voltage_high_critical_threshold(self):
        """
        Retrieves the high critical threshold value of voltage output

        Returns:
            A float number, the high critical threshold value of voltage in Celsius
            up to nearest thousandth of one Volts, e.g. 30.125
        """
        if self.method in [CommonCfg.BY_RESTFUL, CommonCfg.BY_CACHE]:
            if self.method == CommonCfg.BY_RESTFUL:
                power_info = self.plat_common.get_psu_power_status_by_restful(self.index)
            else:
                power_info = self.plat_common.get_psu_info_by_cache(self.index)
            try:
                if power_info is not None and \
                    "Outputs" in power_info.keys() and \
                    "Voltage" in power_info.get("Outputs").keys() and \
                    self.plat_common.is_float(power_info.get("Outputs").get("Voltage").get("Critical_High")) and \
                    self.plat_common.is_valid_value(power_info.get("Outputs").get("Voltage").get("Critical_High")):
                    return round(float(power_info.get("Outputs").get("Voltage").get("Critical_High")), 3)
            except Exception as error:
                self.plat_common.log_error("Get psu out voltage high critical threshold error:{}".format(str(error)))
        else:
            try:
                value = self.plat_common.read_file(self.__get_file_path("out_vol_critical_max"))
                if self.plat_common.is_valid_value(value):
                    return round(float(value) / CommonCfg.VOLTAGE_FACTOR, 3)
            except Exception as error:
                self.plat_common.log_error("Get psu out voltage high critical threshold error:{}".format(str(error)))
        return None

    def get_voltage_low_threshold(self):
        """
        Retrieves the low threshold value of voltage output

        Returns:
            A float number, the low threshold value of voltage in Celsius
            up to nearest thousandth of one Volts, e.g. 30.125
        """
        if self.method in [CommonCfg.BY_RESTFUL, CommonCfg.BY_CACHE]:
            if self.method == CommonCfg.BY_RESTFUL:
                power_info = self.plat_common.get_psu_power_status_by_restful(self.index)
            else:
                power_info = self.plat_common.get_psu_info_by_cache(self.index)
            try:
                if power_info is not None and \
                    "Outputs" in power_info.keys() and \
                    "Voltage" in power_info.get("Outputs").keys() and \
                    self.plat_common.is_float(power_info.get("Outputs").get("Voltage").get("Warning_Low")) and \
                    self.plat_common.is_valid_value(power_info.get("Outputs").get("Voltage").get("Warning_Low")):
                    return round(float(power_info.get("Outputs").get("Voltage").get("Warning_Low")), 3)
            except Exception as error:
                self.plat_common.log_error("Get psu out voltage low error:{}".format(str(error)))
        else:
            try:
                value = self.plat_common.read_file(self.__get_file_path("out_vol_min"))
                if self.plat_common.is_valid_value(value):
                    return round(float(value) / CommonCfg.VOLTAGE_FACTOR, 3)
            except Exception as error:
                self.plat_common.log_error("Get psu voltage low threshold error:{}".format(str(error)))
        return None

    def get_voltage_low_critical_threshold(self):
        """
        Retrieves the low critical threshold value of voltage output

        Returns:
            A float number, the low critical threshold value of voltage in Celsius
            up to nearest thousandth of one Volts, e.g. 30.125
        """
        if self.method in [CommonCfg.BY_RESTFUL, CommonCfg.BY_CACHE]:
            if self.method == CommonCfg.BY_RESTFUL:
                power_info = self.plat_common.get_psu_power_status_by_restful(self.index)
            else:
                power_info = self.plat_common.get_psu_info_by_cache(self.index)
            try:
                if power_info is not None and \
                    "Outputs" in power_info.keys() and \
                    "Voltage" in power_info.get("Outputs").keys() and \
                    self.plat_common.is_float(power_info.get("Outputs").get("Voltage").get("Critical_Low")) and \
                    self.plat_common.is_valid_value(power_info.get("Outputs").get("Voltage").get("Critical_Low")):
                    return round(float(power_info.get("Outputs").get("Voltage").get("Critical_Low")), 3)
            except Exception as error:
                self.plat_common.log_error("Get psu out voltage low critical threshold error:{}".format(str(error)))
        else:
            try:
                value = self.plat_common.read_file(self.__get_file_path("out_vol_critical_min"))
                if self.plat_common.is_valid_value(value):
                    return round(float(value) / CommonCfg.VOLTAGE_FACTOR, 3)
            except Exception as error:
                self.plat_common.log_error("Get psu out voltage low critical threshold error:{}".format(str(error)))
        return None

    def get_input_voltage_high_threshold(self):
        """
        Retrieves the high threshold value of voltage input

        Returns:
            A float number, the high threshold value of voltage in Celsius
            up to nearest thousandth of one Volts, e.g. 30.125
        """
        if self.method in [CommonCfg.BY_RESTFUL, CommonCfg.BY_CACHE]:
            if self.method == CommonCfg.BY_RESTFUL:
                power_info = self.plat_common.get_psu_power_status_by_restful(self.index)
            else:
                power_info = self.plat_common.get_psu_info_by_cache(self.index)
            try:
                if power_info is not None and \
                    "Inputs" in power_info.keys() and \
                    "Voltage" in power_info.get("Inputs").keys() and \
                    self.plat_common.is_float(power_info.get("Inputs").get("Voltage").get("Warning_High")) and \
                    self.plat_common.is_valid_value(power_info.get("Inputs").get("Voltage").get("Warning_High")):
                    return round(float(power_info.get("Inputs").get("Voltage").get("Warning_High")), 3)
            except Exception as error:
                self.plat_common.log_error("Get psu input voltage high error:{}".format(str(error)))
        else:
            try:
                value = self.plat_common.read_file(self.__get_file_path("in_vol_max"))
                if self.plat_common.is_valid_value(value):
                    return round(float(value) / CommonCfg.VOLTAGE_FACTOR, 3)
            except Exception as error:
                self.plat_common.log_error("Get psu input voltage high threshold error:{}".format(str(error)))
        return None

    def get_input_voltage_high_critical_threshold(self):
        """
        Retrieves the high critical threshold value of voltage input

        Returns:
            A float number, the high critical threshold value of voltage in Celsius
            up to nearest thousandth of one Volts, e.g. 30.125
        """
        if self.method in [CommonCfg.BY_RESTFUL, CommonCfg.BY_CACHE]:
            if self.method == CommonCfg.BY_RESTFUL:
                power_info = self.plat_common.get_psu_power_status_by_restful(self.index)
            else:
                power_info = self.plat_common.get_psu_info_by_cache(self.index)
            try:
                if power_info is not None and \
                    "Inputs" in power_info.keys() and \
                    "Voltage" in power_info.get("Inputs").keys() and \
                    self.plat_common.is_float(power_info.get("Inputs").get("Voltage").get("Critical_High")) and \
                    self.plat_common.is_valid_value(power_info.get("Inputs").get("Voltage").get("Critical_High")):
                    return round(float(power_info.get("Inputs").get("Voltage").get("Critical_High")), 3)
            except Exception as error:
                self.plat_common.log_error("Get psu input voltage high critical threshold error:{}".format(str(error)))
        else:
            try:
                value = self.plat_common.read_file(self.__get_file_path("in_vol_critical_max"))
                if self.plat_common.is_valid_value(value):
                    return round(float(value) / CommonCfg.VOLTAGE_FACTOR, 3)
            except Exception as error:
                self.plat_common.log_error("Get psu input voltage high critical threshold error:{}".format(str(error)))
        return None

    def get_input_voltage_low_threshold(self):
        """
        Retrieves the low threshold value of voltage input

        Returns:
            A float number, the low threshold value of voltage in Celsius
            up to nearest thousandth of one Volts, e.g. 30.125
        """
        if self.method in [CommonCfg.BY_RESTFUL, CommonCfg.BY_CACHE]:
            if self.method == CommonCfg.BY_RESTFUL:
                power_info = self.plat_common.get_psu_power_status_by_restful(self.index)
            else:
                power_info = self.plat_common.get_psu_info_by_cache(self.index)
            try:
                if power_info is not None and \
                    "Inputs" in power_info.keys() and \
                    "Voltage" in power_info.get("Inputs").keys() and \
                    self.plat_common.is_float(power_info.get("Inputs").get("Voltage").get("Warning_Low")) and \
                    self.plat_common.is_valid_value(power_info.get("Inputs").get("Voltage").get("Warning_Low")):
                    return round(float(power_info.get("Inputs").get("Voltage").get("Warning_Low")), 3)
            except Exception as error:
                self.plat_common.log_error("Get psu input voltage low error:{}".format(str(error)))
        else:
            try:
                value = self.plat_common.read_file(self.__get_file_path("in_vol_min"))
                if self.plat_common.is_valid_value(value):
                    return round(float(value) / CommonCfg.VOLTAGE_FACTOR, 3)
            except Exception as error:
                self.plat_common.log_error("Get psu input voltage low threshold error:{}".format(str(error)))
        return None

    def get_input_voltage_low_critical_threshold(self):
        """
        Retrieves the low critical threshold value of voltage input

        Returns:
            A float number, the low critical threshold value of voltage in Celsius
            up to nearest thousandth of one Volts, e.g. 30.125
        """
        if self.method in [CommonCfg.BY_RESTFUL, CommonCfg.BY_CACHE]:
            if self.method == CommonCfg.BY_RESTFUL:
                power_info = self.plat_common.get_psu_power_status_by_restful(self.index)
            else:
                power_info = self.plat_common.get_psu_info_by_cache(self.index)
            try:
                if power_info is not None and \
                    "Inputs" in power_info.keys() and \
                    "Voltage" in power_info.get("Inputs").keys() and \
                    self.plat_common.is_float(power_info.get("Inputs").get("Voltage").get("Critical_Low")) and \
                    self.plat_common.is_valid_value(power_info.get("Inputs").get("Voltage").get("Critical_Low")):
                    return round(float(power_info.get("Inputs").get("Voltage").get("Critical_Low")), 3)
            except Exception as error:
                self.plat_common.log_error("Get psu input voltage low critical threshold error:{}".format(str(error)))
        else:
            try:
                value = self.plat_common.read_file(self.__get_file_path("in_vol_critical_min"))
                if self.plat_common.is_valid_value(value):
                    return round(float(value) / CommonCfg.VOLTAGE_FACTOR, 3)
            except Exception as error:
                self.plat_common.log_error("Get psu input voltage low critical threshold error:{}".format(str(error)))
        return None

    #### extend api: input/output current threshold ###
    def get_current_high_threshold(self):
        """
        Retrieves the high threshold value of current output

        Returns:
            A float number, the high threshold value of current in A
            up to nearest thousandth of one current, e.g. 30.125
        """
        if self.method in [CommonCfg.BY_RESTFUL, CommonCfg.BY_CACHE]:
            if self.method == CommonCfg.BY_RESTFUL:
                power_info = self.plat_common.get_psu_power_status_by_restful(self.index)
            else:
                power_info = self.plat_common.get_psu_info_by_cache(self.index)
            try:
                if power_info is not None and \
                    "Outputs" in power_info.keys() and \
                    "Current" in power_info.get("Outputs").keys() and \
                    self.plat_common.is_float(power_info.get("Outputs").get("Current").get("Warning_High")) and \
                    self.plat_common.is_valid_value(power_info.get("Outputs").get("Current").get("Warning_High")):
                    return round(float(power_info.get("Outputs").get("Current").get("Warning_High")), 3)
            except Exception as error:
                self.plat_common.log_error("Get psu out current high error:{}".format(str(error)))
        else:
            try:
                value = self.plat_common.read_file(self.__get_file_path("out_curr_max"))
                if self.plat_common.is_valid_value(value):
                    return round(float(value) / CommonCfg.CURRENT_FACTOR, 3)
            except Exception as error:
                self.plat_common.log_error("Get psu current high threshold error:{}".format(str(error)))
        return None

    def get_current_high_critical_threshold(self):
        """
        Retrieves the high critical threshold value of current output

        Returns:
            A float number, the high critical threshold value of current in A
            up to nearest thousandth of one current, e.g. 30.125
        """
        if self.method in [CommonCfg.BY_RESTFUL, CommonCfg.BY_CACHE]:
            if self.method == CommonCfg.BY_RESTFUL:
                power_info = self.plat_common.get_psu_power_status_by_restful(self.index)
            else:
                power_info = self.plat_common.get_psu_info_by_cache(self.index)
            try:
                if power_info is not None and \
                    "Outputs" in power_info.keys() and \
                    "Current" in power_info.get("Outputs").keys() and \
                    self.plat_common.is_float(power_info.get("Outputs").get("Current").get("Critical_High")) and \
                    self.plat_common.is_valid_value(power_info.get("Outputs").get("Current").get("Critical_High")):
                    return round(float(power_info.get("Outputs").get("Current").get("Critical_High")), 3)
            except Exception as error:
                self.plat_common.log_error("Get psu out current high critical threshold error:{}".format(str(error)))
        else:
            try:
                value = self.plat_common.read_file(self.__get_file_path("out_curr_critical_max"))
                if self.plat_common.is_valid_value(value):
                    return round(float(value) / CommonCfg.CURRENT_FACTOR, 3)
            except Exception as error:
                self.plat_common.log_error("Get psu current high critical threshold error:{}".format(str(error)))
        return None

    def get_current_low_threshold(self):
        """
        Retrieves the low threshold value of current output

        Returns:
            A float number, the low threshold value of current in A
            up to nearest thousandth of one current, e.g. 30.125
        """
        if self.method in [CommonCfg.BY_RESTFUL, CommonCfg.BY_CACHE]:
            if self.method == CommonCfg.BY_RESTFUL:
                power_info = self.plat_common.get_psu_power_status_by_restful(self.index)
            else:
                power_info = self.plat_common.get_psu_info_by_cache(self.index)
            try:
                if power_info is not None and \
                    "Outputs" in power_info.keys() and \
                    "Current" in power_info.get("Outputs").keys() and \
                    self.plat_common.is_float(power_info.get("Outputs").get("Current").get("Warning_Low")) and \
                    self.plat_common.is_valid_value(power_info.get("Outputs").get("Current").get("Warning_Low")):
                    return round(float(power_info.get("Outputs").get("Current").get("Warning_Low")), 3)
            except Exception as error:
                self.plat_common.log_error("Get psu out current low error:{}".format(str(error)))
        else:
            try:
                value = self.plat_common.read_file(self.__get_file_path("out_curr_min"))
                if self.plat_common.is_valid_value(value):
                    return round(float(value) / CommonCfg.CURRENT_FACTOR, 3)
            except Exception as error:
                self.plat_common.log_error("Get psu current low threshold error:{}".format(str(error)))
        return None

    def get_current_low_critical_threshold(self):
        """
        Retrieves the low critical threshold value of current output

        Returns:
            A float number, the low critical threshold value of current in A
            up to nearest thousandth of one current, e.g. 30.125
        """
        if self.method in [CommonCfg.BY_RESTFUL, CommonCfg.BY_CACHE]:
            if self.method == CommonCfg.BY_RESTFUL:
                power_info = self.plat_common.get_psu_power_status_by_restful(self.index)
            else:
                power_info = self.plat_common.get_psu_info_by_cache(self.index)
            try:
                if power_info is not None and \
                    "Outputs" in power_info.keys() and \
                    "Current" in power_info.get("Outputs").keys() and \
                    self.plat_common.is_float(power_info.get("Outputs").get("Current").get("Critical_Low")) and \
                    self.plat_common.is_valid_value(power_info.get("Outputs").get("Current").get("Critical_Low")):
                    return round(float(power_info.get("Outputs").get("Current").get("Critical_Low")), 3)
            except Exception as error:
                self.plat_common.log_error("Get psu out current low critical threshold error:{}".format(str(error)))
        else:
            try:
                value = self.plat_common.read_file(self.__get_file_path("out_curr_critical_min"))
                if self.plat_common.is_valid_value(value):
                    return round(float(value) / CommonCfg.CURRENT_FACTOR, 3)
            except Exception as error:
                self.plat_common.log_error("Get psu current low critical threshold error:{}".format(str(error)))
        return None

    def get_input_current_high_threshold(self):
        """
        Retrieves the high threshold value of current input

        Returns:
            A float number, the high threshold value of current in A
            up to nearest thousandth of one current, e.g. 30.125
        """
        if self.method in [CommonCfg.BY_RESTFUL, CommonCfg.BY_CACHE]:
            if self.method == CommonCfg.BY_RESTFUL:
                power_info = self.plat_common.get_psu_power_status_by_restful(self.index)
            else:
                power_info = self.plat_common.get_psu_info_by_cache(self.index)
            try:
                if power_info is not None and \
                    "Inputs" in power_info.keys() and \
                    "Current" in power_info.get("Inputs").keys() and \
                    self.plat_common.is_float(power_info.get("Inputs").get("Current").get("Warning_High")) and \
                    self.plat_common.is_valid_value(power_info.get("Inputs").get("Current").get("Warning_High")):
                    return round(float(power_info.get("Inputs").get("Current").get("Warning_High")), 3)
            except Exception as error:
                self.plat_common.log_error("Get psu input current high error:{}".format(str(error)))
        else:
            try:
                value = self.plat_common.read_file(self.__get_file_path("in_curr_max"))
                if self.plat_common.is_valid_value(value):
                    return round(float(value) / CommonCfg.CURRENT_FACTOR, 3)
            except Exception as error:
                self.plat_common.log_error("Get psu input current high threshold error:{}".format(str(error)))
        return None

    def get_input_current_high_critical_threshold(self):
        """
        Retrieves the high critical threshold value of current input

        Returns:
            A float number, the high critical threshold value of current in A
            up to nearest thousandth of one current, e.g. 30.125
        """
        if self.method in [CommonCfg.BY_RESTFUL, CommonCfg.BY_CACHE]:
            if self.method == CommonCfg.BY_RESTFUL:
                power_info = self.plat_common.get_psu_power_status_by_restful(self.index)
            else:
                power_info = self.plat_common.get_psu_info_by_cache(self.index)
            try:
                if power_info is not None and \
                    "Inputs" in power_info.keys() and \
                    "Current" in power_info.get("Inputs").keys() and \
                    self.plat_common.is_float(power_info.get("Inputs").get("Current").get("Critical_High")) and \
                    self.plat_common.is_valid_value(power_info.get("Inputs").get("Current").get("Critical_High")):
                    return round(float(power_info.get("Inputs").get("Current").get("Critical_High")), 3)
            except Exception as error:
                self.plat_common.log_error("Get psu input current high critical threshold error:{}".format(str(error)))
        else:
            try:
                value = self.plat_common.read_file(self.__get_file_path("in_curr_critical_max"))
                if self.plat_common.is_valid_value(value):
                    return round(float(value) / CommonCfg.CURRENT_FACTOR, 3)
            except Exception as error:
                self.plat_common.log_error("Get psu input current high critical threshold error:{}".format(str(error)))
        return None

    def get_input_current_low_threshold(self):
        """
        Retrieves the low threshold value of current input

        Returns:
            A float number, the low threshold value of current in A
            up to nearest thousandth of one current, e.g. 30.125
        """
        if self.method in [CommonCfg.BY_RESTFUL, CommonCfg.BY_CACHE]:
            if self.method == CommonCfg.BY_RESTFUL:
                power_info = self.plat_common.get_psu_power_status_by_restful(self.index)
            else:
                power_info = self.plat_common.get_psu_info_by_cache(self.index)
            try:
                if power_info is not None and \
                    "Inputs" in power_info.keys() and \
                    "Current" in power_info.get("Inputs").keys() and \
                    self.plat_common.is_float(power_info.get("Inputs").get("Current").get("Warning_Low")) and \
                    self.plat_common.is_valid_value(power_info.get("Inputs").get("Current").get("Warning_Low")):
                    return round(float(power_info.get("Inputs").get("Current").get("Warning_Low")), 3)
            except Exception as error:
                self.plat_common.log_error("Get psu input current low error:{}".format(str(error)))
        else:
            try:
                value = self.plat_common.read_file(self.__get_file_path("in_curr_min"))
                if self.plat_common.is_valid_value(value):
                    return round(float(value) / CommonCfg.CURRENT_FACTOR, 3)
            except Exception as error:
                self.plat_common.log_error("Get psu input current low threshold error:{}".format(str(error)))
        return None

    def get_input_current_low_critical_threshold(self):
        """
        Retrieves the low critical threshold value of current input

        Returns:
            A float number, the low critical threshold value of current in A
            up to nearest thousandth of one current, e.g. 30.125
        """
        if self.method in [CommonCfg.BY_RESTFUL, CommonCfg.BY_CACHE]:
            if self.method == CommonCfg.BY_RESTFUL:
                power_info = self.plat_common.get_psu_power_status_by_restful(self.index)
            else:
                power_info = self.plat_common.get_psu_info_by_cache(self.index)
            try:
                if power_info is not None and \
                    "Inputs" in power_info.keys() and \
                    "Current" in power_info.get("Inputs").keys() and \
                    self.plat_common.is_float(power_info.get("Inputs").get("Current").get("Critical_Low")) and \
                    self.plat_common.is_valid_value(power_info.get("Inputs").get("Current").get("Critical_Low")):
                    return round(float(power_info.get("Inputs").get("Current").get("Critical_Low")), 3)
            except Exception as error:
                self.plat_common.log_error("Get psu input current low critical threshold error:{}".format(str(error)))
        else:
            try:
                value = self.plat_common.read_file(self.__get_file_path("in_curr_critical_min"))
                if self.plat_common.is_valid_value(value):
                    return round(float(value) / CommonCfg.CURRENT_FACTOR, 3)
            except Exception as error:
                self.plat_common.log_error("Get psu input current low critical threshold error:{}".format(str(error)))
        return None

    #### extend api: input/output power threshold ###
    def get_power_high_threshold(self):
        """
        Retrieves the high threshold value of power output

        Returns:
            A float number, the high threshold value of power in Watts
            up to nearest thousandth of one Watts, e.g. 30.125
        """
        if self.method in [CommonCfg.BY_RESTFUL, CommonCfg.BY_CACHE]:
            if self.method == CommonCfg.BY_RESTFUL:
                power_info = self.plat_common.get_psu_power_status_by_restful(self.index)
            else:
                power_info = self.plat_common.get_psu_info_by_cache(self.index)
            try:
                if power_info is not None and \
                    "Outputs" in power_info.keys() and \
                    "Power" in power_info.get("Outputs").keys() and \
                    self.plat_common.is_float(power_info.get("Outputs").get("Power").get("Warning_High")) and \
                    self.plat_common.is_valid_value(power_info.get("Outputs").get("Power").get("Warning_High")):
                    return round(float(power_info.get("Outputs").get("Power").get("Warning_High")), 3)
            except Exception as error:
                self.plat_common.log_error("Get psu out power high error:{}".format(str(error)))
        else:
            try:
                value = self.plat_common.read_file(self.__get_file_path("out_power_max"))
                if self.plat_common.is_valid_value(value):
                    return round(float(value) / CommonCfg.POWER_FACTOR, 3)
            except Exception as error:
                self.plat_common.log_error("Get psu power high threshold error:{}".format(str(error)))
        return None

    def get_power_high_critical_threshold(self):
        """
        Retrieves the high critical threshold value of power output

        Returns:
            A float number, the high critical threshold value of power in Watts
            up to nearest thousandth of one Watts, e.g. 30.125
        """
        if self.method in [CommonCfg.BY_RESTFUL, CommonCfg.BY_CACHE]:
            if self.method == CommonCfg.BY_RESTFUL:
                power_info = self.plat_common.get_psu_power_status_by_restful(self.index)
            else:
                power_info = self.plat_common.get_psu_info_by_cache(self.index)
            try:
                if power_info is not None and \
                    "Outputs" in power_info.keys() and \
                    "Power" in power_info.get("Outputs").keys() and \
                    self.plat_common.is_float(power_info.get("Outputs").get("Power").get("Critical_High")) and \
                    self.plat_common.is_valid_value(power_info.get("Outputs").get("Power").get("Critical_High")):
                    return round(float(power_info.get("Outputs").get("Power").get("Critical_High")), 3)
            except Exception as error:
                self.plat_common.log_error("Get psu out power high critical threshold error:{}".format(str(error)))
        else:
            try:
                value = self.plat_common.read_file(self.__get_file_path("out_power_critical_max"))
                if self.plat_common.is_valid_value(value):
                    return round(float(value) / CommonCfg.POWER_FACTOR, 3)
            except Exception as error:
                self.plat_common.log_error("Get psu power high critical threshold error:{}".format(str(error)))
        return None

    def get_power_low_threshold(self):
        """
        Retrieves the low threshold value of power output

        Returns:
            A float number, the low threshold value of power in Watts
            up to nearest thousandth of one Watts, e.g. 30.125
        """
        if self.method in [CommonCfg.BY_RESTFUL, CommonCfg.BY_CACHE]:
            if self.method == CommonCfg.BY_RESTFUL:
                power_info = self.plat_common.get_psu_power_status_by_restful(self.index)
            else:
                power_info = self.plat_common.get_psu_info_by_cache(self.index)
            try:
                if power_info is not None and \
                    "Outputs" in power_info.keys() and \
                    "Power" in power_info.get("Outputs").keys() and \
                    self.plat_common.is_float(power_info.get("Outputs").get("Power").get("Warning_Low")) and \
                    self.plat_common.is_valid_value(power_info.get("Outputs").get("Power").get("Warning_Low")):
                    return round(float(power_info.get("Outputs").get("Power").get("Warning_Low")), 3)
            except Exception as error:
                self.plat_common.log_error("Get psu out power low error:{}".format(str(error)))
        else:
            try:
                value = self.plat_common.read_file(self.__get_file_path("out_power_min"))
                if self.plat_common.is_valid_value(value):
                    return round(float(value) / CommonCfg.POWER_FACTOR, 3)
            except Exception as error:
                self.plat_common.log_error("Get psu power low threshold error:{}".format(str(error)))
        return None

    def get_power_low_critical_threshold(self):
        """
        Retrieves the low critical threshold value of power output

        Returns:
            A float number, the low critical threshold value of power in Watts
            up to nearest thousandth of one Watts, e.g. 30.125
        """
        if self.method in [CommonCfg.BY_RESTFUL, CommonCfg.BY_CACHE]:
            if self.method == CommonCfg.BY_RESTFUL:
                power_info = self.plat_common.get_psu_power_status_by_restful(self.index)
            else:
                power_info = self.plat_common.get_psu_info_by_cache(self.index)
            try:
                if power_info is not None and \
                    "Outputs" in power_info.keys() and \
                    "Power" in power_info.get("Outputs").keys() and \
                    self.plat_common.is_float(power_info.get("Outputs").get("Power").get("Critical_Low")) and \
                    self.plat_common.is_valid_value(power_info.get("Outputs").get("Power").get("Critical_Low")):
                    return round(float(power_info.get("Outputs").get("Power").get("Critical_Low")), 3)
            except Exception as error:
                self.plat_common.log_error("Get psu out power low critical threshold error:{}".format(str(error)))
        else:
            try:
                value = self.plat_common.read_file(self.__get_file_path("out_power_critical_min"))
                if self.plat_common.is_valid_value(value):
                    return round(float(value) / CommonCfg.POWER_FACTOR, 3)
            except Exception as error:
                self.plat_common.log_error("Get psu power low critical threshold error:{}".format(str(error)))
        return None

    def get_input_power_high_threshold(self):
        """
        Retrieves the high threshold value of power input

        Returns:
            A float number, the high threshold value of power in Watts
            up to nearest thousandth of one Watts, e.g. 30.125
        """
        if self.method in [CommonCfg.BY_RESTFUL, CommonCfg.BY_CACHE]:
            if self.method == CommonCfg.BY_RESTFUL:
                power_info = self.plat_common.get_psu_power_status_by_restful(self.index)
            else:
                power_info = self.plat_common.get_psu_info_by_cache(self.index)
            try:
                if power_info is not None and \
                    "Inputs" in power_info.keys() and \
                    "Power" in power_info.get("Inputs").keys() and \
                    self.plat_common.is_float(power_info.get("Inputs").get("Power").get("Warning_High")) and \
                    self.plat_common.is_valid_value(power_info.get("Inputs").get("Power").get("Warning_High")):
                    return round(float(power_info.get("Inputs").get("Power").get("Warning_High")), 3)
            except Exception as error:
                self.plat_common.log_error("Get psu input power high error:{}".format(str(error)))
        else:
            try:
                value = self.plat_common.read_file(self.__get_file_path("in_power_max"))
                if self.plat_common.is_valid_value(value):
                    return round(float(value) / CommonCfg.POWER_FACTOR, 3)
            except Exception as error:
                self.plat_common.log_error("Get psu input power high threshold error:{}".format(str(error)))
        return None

    def get_input_power_high_critical_threshold(self):
        """
        Retrieves the high critical threshold value of power input

        Returns:
            A float number, the high critical threshold value of power in Watts
            up to nearest thousandth of one Watts, e.g. 30.125
        """
        if self.method in [CommonCfg.BY_RESTFUL, CommonCfg.BY_CACHE]:
            if self.method == CommonCfg.BY_RESTFUL:
                power_info = self.plat_common.get_psu_power_status_by_restful(self.index)
            else:
                power_info = self.plat_common.get_psu_info_by_cache(self.index)
            try:
                if power_info is not None and \
                    "Inputs" in power_info.keys() and \
                    "Power" in power_info.get("Inputs").keys() and \
                    self.plat_common.is_float(power_info.get("Inputs").get("Power").get("Critical_High")) and \
                    self.plat_common.is_valid_value(power_info.get("Inputs").get("Power").get("Critical_High")):
                    return round(float(power_info.get("Inputs").get("Power").get("Critical_High")), 3)
            except Exception as error:
                self.plat_common.log_error("Get psu input power high critical threshold error:{}".format(str(error)))
        else:
            try:
                value = self.plat_common.read_file(self.__get_file_path("in_power_critical_max"))
                if self.plat_common.is_valid_value(value):
                    return round(float(value) / CommonCfg.POWER_FACTOR, 3)
            except Exception as error:
                self.plat_common.log_error("Get psu input power high critical threshold error:{}".format(str(error)))
        return None

    def get_input_power_low_threshold(self):
        """
        Retrieves the low threshold value of power input

        Returns:
            A float number, the low threshold value of power in Watts
            up to nearest thousandth of one Watts, e.g. 30.125
        """
        if self.method in [CommonCfg.BY_RESTFUL, CommonCfg.BY_CACHE]:
            if self.method == CommonCfg.BY_RESTFUL:
                power_info = self.plat_common.get_psu_power_status_by_restful(self.index)
            else:
                power_info = self.plat_common.get_psu_info_by_cache(self.index)
            try:
                if power_info is not None and \
                    "Inputs" in power_info.keys() and \
                    "Power" in power_info.get("Inputs").keys() and \
                    self.plat_common.is_float(power_info.get("Inputs").get("Power").get("Warning_Low")) and \
                    self.plat_common.is_valid_value(power_info.get("Inputs").get("Power").get("Warning_Low")):
                    return round(float(power_info.get("Inputs").get("Power").get("Warning_Low")), 3)
            except Exception as error:
                self.plat_common.log_error("Get psu input power low error:{}".format(str(error)))
        else:
            try:
                value = self.plat_common.read_file(self.__get_file_path("in_power_min"))
                if self.plat_common.is_valid_value(value):
                    return round(float(value) / CommonCfg.POWER_FACTOR, 3)
            except Exception as error:
                self.plat_common.log_error("Get psu input power low threshold error:{}".format(str(error)))
        return None

    def get_input_power_low_critical_threshold(self):
        """
        Retrieves the low critical threshold value of power input

        Returns:
            A float number, the low critical threshold value of power in Watts
            up to nearest thousandth of one Watts, e.g. 30.125
        """
        if self.method in [CommonCfg.BY_RESTFUL, CommonCfg.BY_CACHE]:
            if self.method == CommonCfg.BY_RESTFUL:
                power_info = self.plat_common.get_psu_power_status_by_restful(self.index)
            else:
                power_info = self.plat_common.get_psu_info_by_cache(self.index)
            try:
                if power_info is not None and \
                    "Inputs" in power_info.keys() and \
                    "Power" in power_info.get("Inputs").keys() and \
                    self.plat_common.is_float(power_info.get("Inputs").get("Power").get("Critical_Low")) and \
                    self.plat_common.is_valid_value(power_info.get("Inputs").get("Power").get("Critical_Low")):
                    return round(float(power_info.get("Inputs").get("Power").get("Critical_Low")), 3)
            except Exception as error:
                self.plat_common.log_error("Get psu input power low critical threshold error:{}".format(str(error)))
        else:
            try:
                value = self.plat_common.read_file(self.__get_file_path("in_power_critical_min"))
                if self.plat_common.is_valid_value(value):
                    return round(float(value) / CommonCfg.POWER_FACTOR, 3)
            except Exception as error:
                self.plat_common.log_error("Get psu input power low critical threshold error:{}".format(str(error)))
        return None

    #### extend api: rated power ###
    def get_rated_power(self):
        """
        Retrieves the maximum supplied power by PSU

        Returns:
            A float number, the maximum power output in Watts.
            e.g. 1200.1
        """
        return self.get_maximum_supplied_power()

    #### extend api: psu input status ###
    def get_input_status(self):
        """
        Retrieves the input status of PSU

        Returns:
            A boolean, True if PSU has stablized its input voltages and passed all
            its internal self-tests, False if not.
        """
        if not self.get_presence():
            return False

        if self.method in [CommonCfg.BY_RESTFUL, CommonCfg.BY_CACHE]:
            if self.method == CommonCfg.BY_RESTFUL:
                status_info = self.plat_common.get_psu_power_status_by_restful(self.index)
            else:
                status_info = self.plat_common.get_psu_info_by_cache(self.index)
            try:
                if status_info is not None and \
                    "Inputs" in status_info.keys() and \
                    "Status" in status_info.get("Inputs").keys():
                    return status_info.get("Inputs").get("Status") == "Normal"
            except Exception as error:
                self.plat_common.log_error("Get psu input stauts error:{}".format(str(error)))
        else:
            return self.plat_common.read_file(self.__get_file_path("in_status")) == "1"

        return False

    #### extend api: psu input power type ###
    def get_type(self):
        """
        Retrieves the input power type of PSU

        Returns:
            String, input power type, including 'AC', 'DC'.
        """
        if not self.get_presence():
            return CommonCfg.NULL_VALUE

        if self.method in [CommonCfg.BY_RESTFUL, CommonCfg.BY_CACHE]:
            if self.method == CommonCfg.BY_RESTFUL:
                status_info = self.plat_common.get_psu_power_status_by_restful(self.index)
            else:
                status_info = self.plat_common.get_psu_info_by_cache(self.index)
            try:
                if status_info is not None and \
                    "Inputs" in status_info.keys() and \
                    "Type" in status_info.get("Inputs").keys():
                    return status_info.get("Inputs").get("Type")
            except Exception as error:
                self.plat_common.log_error("Get psu input type error:{}".format(str(error)))
        else:
            value = self.plat_common.read_file(self.__get_file_path("type"))
            if value == "1":
                return AC_INPUT
            if value == "0":
                return DC_INPUT
        return CommonCfg.NULL_VALUE

    def get_input_voltage(self):
        """
        Retrieves current PSU voltage input

        Returns:
            A float number, the input voltage in volts,
            e.g. 12.1
        """
        if not self.get_presence():
            return None

        if self.method in [CommonCfg.BY_RESTFUL, CommonCfg.BY_CACHE]:
            if self.method == CommonCfg.BY_RESTFUL:
                power_info = self.plat_common.get_psu_power_status_by_restful(self.index)
            else:
                power_info = self.plat_common.get_psu_info_by_cache(self.index)
            try:
                if power_info is not None and \
                    "Inputs" in power_info.keys() and \
                    "Voltage" in power_info.get("Inputs").keys() and \
                    self.plat_common.is_float(power_info.get("Inputs").get("Voltage").get("Value")):
                    # when psu is insert but not power on, bmc returned -99999.0 for input voltage, in order to show value
                    # input current, voltage, power uniform, here we return 0.0 when input voltage is -99999.0
                    if power_info.get("Inputs").get("Voltage").get("Value") == -99999.0:
                        return 0.0
                    if self.plat_common.is_valid_value(power_info.get("Inputs").get("Voltage").get("Value")):
                        return round(float(power_info.get("Inputs").get("Voltage").get("Value")), 3)
            except Exception as error:
                self.plat_common.log_error("Get psu input voltage error:{}".format(str(error)))
        else:
            try:
                value = self.plat_common.read_file(self.__get_file_path("in_vol"))
                if self.plat_common.is_valid_value(value):
                    return round(float(value) / CommonCfg.VOLTAGE_FACTOR, 3)
            except Exception as error:
                self.plat_common.log_error("Get psu input voltage error:{}".format(str(error)))
        return None

    def get_input_current(self):
        """
        Retrieves present electric current supplied by PSU

        Returns:
            A float number, the electric current in amperes, e.g 15.4
        """
        if not self.get_presence():
            return None

        if self.method in [CommonCfg.BY_RESTFUL, CommonCfg.BY_CACHE]:
            if self.method == CommonCfg.BY_RESTFUL:
                power_info = self.plat_common.get_psu_power_status_by_restful(self.index)
            else:
                power_info = self.plat_common.get_psu_info_by_cache(self.index)
            try:
                if power_info is not None and \
                    "Inputs" in power_info.keys() and \
                    "Current" in power_info.get("Inputs").keys() and \
                    self.plat_common.is_float(power_info.get("Inputs").get("Current").get("Value")) and \
                    self.plat_common.is_valid_value(power_info.get("Inputs").get("Current").get("Value")):
                    return round(float(power_info.get("Inputs").get("Current").get("Value")), 3)
            except Exception as error:
                self.plat_common.log_error("Get psu input curr error:{}".format(str(error)))
        else:
            try:
                value = self.plat_common.read_file(self.__get_file_path("in_curr"))
                if self.plat_common.is_valid_value(value):
                    return round(float(value) / CommonCfg.CURRENT_FACTOR, 3)
            except Exception as error:
                self.plat_common.log_error("Get psu input current error:{}".format(str(error)))
        return None

    def get_input_power(self):
        """
        Retrieves current energy supplied by PSU

        Returns:
            A float number, the power in watts, e.g. 302.6
        """
        if not self.get_presence():
            return None

        if self.method in [CommonCfg.BY_RESTFUL, CommonCfg.BY_CACHE]:
            if self.method == CommonCfg.BY_RESTFUL:
                power_info = self.plat_common.get_psu_power_status_by_restful(self.index)
            else:
                power_info = self.plat_common.get_psu_info_by_cache(self.index)
            try:
                if power_info is not None and \
                    "Inputs" in power_info.keys() and \
                    "Power" in power_info.get("Inputs").keys() and \
                    self.plat_common.is_float(power_info.get("Inputs").get("Power").get("Value")) and \
                    self.plat_common.is_valid_value(power_info.get("Inputs").get("Power").get("Value")):
                    return round(float(power_info.get("Inputs").get("Power").get("Value")), 3)
            except Exception as error:
                self.plat_common.log_error("Get psu input power error:{}".format(str(error)))
        else:
            try:
                value = self.plat_common.read_file(self.__get_file_path("in_power"))
                if self.plat_common.is_valid_value(value):
                    return round(float(value) / CommonCfg.POWER_FACTOR, 3)
            except Exception as error:
                self.plat_common.log_error("Get psu input power error:{}".format(str(error)))
        return None

    ### extend api: PSU FAN ####
    def get_fan_direction(self):
        """
        Retrieves the direction of fan

        Returns:
            A string, either "F2B" or "B2F" depending on fan direction
        """
        if not self.get_presence():
            return CommonCfg.NULL_VALUE

        if self.method in [CommonCfg.BY_RESTFUL, CommonCfg.BY_CACHE]:
            if self.method == CommonCfg.BY_RESTFUL:
                psu_info = self.plat_common.get_psu_mfr_by_restful(self.index)
            else:
                psu_info = self.plat_common.get_psu_info_by_cache(self.index)
            try:
                if psu_info is not None and "AirFlow" in psu_info.keys():
                    return psu_info.get("AirFlow")
            except Exception as error:
                self.plat_common.log_error("Get psu fan direction error:{}".format(str(error)))
        else:
            try:
                value = self.plat_common.read_file(self.__get_file_path("fan_direction"))
                if self.plat_common.is_valid_value(value):
                    return CommonCfg.FAN_DIRECTION_MAP[int(value)]
            except Exception as error:
                self.plat_common.log_error("Get psu fan direction error:{}".format(str(error)))

        return CommonCfg.NULL_VALUE

    def get_fan_speed_rpm(self):
        """
        Retrieves PSU Fan speed

        Returns:
            A integer number, the fan speed in RPM, e.g. 6000
        """
        if not self.get_presence():
            return None

        if self.method in [CommonCfg.BY_RESTFUL, CommonCfg.BY_CACHE]:
            if self.method == CommonCfg.BY_RESTFUL:
                status_info = self.plat_common.get_psu_status_by_restful(self.index)
            else:
                status_info = self.plat_common.get_psu_info_by_cache(self.index)
            try:
                if status_info is not None and \
                    "FanSpeed" in status_info.keys() and \
                    self.plat_common.is_float(status_info.get("FanSpeed").get("Value")) and \
                    self.plat_common.is_valid_value(status_info.get("FanSpeed").get("Value")):
                    return int(float(status_info.get("FanSpeed").get("Value")))
            except Exception as error:
                self.plat_common.log_error("Get psu fan speed error:{}".format(str(error)))
        else:
            try:
                value = self.plat_common.read_file(self.__get_file_path("fan_speed"))
                if self.plat_common.is_valid_value(value):
                    return int(value)
            except Exception as error:
                self.plat_common.log_error("Get psu fan speed error:{}".format(str(error)))
        return None

    def get_fan_speed_rpm_max(self):
        """
        Retrieves PSU Fan max speed

        Returns:
            A integer number, the fan speed in RPM, e.g. 6000
        """
        if self.method in [CommonCfg.BY_RESTFUL, CommonCfg.BY_CACHE]:
            if self.method == CommonCfg.BY_RESTFUL:
                status_info = self.plat_common.get_psu_status_by_restful(self.index)
            else:
                status_info = self.plat_common.get_psu_info_by_cache(self.index)
            try:
                if status_info is not None and \
                    "FanSpeed" in status_info.keys() and \
                    self.plat_common.is_float(status_info.get("FanSpeed").get("Max")) and \
                    self.plat_common.is_valid_value(status_info.get("FanSpeed").get("Max")):
                    return int(float(status_info.get("FanSpeed").get("Max")))
            except Exception as error:
                self.plat_common.log_error("Get psu fan speed max error:{}".format(str(error)))
        else:
            try:
                value = self.plat_common.read_file(self.__get_file_path("fan_speed_max"))
                if self.plat_common.is_valid_value(value):
                    return int(value)
            except Exception as error:
                self.plat_common.log_error("Get psu fan max speed error:{}".format(str(error)))
        return None

    def get_fan_speed_rpm_min(self):
        """
        Retrieves PSU Fan min speed

        Returns:
            A integer number, the fan speed in RPM, e.g. 6000
        """
        if self.method in [CommonCfg.BY_RESTFUL, CommonCfg.BY_CACHE]:
            if self.method == CommonCfg.BY_RESTFUL:
                status_info = self.plat_common.get_psu_status_by_restful(self.index)
            else:
                status_info = self.plat_common.get_psu_info_by_cache(self.index)
            try:
                if status_info is not None and \
                    "FanSpeed" in status_info.keys() and \
                    self.plat_common.is_float(status_info.get("FanSpeed").get("Min")) and \
                    self.plat_common.is_valid_value(status_info.get("FanSpeed").get("Min")):
                    return int(float(status_info.get("FanSpeed").get("Min")))
            except Exception as error:
                self.plat_common.log_error("Get psu fan speed min error:{}".format(str(error)))
        else:
            try:
                value = self.plat_common.read_file(self.__get_file_path("fan_speed_min"))
                if self.plat_common.is_valid_value(value):
                    return int(value)
            except Exception as error:
                self.plat_common.log_error("Get psu fan max speed error:{}".format(str(error)))
        return None

    def get_fan_target_speed(self):
        """
        Retrieves the target (expected) speed of the fan

        Returns:
            An integer, the percentage of full fan speed, in the range 0 (off)
                 to 100 (full speed)
        """
        if not self.get_presence():
            return None

        if self.method in [CommonCfg.BY_RESTFUL, CommonCfg.BY_CACHE]:
            if self.method == CommonCfg.BY_RESTFUL:
                status_info = self.plat_common.get_psu_status_by_restful(self.index)
            else:
                status_info = self.plat_common.get_psu_info_by_cache(self.index)
            try:
                if status_info is not None and \
                    "FanSpeed" in status_info.keys() and \
                    self.plat_common.is_float(status_info.get("FanSpeed").get("Value")) and \
                    self.plat_common.is_valid_value(status_info.get("FanSpeed").get("Value")) and \
                    self.plat_common.is_float(status_info.get("FanSpeed").get("Max")) and \
                    self.plat_common.is_valid_value(status_info.get("FanSpeed").get("Max")):
                    speed = int(float(status_info.get("FanSpeed").get("Value")))
                    max_speed = int(float(status_info.get("FanSpeed").get("Max")))
                    return float(speed) * 100 / max_speed
            except Exception as error:
                self.plat_common.log_error("Get psu fan target speed error:{}".format(str(error)))
        else:
            try:
                value = self.plat_common.read_file(self.__get_file_path("fan_ratio"))
                if self.plat_common.is_valid_value(value):
                    return int(value)
            except Exception as error:
                self.plat_common.log_error("Get psu fan target speed error:{}".format(str(error)))
        return None

    def get_fan_speed_tolerance(self):
        """
        Retrieves the speed tolerance of the fan

        Returns:
            An integer, the percentage of variance from target speed which is
                 considered tolerable
        """
        if self.method == CommonCfg.BY_SYSFS:
            try:
                value = self.plat_common.read_file(self.__get_file_path("fan_tolerance"))
                if self.plat_common.is_valid_value(value):
                    return int(value)
            except Exception as error:
                self.plat_common.log_error("Get psu fan tolerance error:{}".format(str(error)))

        return CommonCfg.FAN_DEFAULT_TOLERANCE

    def get_fan_ratio(self):
        """
        Gets the PSU fan speed

        Returns:
            A int, psu fan pwm (0-100)
        """
        return self.get_fan_target_speed()

    def set_fan_speed(self, speed):
        """
        Sets the PSU fan speed

        Args:
            speed: An integer, the percentage of full fan speed to set fan to,
                   in the range 0 (off) to 100 (full speed)
        Returns:
            A boolean, True if speed is set successfully, False if not
        """
        if not self.get_presence():
            return False

        if speed > 100 and speed < 0:
            return False

        if self.method in [CommonCfg.BY_RESTFUL, CommonCfg.BY_CACHE]:
            return self.plat_common.set_psu_fan_speed_by_restful(self.index, speed)

        fan_path = self.__get_file_path("fan_ratio")
        return self.plat_common.write_file(fan_path, speed)

    def get_change_event(self):
        """
        Returns a nested dictionary containing psu devices which have experienced a change

        Returns:
            -----------------------------------------------------------------
            device   |     device_id       |  device_event  |  annotate
            -----------------------------------------------------------------
            'psu'          '<psu number>'     '0'              Psu removed
                                              '1'              Psu inserted
                                              '2'              Psu OK
                                              '3'              Psu power loss
                                              '4'              Psu fan error
                                              '5'              Psu voltage error
                                              '6'              Psu current error
                                              '7'              Psu power error
                                              '8'              Psu temperature error
        """
        new_presence = self.get_presence()
        if self._old_presence != new_presence:
            self._old_presence = new_presence
            if new_presence:
                self.plat_common.log_notice('PSU{} is present'.format(self.index))
                return (True, {"psu": {self.index - 1 : "1"}})
            else:
                self.plat_common.log_notice('PSU{} is absent'.format(self.index))
                return (True, {"psu": {self.index - 1 : "0"}})

        return (False, {"psu": {}})

    def power_cycle(self):
        """
        Control the PSU to power cycle

        Returns:
            A boolean
        """
        if not self.get_presence():
            return False

        if self.method == CommonCfg.BY_SYSFS:
            return self.plat_common.write_file(self.__get_file_path("power_cycle"), 0)

        return False
