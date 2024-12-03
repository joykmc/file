# -*- coding: UTF-8 -*-

"""
Module contains an implementation of SONiC Platform Base API and
provides the current of DCDC/VR information which are available in the platform
"""

try:
    import os.path
    from sonic_platform.plat_common import PlatCommon
    from sonic_platform.plat_common import CommonCfg
except ImportError as e:
    raise ImportError(str(e) + "- required module not found") from e


class Current(object):
    """Platform-specific Current class"""

    def __init__(self, name, slot_index, current_index, method='sysfs'):
        """
        Current initial

        Args:
            name: str, sensor name
            slot_index: int, 0 on chassis, 1-base on slot;
            current_index: int, 1-base
            method: str, eg. 'sysfs', 'restful', 'cache'
        """
        self.name = name
        self.method = method
        self.sysfs_path = None
        self.plat_common = PlatCommon(debug=CommonCfg.DEBUG)
        if self.method == CommonCfg.BY_SYSFS:
            self.__init_sysfs_path(slot_index, current_index)

    def __init_sysfs_path(self, slot_index, current_index):
        ''' slot is 0 indicates thermal on chassis '''
        if slot_index == 0:
            self.sysfs_path = os.path.join(CommonCfg.S3IP_CURR_PATH, "curr{}".format(current_index))
        else:
            self.sysfs_path = os.path.join(CommonCfg.S3IP_SLOT_PATH,
                                           "slot{}".format(slot_index),
                                           CommonCfg.S3IP_CURR_DIR + str(current_index))

    def __get_file_path(self, file_name):
        if self.sysfs_path is not None:
            return os.path.join(self.sysfs_path, file_name)
        return None

    def get_name(self):
        """
        Retrieves the name of the current device

        Returns:
            string: The name of the current device
        """
        # if self.by_restful is False:
        #     return self.plat_common.read_file(self.__get_file_path("alias"))
        return self.name

    def get_model(self):
        """
        Retrieves the model number (or part number) of the device

        Returns:
            string: Model/part number of device
        """
        if self.method == CommonCfg.BY_SYSFS:
            model = self.plat_common.read_file(self.__get_file_path("type"))
            if self.plat_common.is_valid_value(model):
                return model
        return "Current"

    def get_current(self):
        """
        Retrieves current value reading from DCDC or VR

        Returns:
            A float number of current value in A to nearest thousandth
            of one current, e.g. 3.325
        """
        if self.method == CommonCfg.BY_SYSFS:
            return self.__get_current_by_sysfs()
        if self.method == CommonCfg.BY_RESTFUL:
            return self.__get_current_by_restful()

        return self.__get_current_by_cache()

    def get_high_threshold(self):
        """
        Retrieves the high threshold value of current

        Returns:
            A float number, the high threshold value of current in A
            up to nearest thousandth of one current, e.g. 3.325
        """
        if self.method == CommonCfg.BY_SYSFS:
            return self.__get_high_threshold_by_sysfs()
        if self.method == CommonCfg.BY_RESTFUL:
            return self.__get_high_threshold_by_restful()

        return self.__get_high_threshold_by_cache()

    def get_low_threshold(self):
        """
        Retrieves the low threshold value of current
        Returns:
            A float number, the low threshold value of current in A
            up to nearest thousandth of one current, e.g. 3.125
        """
        if self.method == CommonCfg.BY_SYSFS:
            return self.__get_low_threshold_by_sysfs()
        if self.method == CommonCfg.BY_RESTFUL:
            return self.__get_low_threshold_by_restful()

        return self.__get_low_threshold_by_cache()

    def get_high_critical_threshold(self):
        """
        Retrieves the high critical threshold value of current

        Returns:
            A float number, the low threshold value of current in A
            up to nearest thousandth of one current, e.g. 30.125
        """
        if self.method == CommonCfg.BY_SYSFS:
            return self.__get_high_critical_threshold_by_sysfs()
        if self.method == CommonCfg.BY_RESTFUL:
            return self.__get_high_critical_threshold_by_restful()

        return self.__get_high_critical_threshold_by_cache()

    def get_low_critical_threshold(self):
        """
        Retrieves the low critical threshold value of current

        Returns:
            A float number, the low threshold value of current in A
            up to nearest thousandth of one current, e.g. 30.125
        """
        if self.method == CommonCfg.BY_SYSFS:
            return self.__get_low_critical_threshold_by_sysfs()
        if self.method == CommonCfg.BY_RESTFUL:
            return self.__get_low_critical_threshold_by_restful()

        return self.__get_low_critical_threshold_by_cache()

    def get_presence(self):
        """
        Retrieves the presence of the device

        Returns:
            bool: True if device is present, False if not
        """
        if self.get_current() is not None:
            return True
        return False

    def get_status(self):
        """
        Retrieves the operational status of the device

        Returns:
            A boolean value, True if device is operating properly, False if not
        """
        try:
            current_val = self.get_current()
            low_thd = self.get_low_threshold()
            high_thd = self.get_high_threshold()
            if current_val is not None and low_thd is not None and high_thd is not None:
                if low_thd <= current_val <= high_thd:
                    return True
        except Exception as err:
            self.plat_common.log_error("can't get current status:{}".format(str(err)))

        return False

    def __get_current_by_sysfs(self):
        """
        Retrieves current value reading from current

        Returns:
            A float number of current value in A up to nearest thousandth
            of one Current, e.g. 3.125
        """
        value = self.plat_common.read_file(self.__get_file_path("value"))
        if self.plat_common.is_valid_value(value):
            return round(float(value) / CommonCfg.CURRENT_FACTOR, 3)

        return None

    def __get_current_by_restful(self):
        """
        Retrieves current value reading from current

        Returns:
            A float number of current value in V up to nearest thousandth
            of one Current, e.g. 30125
        """
        try:
            sensor_info = self.plat_common.get_one_sensor_info_by_restful(self.get_name())
            if sensor_info is not None and \
                self.plat_common.is_float(sensor_info.get("Value")) and \
                self.plat_common.is_valid_value(sensor_info.get("Value")):
                value = sensor_info.get("Value")
                return round(float(value), 3)
        except Exception as error:
            self.plat_common.log_error("Get current error:{}".format(str(error)))

        return None

    def __get_current_by_cache(self):
        """
        Retrieves current value reading from current

        Returns:
            A float number of current value in V up to nearest thousandth
            of one Current, e.g. 30125
        """
        try:
            sensor_info = self.plat_common.get_one_sensor_info_by_cache(self.get_name())
            if sensor_info is not None and \
                self.plat_common.is_float(sensor_info.get("Value")) and \
                self.plat_common.is_valid_value(sensor_info.get("Value")):
                value = sensor_info.get("Value")
                return round(float(value), 3)
        except Exception as error:
            self.plat_common.log_error("Get current error:{}".format(str(error)))

        return None

    def __get_high_threshold_by_sysfs(self):
        """
        Retrieves the high threshold value of current

        Returns:
            A float number, the high threshold value of current in A
            up to nearest thousandth of one Volts, e.g. 30125
        """
        value = self.plat_common.read_file(self.__get_file_path("max"))
        if self.plat_common.is_valid_value(value):
            return round(float(value) / CommonCfg.CURRENT_FACTOR, 3)

        return None

    def __get_high_threshold_by_restful(self):
        """
        Retrieves the high threshold value of current

        Returns:
            A float number, the high threshold value of current in A
            up to nearest thousandth of one Volts, e.g. 30125
        """
        try:
            sensor_info = self.plat_common.get_one_sensor_info_by_restful(self.get_name())
            if sensor_info is not None and \
                self.plat_common.is_float(sensor_info.get("Warning_High")) and \
                self.plat_common.is_valid_value(sensor_info.get("Warning_High")):
                value = sensor_info.get("Warning_High")
                return round(float(value), 3)
        except Exception as error:
            self.plat_common.log_error("Get current high threshold error:{}".format(str(error)))

        return None

    def __get_high_threshold_by_cache(self):
        """
        Retrieves the high threshold value of current

        Returns:
            A float number, the high threshold value of current in A
            up to nearest thousandth of one Volts, e.g. 30125
        """
        try:
            sensor_info = self.plat_common.get_one_sensor_info_by_cache(self.get_name())
            if sensor_info is not None and \
                self.plat_common.is_float(sensor_info.get("Warning_High")) and \
                self.plat_common.is_valid_value(sensor_info.get("Warning_High")):
                value = sensor_info.get("Warning_High")
                return round(float(value), 3)
        except Exception as error:
            self.plat_common.log_error("Get current high threshold error:{}".format(str(error)))

        return None

    def __get_low_threshold_by_sysfs(self):
        """
        Retrieves the low threshold value of current
        Returns:
            A float number, the low threshold value of current in A
            up to nearest thousandth of one Volts, e.g. 3.125
        """
        value = self.plat_common.read_file(self.__get_file_path("min"))
        if self.plat_common.is_valid_value(value):
            return round(float(value) / CommonCfg.CURRENT_FACTOR, 3)

        return None

    def __get_low_threshold_by_restful(self):
        """
        Retrieves the low threshold value of current
        Returns:
            A float number, the low threshold value of current in A
            up to nearest thousandth of one Volts, e.g. 3.125
        """
        try:
            sensor_info = self.plat_common.get_one_sensor_info_by_restful(self.get_name())
            if sensor_info is not None and \
                self.plat_common.is_float(sensor_info.get("Warning_Low")) and \
                self.plat_common.is_valid_value(sensor_info.get("Warning_Low")):
                value = sensor_info.get("Warning_Low")
                return round(float(value), 3)
        except Exception as error:
            self.plat_common.log_error("Get current low threshold error:{}".format(str(error)))

        return None

    def __get_low_threshold_by_cache(self):
        """
        Retrieves the low threshold value of current
        Returns:
            A float number, the low threshold value of current in A
            up to nearest thousandth of one Volts, e.g. 3.125
        """
        try:
            sensor_info = self.plat_common.get_one_sensor_info_by_cache(self.get_name())
            if sensor_info is not None and \
                self.plat_common.is_float(sensor_info.get("Warning_Low")) and \
                self.plat_common.is_valid_value(sensor_info.get("Warning_Low")):
                value = sensor_info.get("Warning_Low")
                return round(float(value), 3)
        except Exception as error:
            self.plat_common.log_error("Get current low threshold error:{}".format(str(error)))

        return None

    def __get_high_critical_threshold_by_sysfs(self):
        """
        Retrieves the high critical threshold value of current

        Returns:
            A float number, the high critical threshold value of current in A
            up to nearest thousandth of one degree current, e.g. 30.125
        """
        value = self.plat_common.read_file(self.__get_file_path("critical_max"))
        if self.plat_common.is_valid_value(value):
            return round(float(value) / CommonCfg.CURRENT_FACTOR, 3)

        return None

    def __get_high_critical_threshold_by_restful(self):
        """
        Retrieves the high critical threshold value of current

        Returns:
            A float number, the high critical threshold value of current in A
            up to nearest thousandth of one degree current, e.g. 30.125
        """
        try:
            sensor_info = self.plat_common.get_one_sensor_info_by_restful(self.get_name())
            if sensor_info is not None and \
                self.plat_common.is_float(sensor_info.get("Critical_High")) and \
                self.plat_common.is_valid_value(sensor_info.get("Critical_High")):
                value = sensor_info.get("Critical_High")
                return round(float(value), 3)
        except Exception as error:
            self.plat_common.log_error("Get current critical high threshold error:{}".format(str(error)))

        return None

    def __get_high_critical_threshold_by_cache(self):
        """
        Retrieves the high critical threshold value of current

        Returns:
            A float number, the high critical threshold value of current in A
            up to nearest thousandth of one degree current, e.g. 30.125
        """
        try:
            sensor_info = self.plat_common.get_one_sensor_info_by_cache(self.get_name())
            if sensor_info is not None and \
                self.plat_common.is_float(sensor_info.get("Critical_High")) and \
                self.plat_common.is_valid_value(sensor_info.get("Critical_High")):
                value = sensor_info.get("Critical_High")
                return round(float(value), 3)
        except Exception as error:
            self.plat_common.log_error("Get current critical high threshold error:{}".format(str(error)))

        return None

    def __get_low_critical_threshold_by_sysfs(self):
        """
        Retrieves the low critical threshold value of current

        Returns:
            A float number, the low critical threshold value of current in A
            up to nearest thousandth of one degree current, e.g. 30.125
        """
        value = self.plat_common.read_file(self.__get_file_path("critical_min"))
        if self.plat_common.is_valid_value(value):
            return round(float(value) / CommonCfg.CURRENT_FACTOR, 3)

        return None

    def __get_low_critical_threshold_by_restful(self):
        """
        Retrieves the low critical threshold value of current

        Returns:
            A float number, the low critical threshold value of current in A
            up to nearest thousandth of one degree current, e.g. 30.125
        """
        try:
            sensor_info = self.plat_common.get_one_sensor_info_by_restful(self.get_name())
            if sensor_info is not None and \
                self.plat_common.is_float(sensor_info.get("Critical_Low")) and \
                self.plat_common.is_valid_value(sensor_info.get("Critical_Low")):
                value = sensor_info.get("Critical_Low")
                return round(float(value), 3)
        except Exception as error:
            self.plat_common.log_error("Get current critical low threshold error:{}".format(str(error)))

        return None

    def __get_low_critical_threshold_by_cache(self):
        """
        Retrieves the low critical threshold value of current

        Returns:
            A float number, the low critical threshold value of current in A
            up to nearest thousandth of one degree current, e.g. 30.125
        """
        try:
            sensor_info = self.plat_common.get_one_sensor_info_by_cache(self.get_name())
            if sensor_info is not None and \
                self.plat_common.is_float(sensor_info.get("Critical_Low")) and \
                self.plat_common.is_valid_value(sensor_info.get("Critical_Low")):
                value = sensor_info.get("Critical_Low")
                return round(float(value), 3)
        except Exception as error:
            self.plat_common.log_error("Get current critical low threshold error:{}".format(str(error)))

        return None
