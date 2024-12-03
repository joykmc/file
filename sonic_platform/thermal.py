# -*- coding: UTF-8 -*-

"""
Module contains an implementation of SONiC Platform Base API and
provides the thermal information which are available in the platform
"""

try:
    import os.path
    from sonic_platform_base.thermal_base import ThermalBase
    from sonic_platform.plat_common import PlatCommon
    from sonic_platform.plat_common import CommonCfg
    from vendor_sonic_platform import hooks
except ImportError as e:
    raise ImportError(str(e) + "- required module not found") from e


class Thermal(ThermalBase):
    """Platform-specific Thermal class"""

    def __init__(self, name, slot_index, thermal_index, method="sysfs"):
        """
        Thermal initial

        Args:
            name: str, sensor name
            slot_index: int, 0 on chassis, 1-base on slot;
            thermal_index: int, 1-base
            method: str, eg. 'sysfs', 'restful', 'cache'
        """
        ThermalBase.__init__(self)
        self.name = name
        self.index = thermal_index
        self.method = method
        self.sysfs_path = None
        self.temperature_list = []
        self.plat_common = PlatCommon(debug=CommonCfg.DEBUG)
        self._old_status = False
        if self.method == CommonCfg.BY_SYSFS:
            self.__init_sysfs_path(slot_index, thermal_index)

    def __init_sysfs_path(self, slot_index, thermal_index):
        ''' slot is 0 indicates thermal on chassis '''
        if slot_index == 0:
            self.sysfs_path = os.path.join(CommonCfg.S3IP_TEMP_PATH, "temp{}".format(thermal_index))
        else:
            self.sysfs_path = os.path.join(CommonCfg.S3IP_SLOT_PATH,
                                           "slot{}".format(slot_index),
                                           CommonCfg.S3IP_TEMP_DIR + str(thermal_index))

    def __get_file_path(self, file_name):
        if self.sysfs_path is not None:
            return os.path.join(self.sysfs_path, file_name)
        return None

    def get_name(self):
        """
        Retrieves the name of the thermal device

        Returns:
            string: The name of the thermal device
        """
        # if self.by_restful is False:
        #     return self.plat_common.read_file(self.__get_file_path("alias"))
        return self.name

    def get_temperature(self):
        """
        Retrieves current temperature reading from thermal

        Returns:
            A float number of current temperature in Celsius up to nearest thousandth
            of one degree Celsius, e.g. 30.125
        """
        try:
            if hasattr(hooks, "get_temperature"):
                value = hooks.get_temperature(self.name)
            else:
                if self.method == CommonCfg.BY_SYSFS:
                    value = self.__get_temperature_by_sysfs()
                elif self.method == CommonCfg.BY_RESTFUL:
                    value = self.__get_temperature_by_restful()
                else:
                    value = self.__get_temperature_by_cache()

            # temperature cache
            if len(self.temperature_list) >= 100:
                del self.temperature_list[0]
            if value is not None:
                self.temperature_list.append(value)

            return value
        except Exception as error:
            self.plat_common.log_error("get {} temperature fail:{}".format(self.name, str(error)))

        return None

    def get_high_threshold(self):
        """
        Retrieves the high threshold temperature of thermal

        Returns:
            A float number, the high threshold temperature of thermal in Celsius
            up to nearest thousandth of one degree Celsius, e.g. 30.125
        """
        if self.method == CommonCfg.BY_SYSFS:
            value = self.__get_high_threshold_by_sysfs()
        elif self.method == CommonCfg.BY_RESTFUL:
            value = self.__get_high_threshold_by_restful()
        else:
            value = self.__get_high_threshold_by_cache()
        return value

    def get_low_threshold(self):
        """
        Retrieves the low threshold temperature of thermal

        Returns:
            A float number, the low threshold temperature of thermal in Celsius
            up to nearest thousandth of one degree Celsius, e.g. 30.125
        """
        if self.method == CommonCfg.BY_SYSFS:
            value = self.__get_low_threshold_by_sysfs()
        elif self.method == CommonCfg.BY_RESTFUL:
            value = self.__get_low_threshold_by_restful()
        else:
            value = self.__get_low_threshold_by_cache()
        return value

    def get_high_critical_threshold(self):
        """
        Retrieves the high critical threshold temperature of thermal

        Returns:
            A float number, the high critical threshold temperature of thermal in Celsius
            up to nearest thousandth of one degree Celsius, e.g. 30.125
        """
        if self.method == CommonCfg.BY_SYSFS:
            value = self.__get_high_critical_threshold_by_sysfs()
        elif self.method == CommonCfg.BY_RESTFUL:
            value = self.__get_high_critical_threshold_by_restful()
        else:
            value = self.__get_high_critical_threshold_by_cache()
        return value

    def get_low_critical_threshold(self):
        """
        Retrieves the low critical threshold temperature of thermal

        Returns:
            A float number, the low critical threshold temperature of thermal in Celsius
            up to nearest thousandth of one degree Celsius, e.g. 30.125
        """
        if self.method == CommonCfg.BY_SYSFS:
            value = self.__get_low_critical_threshold_by_sysfs()
        elif self.method == CommonCfg.BY_RESTFUL:
            value = self.__get_low_critical_threshold_by_restful()
        else:
            value = self.__get_low_critical_threshold_by_cache()
        return value

    def set_high_threshold(self, temperature):
        """
        Sets the high threshold temperature of thermal

        Args :
            temperature: A float number up to nearest thousandth of one degree Celsius,
            e.g. 30.125
        Returns:
            A boolean, True if threshold is set successfully, False if not
        """
        # self.plat_common.log_notice("Unsupported operation: set_high_threshold")
        return False

    def set_low_threshold(self, temperature):
        """
        Sets the low threshold temperature of thermal

        Args :
            temperature: A float number up to nearest thousandth of one degree Celsius,
            e.g. 30.125
        Returns:
            A boolean, True if threshold is set successfully, False if not
        """
        # self.plat_common.log_notice("Unsupported operation: set_low_threshold")
        return False

    def set_high_critical_threshold(self, temperature):
        """
        Sets the critical high threshold temperature of thermal

        Args :
            temperature: A float number up to nearest thousandth of one degree Celsius,
            e.g. 30.125

        Returns:
            A boolean, True if threshold is set successfully, False if not
        """
        # self.plat_common.log_notice("Unsupported operation: set_high_critical_threshold")
        return False

    def set_low_critical_threshold(self, temperature):
        """
        Sets the critical low threshold temperature of thermal

        Args :
            temperature: A float number up to nearest thousandth of one degree Celsius,
            e.g. 30.125

        Returns:
            A boolean, True if threshold is set successfully, False if not
        """
        # self.plat_common.log_notice("Unsupported operation: set_low_critical_threshold")
        return False

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
        return "Thermal"

    def get_serial(self):
        """
        Retrieves the serial number of the Thermal

        Returns:
            string: Serial number of Thermal
        """
        return CommonCfg.NULL_VALUE

    def get_presence(self):
        """
        Retrieves the presence of the device

        Returns:
            bool: True if device is present, False if not
        """
        return True

    def get_status(self):
        """
        Retrieves the operational status of the device

        Returns:
            A boolean value, True if device is operating properly, False if not
        """
        try:
            value = self.get_temperature()
            low_thd = self.get_low_threshold()
            high_thd = self.get_high_threshold()
            if value is not None and low_thd is not None and high_thd is not None:
                if low_thd <= value <= high_thd:
                    return True
        except Exception as err:
            self.plat_common.log_error("can't get thermal status:{}".format(str(err)))

        return False

    # def get_position_in_parent(self):
    #     """
    #     Retrieves 1-based relative physical position in parent device. If the agent cannot determine the parent-relative position
    #     for some reason, or if the associated value of entPhysicalContainedIn is '0', then the value '-1' is returned
    #     Returns:
    #         integer: The 1-based relative physical position in parent device or -1 if cannot determine the position
    #     """
    #     return -1

    def is_replaceable(self):
        """
        Indicate whether this device is replaceable.
        Returns:
            bool: True if it is replaceable.
        """
        return False

    def get_minimum_recorded(self):
        """
        Retrieves the minimum recorded temperature of thermal

        Returns:
            A float number, the minimum recorded temperature of thermal in Celsius
            up to nearest thousandth of one degree Celsius, e.g. 30.125
        """
        if len(self.temperature_list) == 0:
            return self.get_temperature()
        return min(self.temperature_list)

    def get_maximum_recorded(self):
        """
        Retrieves the maximum recorded temperature of thermal

        Returns:
            A float number, the maximum recorded temperature of thermal in Celsius
            up to nearest thousandth of one degree Celsius, e.g. 30.125
        """
        if len(self.temperature_list) == 0:
            return self.get_temperature()
        return max(self.temperature_list)

    def get_change_event(self):
        """
        Returns a nested dictionary containing thermal devices which have experienced a change

        Returns:
            -----------------------------------------------------------------
            device   |     device_id       |  device_event  |  annotate
            -----------------------------------------------------------------
            'thermal'      '<thermal name>'   '0'              Thermal normal
                                              '1'              Thermal Abnormal
        """
        new_status = self.get_status()
        if self._old_status != new_status:
            self._old_status = new_status
            if new_status:
                self.plat_common.log_notice('{} is normal'.format(self.name))
                return (True, {"thermal": {self.name:"0"}})
            else:
                self.plat_common.log_notice('{} is abnormal'.format(self.name))
                return (True, {"thermal": {self.name:"1"}})

        return (False, {"thermal": {}})

    def __get_temperature_by_sysfs(self):
        """
        Retrieves current temperature reading from thermal

        Returns:
            A float number of current temperature in Celsius up to nearest thousandth
            of one degree Celsius, e.g. 30.125
        """
        value = self.plat_common.read_file(self.__get_file_path("value"))
        if self.plat_common.is_valid_value(value):
            return round(float(value) / CommonCfg.TEMPERATURE_FACTOR, 3)

        return None

    def __get_temperature_by_restful(self):
        """
        Retrieves current temperature reading from thermal

        Returns:
            A float number of current temperature in Celsius up to nearest thousandth
            of one degree Celsius, e.g. 30.125
        """
        try:
            sensor_info = self.plat_common.get_one_sensor_info_by_restful(self.get_name())
            if sensor_info is not None and \
                self.plat_common.is_float(sensor_info.get("Value")) and \
                self.plat_common.is_valid_value(sensor_info.get("Value")):
                value = sensor_info.get("Value")
                return round(float(value), 3)
        except Exception as error:
            self.plat_common.log_error("Get temperature error:{}".format(str(error)))

        return None

    def __get_temperature_by_cache(self):
        """
        Retrieves current temperature reading from thermal

        Returns:
            A float number of current temperature in Celsius up to nearest thousandth
            of one degree Celsius, e.g. 30.125
        """
        try:
            sensor_info = self.plat_common.get_one_sensor_info_by_cache(self.get_name())
            if sensor_info is not None and \
                self.plat_common.is_float(sensor_info.get("Value")) and \
                self.plat_common.is_valid_value(sensor_info.get("Value")):
                value = sensor_info.get("Value")
                return round(float(value), 3)
        except Exception as error:
            self.plat_common.log_error("Get temperature error:{}".format(str(error)))

        return None

    def __get_high_threshold_by_sysfs(self):
        """
        Retrieves the high threshold temperature of thermal

        Returns:
            A float number, the high threshold temperature of thermal in Celsius
            up to nearest thousandth of one degree Celsius, e.g. 30.125
        """
        value = self.plat_common.read_file(self.__get_file_path("max"))
        if self.plat_common.is_valid_value(value):
            return round(float(value) / CommonCfg.TEMPERATURE_FACTOR, 3)

        return None

    def __get_high_threshold_by_restful(self):
        """
        Retrieves the high threshold temperature of thermal

        Returns:
            A float number, the high threshold temperature of thermal in Celsius
            up to nearest thousandth of one degree Celsius, e.g. 30.125
        """
        try:
            sensor_info = self.plat_common.get_one_sensor_info_by_restful(self.get_name())
            if sensor_info is not None and \
                self.plat_common.is_float(sensor_info.get("Warning_High")) and \
                self.plat_common.is_valid_value(sensor_info.get("Warning_High")):
                value = sensor_info.get("Warning_High")
                return round(float(value), 3)
        except Exception as error:
            self.plat_common.log_error("Get temperature high threshold error:{}".format(str(error)))

        return None

    def __get_high_threshold_by_cache(self):
        """
        Retrieves the high threshold temperature of thermal

        Returns:
            A float number, the high threshold temperature of thermal in Celsius
            up to nearest thousandth of one degree Celsius, e.g. 30.125
        """
        try:
            sensor_info = self.plat_common.get_one_sensor_info_by_cache(self.get_name())
            if sensor_info is not None and \
                self.plat_common.is_float(sensor_info.get("Warning_High")) and \
                self.plat_common.is_valid_value(sensor_info.get("Warning_High")):
                value = sensor_info.get("Warning_High")
                return round(float(value), 3)
        except Exception as error:
            self.plat_common.log_error("Get temperature high threshold error:{}".format(str(error)))

        return None

    def __get_low_threshold_by_sysfs(self):
        """
        Retrieves the low threshold temperature of thermal

        Returns:
            A float number, the low threshold temperature of thermal in Celsius
            up to nearest thousandth of one degree Celsius, e.g. 30.125
        """
        value = self.plat_common.read_file(self.__get_file_path("min"))
        if self.plat_common.is_valid_value(value):
            return round(float(value) / CommonCfg.TEMPERATURE_FACTOR, 3)

        return None

    def __get_low_threshold_by_restful(self):
        """
        Retrieves the low threshold temperature of thermal

        Returns:
            A float number, the low threshold temperature of thermal in Celsius
            up to nearest thousandth of one degree Celsius, e.g. 30.125
        """
        try:
            sensor_info = self.plat_common.get_one_sensor_info_by_restful(self.get_name())
            if sensor_info is not None and \
                self.plat_common.is_float(sensor_info.get("Warning_Low")) and \
                self.plat_common.is_valid_value(sensor_info.get("Warning_Low")):
                value = sensor_info.get("Warning_Low")
                return round(float(value), 3)
        except Exception as error:
            self.plat_common.log_error("Get temperature low threshold error:{}".format(str(error)))

        return None

    def __get_low_threshold_by_cache(self):
        """
        Retrieves the low threshold temperature of thermal

        Returns:
            A float number, the low threshold temperature of thermal in Celsius
            up to nearest thousandth of one degree Celsius, e.g. 30.125
        """
        try:
            sensor_info = self.plat_common.get_one_sensor_info_by_cache(self.get_name())
            if sensor_info is not None and \
                self.plat_common.is_float(sensor_info.get("Warning_Low")) and \
                self.plat_common.is_valid_value(sensor_info.get("Warning_Low")):
                value = sensor_info.get("Warning_Low")
                return round(float(value), 3)
        except Exception as error:
            self.plat_common.log_error("Get temperature low threshold error:{}".format(str(error)))

        return None

    def __get_high_critical_threshold_by_sysfs(self):
        """
        Retrieves the high critical threshold temperature of thermal

        Returns:
            A float number, the high critical threshold temperature of thermal in Celsius
            up to nearest thousandth of one degree Celsius, e.g. 30.125
        """
        value = self.plat_common.read_file(self.__get_file_path("critical_max"))
        if self.plat_common.is_valid_value(value):
            return round(float(value) / CommonCfg.TEMPERATURE_FACTOR, 3)

        return None

    def __get_high_critical_threshold_by_restful(self):
        """
        Retrieves the high critical threshold temperature of thermal

        Returns:
            A float number, the high critical threshold temperature of thermal in Celsius
            up to nearest thousandth of one degree Celsius, e.g. 30.125
        """
        try:
            sensor_info = self.plat_common.get_one_sensor_info_by_restful(self.get_name())
            if sensor_info is not None and \
                self.plat_common.is_float(sensor_info.get("Critical_High")) and \
                self.plat_common.is_valid_value(sensor_info.get("Critical_High")):
                value = sensor_info.get("Critical_High")
                return round(float(value), 3)
        except Exception as error:
            self.plat_common.log_error("Get temperature critical high threshold error:{}".format(str(error)))

        return None

    def __get_high_critical_threshold_by_cache(self):
        """
        Retrieves the high critical threshold temperature of thermal

        Returns:
            A float number, the high critical threshold temperature of thermal in Celsius
            up to nearest thousandth of one degree Celsius, e.g. 30.125
        """
        try:
            sensor_info = self.plat_common.get_one_sensor_info_by_cache(self.get_name())
            if sensor_info is not None and \
                self.plat_common.is_float(sensor_info.get("Critical_High")) and \
                self.plat_common.is_valid_value(sensor_info.get("Critical_High")):
                value = sensor_info.get("Critical_High")
                return round(float(value), 3)
        except Exception as error:
            self.plat_common.log_error("Get temperature critical high threshold error:{}".format(str(error)))

        return None

    def __get_low_critical_threshold_by_sysfs(self):
        """
        Retrieves the low critical threshold temperature of thermal

        Returns:
            A float number, the low critical threshold temperature of thermal in Celsius
            up to nearest thousandth of one degree Celsius, e.g. 30.125
        """
        value = self.plat_common.read_file(self.__get_file_path("critical_min"))
        if self.plat_common.is_valid_value(value):
            return round(float(value) / CommonCfg.TEMPERATURE_FACTOR, 3)

        return None

    def __get_low_critical_threshold_by_restful(self):
        """
        Retrieves the low critical threshold temperature of thermal

        Returns:
            A float number, the low critical threshold temperature of thermal in Celsius
            up to nearest thousandth of one degree Celsius, e.g. 30.125
        """
        try:
            sensor_info = self.plat_common.get_one_sensor_info_by_restful(self.get_name())
            if sensor_info is not None and \
                self.plat_common.is_float(sensor_info.get("Critical_Low")) and \
                self.plat_common.is_valid_value(sensor_info.get("Critical_Low")):
                value = sensor_info.get("Critical_Low")
                return round(float(value), 3)
        except Exception as error:
            self.plat_common.log_error("Get temperature critical low threshold error:{}".format(str(error)))

        return None

    def __get_low_critical_threshold_by_cache(self):
        """
        Retrieves the low critical threshold temperature of thermal

        Returns:
            A float number, the low critical threshold temperature of thermal in Celsius
            up to nearest thousandth of one degree Celsius, e.g. 30.125
        """
        try:
            sensor_info = self.plat_common.get_one_sensor_info_by_cache(self.get_name())
            if sensor_info is not None and \
                self.plat_common.is_float(sensor_info.get("Critical_Low")) and \
                self.plat_common.is_valid_value(sensor_info.get("Critical_Low")):
                value = sensor_info.get("Critical_Low")
                return round(float(value), 3)
        except Exception as error:
            self.plat_common.log_error("Get temperature critical low threshold error:{}".format(str(error)))

        return None


    # other unimportant DeviceBase abstract functions implementation
    def get_position_in_parent(self):
        """
        Retrieves 1-based relative physical position in parent device. If the agent cannot determine the parent-relative position
        for some reason, or if the associated value of entPhysicalContainedIn is '0', then the value '-1' is returned
        Returns:
            integer: The 1-based relative physical position in parent device or -1 if cannot determine the position
        """
        return self.index

    def get_revision(self):
        """
        Retrieves the hardware revision of the device

        Returns:
            string: Revision value of device
        """
        return "N/A"
