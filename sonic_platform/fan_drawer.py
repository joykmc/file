# -*- coding: UTF-8 -*-

"""
Module contains an implementation of SONiC Platform Base API and
provides the fandrawer information which are available in the platform
"""

try:
    import os.path
    from sonic_platform_base.fan_drawer_base import FanDrawerBase
    from sonic_platform.fan import Fan
    from sonic_platform.plat_common import PlatCommon
    from sonic_platform.plat_common import CommonCfg
    from vendor_sonic_platform import hooks
except ImportError as e:
    raise ImportError(str(e) + "- required module not found") from e


class FanDrawer(FanDrawerBase):
    def __init__(self, index, rotor_num, method='sysfs'):
        """
        Fan drawer initial

        Args:
            index: int, start from 0
            rotor_num: int, the motor number of fandrawer
            method: str, eg. 'sysfs', 'restful', 'cache'
        """
        FanDrawerBase.__init__(self)
        self.plat_common = PlatCommon(debug=CommonCfg.DEBUG)
        self.index = index + 1   # fan drawer index start from 1
        self.sysfs_path = None
        self.method = method
        self._old_presence = False
        if self.method == CommonCfg.BY_SYSFS:
            self.sysfs_path = os.path.join(CommonCfg.S3IP_FAN_PATH, "fan{}".format(self.index))
        for motor_index in range(0, rotor_num):
            fan = Fan(motor_index, self, False, method)
            self._fan_list.append(fan)

    def __get_file_path(self, file_name):
        if self.sysfs_path is not None:
            return os.path.join(self.sysfs_path, file_name)
        return None

    def get_sysfs_path(self):
        """
        Get fan sysfs path
        Returns:
            string
        """
        return self.sysfs_path

    def get_index(self):
        """
        Get fan index
        Returns:
            int
        """
        return self.index

    def set_status_led(self, color):
        """
        Sets the state of the fan drawer status LED

        Args:
            color: A string representing the color with which to set the
                   fan drawer status LED

        Returns:
            bool: True if status LED state is set successfully, False if not
        """
        # thermalctld will call this function to set fan led,
        # it will conflict with ledctrl/fanctrl.
        # so, api just do nothing;
        return False

    def get_status_led(self):
        """
        Gets the state of the fan drawer LED

        Returns:
            A string, one of the predefined STATUS_LED_COLOR_* strings above
        """
        if not self.get_presence():
            return CommonCfg.NULL_VALUE

        if self.method == CommonCfg.BY_RESTFUL:
            return self.plat_common.get_fantray_led_by_restful(self.index)
        if self.method == CommonCfg.BY_CACHE:
            return self.plat_common.get_fantray_led_by_cache(self.index)

        led_value = self.plat_common.read_file(self.__get_file_path("led_status"))
        if not self.plat_common.is_valid_value(led_value):
            return CommonCfg.NULL_VALUE

        try:
            for color, value in CommonCfg.COLOR_MAP.items():
                if value == int(led_value):
                    return color
        except Exception as error:
            self.plat_common.log_error("Get fan status led fail:{}".format(str(error)))

        self.plat_common.log_info("Got unsupported fan led status:{}!".format(led_value))
        return CommonCfg.NULL_VALUE

    def get_maximum_consumed_power(self):
        """
        Retrives the maximum power drawn by Fan Drawer

        Returns:
            A float, with value of the maximum consumable power of the
            component.
        """
        if not self.get_presence():
            return None

        if hasattr(hooks, "get_fan_maximum_consumed_power"):
            return hooks.get_fan_maximum_consumed_power()
        return None

    ##############################################################
    ###################### Device methods ########################
    ##############################################################

    def get_name(self):
        """
        Retrieves the name of the device
        Returns:
            string: The name of the device
        """
        # if self.method == CommonCfg.BY_SYSFS:
        #     return self.plat_common.read_file(self.__get_file_path("alias"))
        return "FanDrawer{}".format(self.index)

    def get_presence(self):
        """
        Retrieves the presence of the device
        Returns:
            bool: True if device is present, False if not
        """
        if self.method == CommonCfg.BY_RESTFUL:
            return self.plat_common.get_fantray_presence_by_restful(self.index)
        if self.method == CommonCfg.BY_CACHE:
            return self.plat_common.get_fantray_presence_by_cache(self.index)

        value = self.plat_common.read_file(self.__get_file_path("present"))
        if value == "1":
            return True

        return False

    def get_model(self):
        """
        Retrieves the model number (or part number) of the device
        Returns:
            string: Model/part number of device
        """
        if not self.get_presence():
            return CommonCfg.NULL_VALUE

        if self.method == CommonCfg.BY_RESTFUL:
            mfr_info = self.plat_common.get_fantray_mfr_by_restful(self.index)
            if mfr_info:
                return mfr_info.get("PN")

        if self.method == CommonCfg.BY_CACHE:
            mfr_info = self.plat_common.get_fantray_mfr_by_cache(self.index)
            if mfr_info:
                return mfr_info.get("PN")

        if self.method == CommonCfg.BY_SYSFS:
            model = self.plat_common.read_file(self.__get_file_path("part_number"))
            if self.plat_common.is_valid_value(model):
                return model

        return CommonCfg.NULL_VALUE

    def get_serial(self):
        """
        Retrieves the serial number of the device
        Returns:
            string: Serial number of device
        """
        if not self.get_presence():
            return CommonCfg.NULL_VALUE

        if self.method == CommonCfg.BY_RESTFUL:
            mfr_info = self.plat_common.get_fantray_mfr_by_restful(self.index)
            if mfr_info:
                return mfr_info.get("SN")

        if self.method == CommonCfg.BY_CACHE:
            mfr_info = self.plat_common.get_fantray_mfr_by_cache(self.index)
            if mfr_info:
                return mfr_info.get("SN")

        if self.method == CommonCfg.BY_SYSFS:
            serial = self.plat_common.read_file(self.__get_file_path("serial_number"))
            if self.plat_common.is_valid_value(serial):
                return serial

        return CommonCfg.NULL_VALUE

    def get_vendor(self):
        """
        Retrieves vendor of fan

        Returns:
            A string, fan vendor
        """
        if not self.get_presence():
            return CommonCfg.NULL_VALUE

        if self.method == CommonCfg.BY_RESTFUL:
            mfr_info = self.plat_common.get_fantray_mfr_by_restful(self.index)
            if mfr_info:
                return mfr_info.get("Vendor")

        if self.method == CommonCfg.BY_CACHE:
            mfr_info = self.plat_common.get_fantray_mfr_by_cache(self.index)
            if mfr_info:
                return mfr_info.get("Vendor")

        if self.method == CommonCfg.BY_SYSFS:
            vendor = self.plat_common.read_file(self.__get_file_path("vendor"))
            if self.plat_common.is_valid_value(vendor):
                return vendor

        return CommonCfg.NULL_VALUE

    def get_hardware_version(self):
        """
        Retrieves hardware version of fan

        Returns:
            A string, fan hardware version
        """
        if not self.get_presence():
            return CommonCfg.NULL_VALUE

        if self.method in [CommonCfg.BY_CACHE, CommonCfg.BY_RESTFUL]:
            return CommonCfg.NULL_VALUE
        else:
            hw_version = self.plat_common.read_file(self.__get_file_path("hardware_version"))
            if self.plat_common.is_valid_value(hw_version):
                return hw_version
        return CommonCfg.NULL_VALUE

    def get_revision(self):
        """
        Retrieves the hardware revision of the device

        Returns:
            string: Revision value of device
        """
        return self.get_hardware_version()

    def get_status(self):
        """
        Retrieves the operational status of the device
        Returns:
            A boolean value, True if device is operating properly, False if not
        """
        if not self.get_presence():
            return False

        for fan in self.get_all_fans():
            if not fan.get_status():
                return False

        return True

    def get_direction(self):
        """
        Retrieves the direction of fan

        Returns:
            A string, either "F2B" or "B2F" depending on fan direction
        """
        if not self.get_presence():
            return CommonCfg.NULL_VALUE

        if self.method in [CommonCfg.BY_RESTFUL, CommonCfg.BY_CACHE]:
            if self.method == CommonCfg.BY_RESTFUL:
                mfr_info = self.plat_common.get_fantray_mfr_by_restful(self.index)
            else:
                mfr_info = self.plat_common.get_fantray_mfr_by_cache(self.index)
            if mfr_info and "AirFlow" in mfr_info.keys():
                return mfr_info.get("AirFlow")
        else:
            try:
                value = self.plat_common.read_file(self.__get_file_path("direction"))
                if self.plat_common.is_valid_value(value):
                    return CommonCfg.FAN_DIRECTION_MAP[int(value)]
            except Exception as error:
                self.plat_common.log_error("Get fan direction error:{}".format(str(error)))

        return CommonCfg.NULL_VALUE

    def get_target_speed(self):
        """
        Retrieves the target (expected) speed of the fan-drawer

        Returns:
            An integer, the percentage of full fan speed, in the range 0 (off)
                 to 100 (full speed)
        """
        if not self.get_presence():
            return None

        target_speed = None
        try:
            if self.method in [CommonCfg.BY_RESTFUL, CommonCfg.BY_CACHE]:
                if self.method == CommonCfg.BY_RESTFUL:
                    speed_info = self.plat_common.get_fantray_speed_info_by_restful(self.index)
                else:
                    speed_info = self.plat_common.get_fantray_mfr_by_cache(self.index)
                if speed_info is not None and \
                    self.plat_common.is_float(speed_info.get("pwm")) and \
                    self.plat_common.is_valid_value(speed_info.get("pwm")):
                    target_speed = int(float(speed_info.get("pwm")))
            else:
                raw_value = self.plat_common.read_file(self.__get_file_path("ratio"))
                if self.plat_common.is_valid_value(raw_value):
                    target_speed = int(raw_value)
        except Exception as error:
            self.plat_common.log_error("Get fan target speed error:{}".format(str(error)))

        return target_speed

    def set_speed(self, speed):
        """
        Sets the fan speed

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
            return self.plat_common.set_fantray_speed_by_restful(self.index, speed)

        ratio_path = self.__get_file_path("ratio")
        return self.plat_common.write_file(ratio_path, speed)

    def get_position_in_parent(self):
        """
        Retrieves 1-based relative physical position in parent device
        Returns:
            integer: The 1-based relative physical position in parent device
        """
        return self.index

    def is_replaceable(self):
        """
        Indicate whether this device is replaceable.
        Returns:
            bool: True if it is replaceable.
        """
        return True

    def get_change_event(self):
        """
        Returns a nested dictionary containing fan devices which have experienced a change

        Returns:
            -----------------------------------------------------------------
            device   |     device_id       |  device_event  |  annotate
            -----------------------------------------------------------------
            'fan'          '<fan number>'     '0'              Fan removed
                                              '1'              Fan inserted
                                              '2'              Fan OK
                                              '3'              Fan speed low
                                              '4'              Fan speed high
        """
        new_presence = self.get_presence()
        if self._old_presence != new_presence:
            self._old_presence = new_presence
            if new_presence:
                self.plat_common.log_notice('FanDrawer{} is present'.format(self.index))
                return (True, {"fan": {self.index - 1 : "1"}})
            else:
                self.plat_common.log_notice('FanDrawer{} is absent'.format(self.index))
                return (True, {"fan": {self.index - 1 : "0"}})

        return (False, {"fan": {}})

    def set_status_led_for_fanctl(self, color):
        """
        Sets the state of the fan drawer status LED

        Args:
            color: A string representing the color with which to set the
                   fan drawer status LED

        Returns:
            bool: True if status LED state is set successfully, False if not
        """
        if not self.get_presence():
            return False

        if color not in CommonCfg.COLOR_MAP.keys():
            return False

        if self.method in [CommonCfg.BY_RESTFUL, CommonCfg.BY_CACHE]:
            return self.plat_common.set_fantray_led_by_restful(self.index, color)

        value = CommonCfg.COLOR_MAP[color]
        return self.plat_common.write_file(self.__get_file_path("led_status"), value)
