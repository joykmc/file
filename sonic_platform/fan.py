# -*- coding: UTF-8 -*-

"""
Module contains an implementation of SONiC Platform Base API and
provides the fan information which are available in the platform
"""

try:
    import os.path
    from sonic_platform.plat_common import PlatCommon
    from sonic_platform.plat_common import CommonCfg
    from sonic_platform_base.fan_base import FanBase
except ImportError as e:
    raise ImportError(str(e) + "- required module not found") from e

RETRY_CNT = 8
class Fan(FanBase):
    """Platform-specific Fan class"""
    def __init__(self, index, parent=None, is_psu_fan=False, method="sysfs"):
        """
        Fan initial

        Args:
            index: int, start from 0
            parent: object, the parent of fan object
            is_psu_fan: boolean, True for psu fan
            method: str, eg. 'sysfs', 'restful', 'cache'
        """
        FanBase.__init__(self)
        self.index = index + 1       # fan index start from 1
        self.is_psu_fan = is_psu_fan
        self.method = method
        self.sysfs_path = None
        self.plat_common = PlatCommon(debug=CommonCfg.DEBUG)
        self.parent = parent
        if self.method == CommonCfg.BY_SYSFS:
            self.__init_sysfs_path()

    def __init_sysfs_path(self):
        if self.parent is not None:
            if self.is_psu_fan is False:
                self.sysfs_path = os.path.join(
                    CommonCfg.S3IP_FAN_PATH,
                    "fan{}".format(self.parent.get_index()),
                    "motor{}".format(self.index))
            else:
                self.sysfs_path = os.path.join(
                    CommonCfg.S3IP_PSU_PATH,
                    "psu{}".format(self.parent.get_index()))

    def __get_file_path(self, file_name):
        if self.sysfs_path is not None:
            return os.path.join(self.sysfs_path, file_name)
        return None

    def get_name(self):
        """
        Retrieves the name of the device

        Returns:
            string: The name of the device
        """
        return "{}_Fan{}".format(self.parent.get_name(), self.index)

    def get_presence(self):
        """
        Obtain the presence of the current fan
        Returns:
            boolean:True if the current fan is present, False if not
        """
        return self.parent.get_presence()

    def get_model(self):
        """
        Retrieves the model number (or part number) of the device

        Returns:
            string: Model/part number of device
        """
        if not self.get_presence():
            return CommonCfg.NULL_VALUE

        return self.parent.get_model()

    def get_serial(self):
        """
        Get the serial number of current fan
        Returns:
            string: serial number
        """
        if not self.get_presence():
            return CommonCfg.NULL_VALUE

        return self.parent.get_serial()

    def get_vendor(self):
        """
        Retrieves vendor of fan

        Returns:
        	A string, fan vendor
        """
        if not self.get_presence():
            return CommonCfg.NULL_VALUE

        return self.parent.get_vendor()

    def get_hardware_version(self):
        """
        Retrieves hardware version of fan

        Returns:
        	A string, fan hardware version
        """
        if not self.get_presence():
            return CommonCfg.NULL_VALUE

        return self.parent.get_hardware_version()

    def get_revision(self):
        """
        Retrieves the hardware revision of the device

        Returns:
            string: Revision value of device
        """
        if not self.get_presence():
            return CommonCfg.NULL_VALUE

        return self.parent.get_revision()

    def get_status(self):
        """
        Get satus of the current fan
        Returns:
            boolean:True if the fan is ok, False if not
        """
        if not self.get_presence():
            return False

        if self.is_psu_fan:
            speed_rpm = self.get_speed_rpm()
            if speed_rpm is not None:
                return self.get_speed_rpm() > 0
            return False

        now_speed = self.get_speed()
        if now_speed == 0 or now_speed is None:
            return False

        speed_tolerance = self.get_speed_tolerance()
        target_speed = self.get_target_speed()
        if speed_tolerance is None or target_speed is None:
            return False
        if now_speed < (target_speed * (1 - float(speed_tolerance) / 100)):
            return False
        if now_speed > (target_speed * (1 + float(speed_tolerance) / 100)):
            return False
        return True

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

    def get_direction(self):
        """
        Retrieves the direction of fan

        Returns:
            A string, either FAN_DIRECTION_INTAKE or FAN_DIRECTION_EXHAUST
            depending on fan direction
        """
        if self.is_psu_fan:
            raw_direction = self.parent.get_fan_direction()
        else:
            raw_direction = self.parent.get_direction()

        if raw_direction == CommonCfg.FAN_DIRECTION_F2B_STR:
            return self.FAN_DIRECTION_INTAKE
        if raw_direction == CommonCfg.FAN_DIRECTION_B2F_STR:
            return self.FAN_DIRECTION_EXHAUST
        return self.FAN_DIRECTION_NOT_APPLICABLE

    def get_speed(self):
        """
        Retrieves the speed of fan as a percentage of full speed

        Returns:
            An integer, the percentage of full fan speed, in the range 0 (off)
                 to 100 (full speed)
        """
        if not self.get_presence():
            return None

        try:
            speed_rpm = self.get_speed_rpm()
            speed_max = self.get_speed_rpm_max()
            if speed_max == 0 or speed_max is None or \
                speed_rpm == 0 or speed_rpm is None:
                return 0

            pwm = speed_rpm * 100 / speed_max
            if pwm > 100:
                pwm = 100
            elif pwm < 0:
                pwm = 0

            return int(round(pwm))
        except Exception as error:
            self.plat_common.log_error("Get fan speed error:{}".format(str(error)))

        return None

    def get_target_speed(self):
        """
        Retrieves the target (expected) speed of the fan

        Returns:
            An integer, the percentage of full fan speed, in the range 0 (off)
                 to 100 (full speed)
        """
        if not self.get_presence():
            return None

        if self.is_psu_fan:
            return self.parent.get_fan_target_speed()

        return self.parent.get_target_speed()

    def get_speed_tolerance(self):
        """
        Retrieves the speed tolerance of the fan

        Returns:
            An integer, the percentage of variance from target speed which is
                 considered tolerable
        """
        try:
            if self.is_psu_fan:
                return self.parent.get_fan_speed_tolerance()

            if self.method in [CommonCfg.BY_RESTFUL, CommonCfg.BY_CACHE]:
                speed_info = self.plat_common.get_fantray_speed_info_by_cache(self.parent.get_index())
                if speed_info is not None and \
                    self.plat_common.is_float(speed_info.get("fixup")) and \
                    self.plat_common.is_valid_value(speed_info.get("fixup")):
                    fixup = float(speed_info.get("fixup"))
                    return int(((fixup - 1) * 100))
            else:
                tolerance = self.plat_common.read_file(self.__get_file_path("speed_tolerance_ratio"))
                if self.plat_common.is_valid_value(tolerance):
                    return int(tolerance)
        except Exception as error:
            self.plat_common.log_error("Get fan speed tolerance error:{}".format(str(error)))

        return CommonCfg.FAN_DEFAULT_TOLERANCE

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

        try:
            if self.is_psu_fan:
                return self.parent.set_fan_speed(speed)

            if self.method == CommonCfg.BY_RESTFUL:
                return False
            if self.method == CommonCfg.BY_CACHE:
                return False
            return self.parent.set_speed(speed)
        except Exception as error:
            self.plat_common.log_error("Set fan speed error:{}".format(str(error)))

        return False

    def set_status_led(self, color):
        """
        Sets the state of the fan module status LED

        Args:
            color: A string representing the color with which to set the
                   fan module status LED

        Returns:
            bool: True if status LED state is set successfully, False if not
        """
        if not self.get_presence():
            return False

        return self.parent.set_status_led(color)

    def get_status_led(self):
        """
        Gets the state of the fan status LED

        Returns:
            A string, one of the predefined STATUS_LED_COLOR_* strings above
        """
        if not self.get_presence():
            return CommonCfg.NULL_VALUE

        return self.parent.get_status_led()

    def get_speed_rpm(self):
        """
        Get real-time speed(revolution per min; RPM) of the current fan motor
        Returns:
            int
        """
        if not self.get_presence():
            return None

        speed = None
        try:
            if self.is_psu_fan:
                return self.parent.get_fan_speed_rpm()

            if self.method in [CommonCfg.BY_RESTFUL, CommonCfg.BY_CACHE]:
                rotor = "Rotor{}".format(self.index)
                if self.method == CommonCfg.BY_RESTFUL:
                    speed_info = self.plat_common.get_fantray_speed_info_by_restful(self.parent.get_index())
                else:
                    speed_info = self.plat_common.get_fantray_speed_info_by_cache(self.parent.get_index())
                if speed_info is not None and \
                    rotor in speed_info.keys() and \
                    self.plat_common.is_float(speed_info.get(rotor).get("Speed")) and \
                    self.plat_common.is_valid_value(speed_info.get(rotor).get("Speed")):
                    speed = int(float(speed_info.get(rotor).get("Speed")))
            else:
                raw_value = self.plat_common.read_file(self.__get_file_path("speed"))
                if self.plat_common.is_valid_value(raw_value):
                    speed = int(raw_value)
        except Exception as error:
            self.plat_common.log_error("Get fan speed rpm error:{}".format(str(error)))

        return speed

    def get_speed_rpm_max(self):
        """
        Get the max speed(PRM) of the current fan motor
        Returns:
            int
        """
        speed_max = None
        try:
            if self.is_psu_fan:
                return self.parent.get_fan_speed_rpm_max()

            if self.method in [CommonCfg.BY_RESTFUL, CommonCfg.BY_CACHE]:
                rotor = "Rotor{}".format(self.index)
                if self.method == CommonCfg.BY_RESTFUL:
                    speed_info = self.plat_common.get_fantray_speed_info_by_restful(self.parent.get_index())
                else:
                    speed_info = self.plat_common.get_fantray_speed_info_by_cache(self.parent.get_index())
                if speed_info is not None and \
                    rotor in speed_info.keys() and \
                    self.plat_common.is_float(speed_info.get(rotor).get("SpeedMax")) and \
                    self.plat_common.is_valid_value(speed_info.get(rotor).get("SpeedMax")):
                    speed_max = int(float((speed_info.get(rotor).get("SpeedMax"))))
            else:
                raw_value = self.plat_common.read_file(self.__get_file_path("speed_max"))
                if self.plat_common.is_valid_value(raw_value):
                    speed_max = int(raw_value)
        except Exception as error:
            self.plat_common.log_error("Get fan max speed rpm error:{}".format(str(error)))

        return speed_max

    def get_speed_max(self):
        """
        Get the max speed(PRM) of the current fan motor, For UXOS
        Returns:
            int
        """
        return self.get_speed_rpm_max()

    def get_speed_rpm_min(self):
        """
        Get the min speed(PRM) of the current fan motor
        Returns:
            int
        """
        speed_min = None
        try:
            if self.is_psu_fan:
                return self.parent.get_fan_speed_rpm_min()

            if self.method in [CommonCfg.BY_RESTFUL, CommonCfg.BY_CACHE]:
                rotor = "Rotor{}".format(self.index)
                if self.method == CommonCfg.BY_RESTFUL:
                    speed_info = self.plat_common.get_fantray_speed_info_by_restful(self.parent.get_index())
                else:
                    speed_info = self.plat_common.get_fantray_speed_info_by_cache(self.parent.get_index())
                if speed_info is not None and \
                    rotor in speed_info.keys() and \
                    self.plat_common.is_float(speed_info.get(rotor).get("SpeedMin")) and \
                    self.plat_common.is_valid_value(speed_info.get(rotor).get("SpeedMin")):
                    speed_min = int(float((speed_info.get(rotor).get("SpeedMin"))))
            else:
                raw_value = self.plat_common.read_file(self.__get_file_path("speed_min"))
                if self.plat_common.is_valid_value(raw_value):
                    speed_min = int(raw_value)
        except Exception as error:
            self.plat_common.log_error("Get fan min speed rpm error:{}".format(str(error)))

        return speed_min

    def get_ratio(self):
        """
        Get the ratio of the current fan motor
        Returns:
            int, 0-100
        """
        return self.get_speed()

    def set_status_led_for_fanctl(self, color):
        """
        Sets the state of the fan module status LED

        Args:
            color: A string representing the color with which to set the
                   fan module status LED
        Returns:
            bool: True if status LED state is set successfully, False if not
        """
        if not self.get_presence():
            return False

        if self.is_psu_fan:
            return False

        return self.parent.set_status_led_for_fanctl(color)

    def get_retry_count(self):
        """
        Get the retry times to get fan information
        Returns:
            int: set retry times as more than 6

        """
        return RETRY_CNT
