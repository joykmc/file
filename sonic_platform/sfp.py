# -*- coding: UTF-8 -*-

"""
Module contains an implementation of SONiC Platform Base API and
provides the SFP information which are available in the platform
"""

try:
    import struct
    import os.path
    import time
    from sonic_platform_base.sonic_xcvr.sfp_optoe_base import SfpOptoeBase
    from sonic_platform.plat_common import PlatCommon
    from sonic_platform.plat_common import CommonCfg
    from vendor_sonic_platform import hooks
except ImportError as e:
    raise ImportError (str(e) + "- required module not found") from e


QSFP_CHANNL_RX_LOS_STATUS_OFFSET = 3
QSFP_CHANNL_RX_LOS_STATUS_WIDTH = 1
QSFP_CHANNL_TX_FAULT_STATUS_OFFSET = 4
QSFP_CHANNL_TX_FAULT_STATUS_WIDTH = 1
QSFP_CHANNL_DISABLE_STATUS_OFFSET = 86
QSFP_CHANNL_DISABLE_STATUS_WIDTH = 1

XCVR_COMPLIANCE_CODE_OFFSET = 3
XCVR_COMPLIANCE_CODE_WIDTH = 8
XCVR_EXTENDED_COMPLIANCE_OFFSET_QSFP = 64
XCVR_EXTENDED_COMPLIANCE_OFFSET_SFP = 36
XCVR_EXTENDED_COMPLIANCE_WIDTH = 1
XCVR_CONNECTOR_OFFSET = 2
XCVR_CONNECTOR_WIDTH = 1
QSFP_DEVICE_TECH_OFFSET = 19
QSFP_DEVICE_TECH_WIDTH = 1

QSFP_DD_CHANNEL_RX_LOS_STATUS_OFFSET = 17 * 128 + 147
QSFP_DD_CHANNEL_RX_LOS_STATUS_WIDTH = 1
QSFP_DD_CHANNEL_TX_FAULT_STATUS_OFFSET = 17 * 128 + 135
QSFP_DD_CHANNEL_TX_FAULT_STATUS_WIDTH = 1
QSFP_DD_CHANNEL_TX_DISABLE_STATUS_OFFSET = 16 * 128 + 130
QSFP_DD_CHANNEL_TX_DISABLE_STATUS_WIDTH = 1

SFP_AOC_10G_MASK = 0xf0
SFP_AOC_1G_MASK = 0x0b
QSFP_AOC_MASK = 0x77
QSFP_DAC_MASK = 0x08
QSFP_DONT_CARE_BIT = 0x80

SFP_TYPE = "SFP"
QSFP_TYPE = "QSFP"
QSFP_DD_TYPE = "QSFP_DD"
OSFP_TYPE = "OSFP"
DSFP_TYPE = "DSFP"

cable_type_map = {
    'qsfp_optical': 0,
    'sfp_optical': 1,
    'sfp_cable': 2,
    'qsfp_copper_cable': 3,
    'qsfp_other_cable': 4
}

#If meet init slow module, change this value
SFP_INSERT_DELAY_MS = 2000

class Sfp(SfpOptoeBase):
    """Platform-specific SFP/XCVR class"""

    def __init__(self, index):
        """Initialize sfp object
        Args:
            index: int, start from 0
        """
        SfpOptoeBase.__init__(self)
        self.index = index + 1         # sfp index start from 1
        self.sfp_type = None
        self.plat_comm = PlatCommon(debug=CommonCfg.DEBUG)
        self.sysfs_path = os.path.join(CommonCfg.S3IP_SFP_PATH, "eth{}".format(self.index))
        self._old_presence = False
        self.__init_cage_type()
        self._start_ms = 0

    def __init_cage_type(self):
        try:
            cage_type_path = self.__get_file_path("type")
            self.cage_type = self.plat_comm.read_file(cage_type_path)
        except Exception as err:
            self.plat_comm.log_error(str(err))
            self.cage_type = None

    def get_cage_type(self):
        """
        Retrieves the type of the SFP

        Returns:
            str: "SFP", "QSFP", "QSFP_DD"
        """
        return self.cage_type

    def get_port_or_cage_type(self):
        """
        Retrieves sfp port or cage type corresponding to physical port <index>

        Returns:
            The masks of all types of port or cage that can be supported on the port
            Types are defined in sfp_base.py
            Eg.
                Both SFP and SFP+ are supported on the port, the return value should be 0x0a
                which is 0x02 | 0x08
        """
        supported_type = 0
        if self.cage_type == SFP_TYPE:
            supported_type = self.SFP_PORT_TYPE_BIT_SFP | \
                             self.SFP_PORT_TYPE_BIT_SFP_PLUS | \
                             self.SFP_PORT_TYPE_BIT_SFP28
        elif self.cage_type == QSFP_TYPE:
            supported_type = self.SFP_PORT_TYPE_BIT_QSFP28 | \
                             self.SFP_PORT_TYPE_BIT_QSFP | \
                             self.SFP_PORT_TYPE_BIT_QSFP_PLUS
        elif self.cage_type == QSFP_DD_TYPE:
            supported_type = self.SFP_PORT_TYPE_BIT_QSFP56 | \
                             self.SFP_PORT_TYPE_BIT_QSFPDD
        return supported_type

    def get_name(self):
        """
        Retrieves the name of the SFP

        Returns:
            str: "sfp1"
        """
        return "Ethernet{}".format(self.index)

    def get_eeprom_path(self):
        """
        Retrieves the eeprom path of the SFP

        Returns:
            str: '/sys_switch/transceiver/eth1/eeprom'
        """
        return self.__get_file_path("eeprom")

    def get_sfp_type(self):
        """
        Retrieves the type of the SFP

        Returns:
            str: "SFP", "QSFP", "QSFP_DD"
        """
        if not self.get_presence():
            return self.cage_type
        else:
            if self.sfp_type is None:
                self.refresh_xcvr_api()
        return self.sfp_type

    def get_presence(self):
        """
        Retrieves the presence of the SFP

        Returns:
            bool: True if SFP is present, False if not
        """
        try:
            presence_sysfs_path = self.__get_file_path("present")
            result = self.plat_comm.read_file(presence_sysfs_path)
            if self.plat_comm.is_valid_value(result):
                return int(result) == 1
        except Exception as err:
            self.plat_comm.log_error(str(err))

        return False

    def read_eeprom(self, offset, num_bytes):
        try:
            with open(self.get_eeprom_path(), mode='rb', buffering=0) as f:
                f.seek(offset)
                return bytearray(f.read(num_bytes))
        except (OSError, IOError):
            # if access sfp eeprom failed, the i2c clock maybe be pulled low always.
            if hasattr(hooks, "reset_pca9548"):
                hooks.reset_pca9548(self.index)
            return None

    def write_eeprom(self, offset, num_bytes, write_buffer):
        try:
            with open(self.get_eeprom_path(), mode='r+b', buffering=0) as f:
                f.seek(offset)
                f.write(write_buffer[0:num_bytes])
        except (OSError, IOError):
            # if access sfp eeprom failed, the i2c clock maybe be pulled low always.
            if hasattr(hooks, "reset_pca9548"):
                hooks.reset_pca9548(self.index)
            return False
        return True

    def get_transceiver_info(self):
        # temporary solution for a SONiC community bug
        transceiver_info = super().get_transceiver_info()
        try:
            if transceiver_info and transceiver_info["vendor_rev"] is not None:
                transceiver_info["hardware_rev"] = transceiver_info["vendor_rev"]
            return transceiver_info
        except Exception as error:
            self.plat_comm.log_error(str(error))
        return None

    def get_transceiver_bulk_status(self):
        # temporary record sfp temperature into s3ip-sysfs cache
        transceiver_bulk_status = super().get_transceiver_bulk_status()
        if transceiver_bulk_status and transceiver_bulk_status["temperature"] is not None:
            sfp_temp_sysfs_path = os.path.join(self.sysfs_path, "temp1", "value")
            self.plat_comm.write_file(sfp_temp_sysfs_path, transceiver_bulk_status["temperature"])

        return transceiver_bulk_status

    def get_temperature_cache(self):
        """
        Retrieves the sfp/qsfp temperature from S3IP sysfs

        Returns:
            a integer, the value of sfp/qsfp temperature
        """
        sfp_temp_sysfs_path = os.path.join(self.sysfs_path, "temp1", "value")
        temp = self.plat_comm.read_file(sfp_temp_sysfs_path)
        if self.plat_comm.is_valid_value(temp):
            return float(temp)
        return None

    def get_reset_status(self):
        """
        Retrieves the reset status of SFP

        Returns:
            A Boolean, True if reset enabled, False if disabled
        """
        if not self.get_presence():
            return False

        if self.sfp_type is None:
            self.refresh_xcvr_api()

        if not self.sfp_type:
            return False

        if self.sfp_type == SFP_TYPE:
            return False

        try:
            sysfs_path = self.__get_file_path("reset")
            result = self.plat_comm.read_file(sysfs_path)
            if self.plat_comm.is_valid_value(result):
                return int(result) == 1
        except Exception as err:
            self.plat_comm.log_error(str(err))

        return False

    def reset(self):
        """
        Reset SFP and return all user module settings to their default state.

        Returns:
            A boolean, True if successful, False if not
        """
        if self.get_presence() is False:
            return False

        if self.sfp_type is None:
            self.refresh_xcvr_api()

        if not self.sfp_type:
            return False

        if self.sfp_type == SFP_TYPE:
            return False

        self._set_write_enable(False)
        ret = self._set_reset(True)
        if ret:
            time.sleep(0.5)
            ret = self._set_reset(False)
        self._set_write_enable(True)
        return ret

    def set_reset(self, reset):
        """
        Reset SFP and return all user module settings to their default state.
        Returns:
            A boolean, True if successful, False if not
        """
        if self.get_presence() is False:
            return False

        if self.sfp_type is None:
            self.refresh_xcvr_api()

        if self.sfp_type == SFP_TYPE:
            self.plat_comm.log_error('SFP does not support reset')
            return False

        self._set_write_enable(False)
        ret = self._set_reset(reset)
        self._set_write_enable(True)
        return ret

    def get_lpmode(self):
        """
        Retrieves the lpmode (low power mode) status of this SFP
        Returns:
            A Boolean, True if lpmode is enabled, False if disabled
        """
        if not self.get_presence():
            return False

        if self.sfp_type is None:
            self.refresh_xcvr_api()

        if not self.sfp_type:
            return False

        if self.sfp_type == SFP_TYPE:
            return False

        try:
            sysfs_path = self.__get_file_path("low_power_mode")
            result = self.plat_comm.read_file(sysfs_path)
            if self.plat_comm.is_valid_value(result):
                return int(result) == 1
        except Exception as err:
            self.plat_comm.log_error(str(err))

        return False

    def set_lpmode(self, lpmode):
        """
        Sets the lpmode (low power mode) of SFP

        Args:
            lpmode: A Boolean, True to enable lpmode, False to disable it
            Note  : lpmode can be overridden by set_power_override

        Returns:
            A boolean, True if lpmode is set successfully, False if not
        """
        if not self.get_presence():
            return False

        if self.sfp_type is None or self._xcvr_api is None:
            self.refresh_xcvr_api()

        if not self.sfp_type:
            return False

        if self.sfp_type == SFP_TYPE:
            return False

        self._set_write_enable(False)
        value = 1 if lpmode else 0
        ret = self.plat_comm.write_file(self.__get_file_path("low_power_mode"), value)
        self._set_write_enable(True)
        return ret

    def get_rx_los(self):
        """
        Retrieves the RX LOS (loss-of-signal) status of SFP

        Returns:
            A list of boolean values, representing the RX LOS status
            of each available channel, value is True if SFP channel
            has RX LOS, False if not.
            E.g., for a tranceiver with four channels: [False, False, True, False]
            Note : RX LOS status is latched until a call to get_rx_los or a reset.
        """
        if not self.get_presence():
            return [False]

        if self.sfp_type is None:
            self.refresh_xcvr_api()

        rx_los_list = []
        if self.sfp_type == QSFP_TYPE:
            rx_los_raw = self.read_eeprom(QSFP_CHANNL_RX_LOS_STATUS_OFFSET, QSFP_CHANNL_RX_LOS_STATUS_WIDTH)
            if rx_los_raw is None:
                self.plat_comm.log_info("QSFP rx_los_raw is None")
                return None
            rx_los_data = struct.unpack("B", rx_los_raw)[0]
            rx_los_list.append(rx_los_data & 0x01 != 0)
            rx_los_list.append(rx_los_data & 0x02 != 0)
            rx_los_list.append(rx_los_data & 0x04 != 0)
            rx_los_list.append(rx_los_data & 0x08 != 0)
        elif self.sfp_type == QSFP_DD_TYPE:
            rx_los_raw = self.read_eeprom(QSFP_DD_CHANNEL_RX_LOS_STATUS_OFFSET, QSFP_DD_CHANNEL_RX_LOS_STATUS_WIDTH)
            if rx_los_raw is None:
                self.plat_comm.log_info("QSFP-DD rx_los_raw is None")
                return None
            rx_los_data = struct.unpack("B", rx_los_raw)[0]
            rx_los_list.append(rx_los_data & 0x01 != 0)
            rx_los_list.append(rx_los_data & 0x02 != 0)
            rx_los_list.append(rx_los_data & 0x04 != 0)
            rx_los_list.append(rx_los_data & 0x08 != 0)
            rx_los_list.append(rx_los_data & 0x10 != 0)
            rx_los_list.append(rx_los_data & 0x20 != 0)
            rx_los_list.append(rx_los_data & 0x40 != 0)
            rx_los_list.append(rx_los_data & 0x80 != 0)
        elif self.sfp_type == SFP_TYPE:
            try:
                sysfs_path = self.__get_file_path("rx_los")
                result = self.plat_comm.read_file(sysfs_path)
                if self.plat_comm.is_valid_value(result):
                    rx_los_list.append(int(result) == 1)
            except Exception as err:
                self.plat_comm.log_error(str(err))
                return None

        return rx_los_list

    def get_tx_fault(self):
        """
        Retrieves the TX fault status of SFP

        Returns:
            A list of boolean values, representing the TX fault status
            of each available channel, value is True if SFP channel
            has TX fault, False if not.
            E.g., for a tranceiver with four channels: [False, False, True, False]
            Note : TX fault status is lached until a call to get_tx_fault or a reset.
        """
        if not self.get_presence():
            return [False]

        if self.sfp_type is None:
            self.refresh_xcvr_api()

        tx_fault_list = []
        if self.sfp_type == QSFP_TYPE:
            tx_fault_raw = self.read_eeprom(QSFP_CHANNL_TX_FAULT_STATUS_OFFSET, QSFP_CHANNL_TX_FAULT_STATUS_WIDTH)
            if tx_fault_raw is None:
                self.plat_comm.log_info("QSFP tx_fault_raw is None")
                return None
            tx_fault_data = struct.unpack("B", tx_fault_raw)[0]
            tx_fault_list.append(tx_fault_data & 0x01 != 0)
            tx_fault_list.append(tx_fault_data & 0x02 != 0)
            tx_fault_list.append(tx_fault_data & 0x04 != 0)
            tx_fault_list.append(tx_fault_data & 0x08 != 0)
        elif self.sfp_type == QSFP_DD_TYPE:
            tx_fault_raw = self.read_eeprom(QSFP_DD_CHANNEL_TX_FAULT_STATUS_OFFSET,
                                            QSFP_DD_CHANNEL_TX_FAULT_STATUS_WIDTH)
            if tx_fault_raw is None:
                self.plat_comm.log_info("QSFP-DD tx_fault_raw is None")
                return None
            tx_fault_data = struct.unpack("B", tx_fault_raw)[0]
            tx_fault_list.append(tx_fault_data & 0x01 != 0)
            tx_fault_list.append(tx_fault_data & 0x02 != 0)
            tx_fault_list.append(tx_fault_data & 0x04 != 0)
            tx_fault_list.append(tx_fault_data & 0x08 != 0)
            tx_fault_list.append(tx_fault_data & 0x10 != 0)
            tx_fault_list.append(tx_fault_data & 0x20 != 0)
            tx_fault_list.append(tx_fault_data & 0x40 != 0)
            tx_fault_list.append(tx_fault_data & 0x80 != 0)
        elif self.sfp_type == SFP_TYPE:
            try:
                sysfs_path = self.__get_file_path("tx_fault")
                result = self.plat_comm.read_file(sysfs_path)
                if self.plat_comm.is_valid_value(result):
                    tx_fault_list.append(int(result) == 1)
            except Exception as err:
                self.plat_comm.log_error(str(err))
                return None

        return tx_fault_list

    def get_tx_disable(self):
        """
        Retrieves the tx_disable status of this SFP

        Returns:
            A list of boolean values, representing the TX disable status
            of each available channel, value is True if SFP channel
            is TX disabled, False if not.
            E.g., for a tranceiver with four channels: [False, False, True, False]
        """
        if not self.get_presence():
            return [False]

        if self.sfp_type is None:
            self.refresh_xcvr_api()

        tx_disable_list = []
        if self.sfp_type == QSFP_TYPE:
            tx_disable_raw = self.read_eeprom(QSFP_CHANNL_DISABLE_STATUS_OFFSET, QSFP_CHANNL_DISABLE_STATUS_WIDTH)
            if tx_disable_raw is None:
                self.plat_comm.log_info("QSFP tx_disable_raw is None")
                return None
            tx_disable_data = struct.unpack("B", tx_disable_raw)[0]
            tx_disable_list.append(tx_disable_data & 0x01 != 0)
            tx_disable_list.append(tx_disable_data & 0x02 != 0)
            tx_disable_list.append(tx_disable_data & 0x04 != 0)
            tx_disable_list.append(tx_disable_data & 0x08 != 0)

        elif self.sfp_type == QSFP_DD_TYPE:
            tx_disable_raw = self.read_eeprom(QSFP_DD_CHANNEL_TX_DISABLE_STATUS_OFFSET,
                                              QSFP_DD_CHANNEL_TX_DISABLE_STATUS_WIDTH)
            if tx_disable_raw is None:
                self.plat_comm.log_info("QSFP-DD tx_disable_raw is None")
                return None
            tx_disable_data = struct.unpack("B", tx_disable_raw)[0]
            tx_disable_list.append(tx_disable_data & 0x01 != 0)
            tx_disable_list.append(tx_disable_data & 0x02 != 0)
            tx_disable_list.append(tx_disable_data & 0x04 != 0)
            tx_disable_list.append(tx_disable_data & 0x08 != 0)
            tx_disable_list.append(tx_disable_data & 0x10 != 0)
            tx_disable_list.append(tx_disable_data & 0x20 != 0)
            tx_disable_list.append(tx_disable_data & 0x40 != 0)
            tx_disable_list.append(tx_disable_data & 0x80 != 0)

        elif self.sfp_type == SFP_TYPE:
            try:
                sysfs_path = self.__get_file_path("tx_disable")
                result = self.plat_comm.read_file(sysfs_path)
                if self.plat_comm.is_valid_value(result):
                    tx_disable_list.append(int(result) == 1)
            except Exception as err:
                self.plat_comm.log_error(str(err))
                return None

        return tx_disable_list

    def tx_disable(self, tx_disable):
        """
        Disable SFP TX for all channels

        Args:
            tx_disable : A Boolean, True to enable tx_disable mode, False to disable
                         tx_disable mode.

        Returns:
            A boolean, True if tx_disable is set successfully, False if not
        """
        if self.get_presence() is False:
            return False

        if self.sfp_type is None:
            self.refresh_xcvr_api()

        if self.sfp_type == QSFP_TYPE:
            try:
                return self.tx_disable_channel(0xf, tx_disable)
            except Exception as err:
                self.plat_comm.log_error(str(err))
                return False
        elif self.sfp_type == QSFP_DD_TYPE:
            try:
                return self.tx_disable_channel(0xff, tx_disable)
            except Exception as err:
                self.plat_comm.log_error(str(err))
                return False
        elif self.sfp_type == SFP_TYPE:
            self._set_write_enable(False)
            ret = self._set_tx_disable(tx_disable)
            self._set_write_enable(True)
            return ret

        return False

    def set_optoe_write_max(self, write_max):
        """
        This func is declared and implemented by SONiC but we're not supported
        so override it as NotImplemented
        """
        self.plat_comm.log_info("set_optoe_write_max NotImplemented")
        return False

    def refresh_xcvr_api(self):
        """
        Updates the XcvrApi associated with this SFP
        """
        self._xcvr_api = self._xcvr_api_factory.create_xcvr_api()
        self.refresh_sfp_type(self._xcvr_api.__class__.__name__)

    def get_error_description(self):
        """
        Retrives the error descriptions of the SFP module

        Returns:
            String that represents the current error descriptions of vendor specific errors
            In case there are multiple errors, they should be joined by '|',
            like: "Bad EEPROM|Unsupported cable"
        """
        if not self.get_presence():
            return self.SFP_STATUS_UNPLUGGED

        return self.SFP_STATUS_OK

    def get_change_event(self):
        """
        Returns a nested dictionary containing sfp devices which have experienced a change

        Returns:
            -----------------------------------------------------------------
            device   |     device_id       |  device_event  |  annotate
            -----------------------------------------------------------------
            'sfp'          '<sfp number>'     '0'              Sfp removed
                                              '1'              Sfp inserted
                                              '2'              I2C bus stuck
                                              '3'              Bad eeprom
                                              '4'              Unsupported cable
                                              '5'              High Temperature
                                              '6'              Bad cable
                                              '7'              Sfp ok
        """
        now_ms = time.time() * 1000
        new_presence = self.get_presence()
        plug_record = self.get_plug_record()
        if hasattr(hooks, "sfp_change_event_delay"):
            hooks.sfp_change_event_delay()
        if self._old_presence != new_presence or plug_record:
            if new_presence:
                if now_ms - self._start_ms >= SFP_INSERT_DELAY_MS:
                    self._old_presence = new_presence
                    self.plat_comm.log_notice('SFP{} is present'.format(self.index))
                    self._xcvr_api = None
                    return True, {"sfp": {self.index - 1: "1"}}
            else:
                self._old_presence = new_presence
                self.plat_comm.log_notice('SFP{} is absent'.format(self.index))
                self._xcvr_api = None
                return True, {"sfp": {self.index - 1: "0"}}
        else:
            self._start_ms = now_ms

        return False, {"sfp": {}}

#################### private api ####################

    def refresh_sfp_type(self, class_name):
        if class_name == 'CmisApi':
            self.sfp_type = QSFP_DD_TYPE
        elif class_name == 'Sff8472Api':
            self.sfp_type = SFP_TYPE
        elif class_name in ['Sff8636Api', 'Sff8436Api']:
            self.sfp_type = QSFP_TYPE
        else:
            # self.plat_comm.log_error("get_device_type error, class_name not supported:%s" % class_name)
            self.sfp_type = None

    def _set_reset(self, reset):
        try:
            reset_sysfs_path = self.__get_file_path("reset")
            if reset:
                result = 1
            else:
                result = 0

            return self.plat_comm.write_file(reset_sysfs_path, result)
        except Exception as err:
            self.plat_comm.log_error(str(err))
        return False

    def _set_tx_disable(self, disable):
        try:
            sysfs_path = self.__get_file_path("tx_disable")
            if disable:
                result = 1
            else:
                result = 0

            return self.plat_comm.write_file(sysfs_path, result)
        except Exception as err:
            self.plat_comm.log_error(str(err))
        return False

    def __get_file_path(self, file_name):
        if self.sysfs_path is not None:
            return os.path.join(self.sysfs_path, file_name)

        return None

    def _set_write_enable(self, enable):
        try:
            sysfs_path = self.__get_file_path("write_enable")
            if enable:
                value = 0x4E
            else:
                value = 0x59

            return self.plat_comm.write_file(sysfs_path, value)
        except Exception as err:
            self.plat_comm.log_error(str(err))
        return False

    def __get_sfp_cable_type(self, raw_data, raw_data2):
        data2 = 0
        data = struct.unpack("B", raw_data)[0]
        if raw_data2 != 0:
            data2 = struct.unpack("B", raw_data2)[0]
        ret = None

        if self.sfp_type == QSFP_TYPE:
            if (data & QSFP_DONT_CARE_BIT) != 0:
                return ret
            if (data & QSFP_AOC_MASK) != 0:
                ret = 'Optical'
            elif (data & QSFP_DAC_MASK) != 0:
                ret = 'Copper'
            else:
                return None
        elif self.sfp_type == SFP_TYPE:
            if (data & SFP_AOC_10G_MASK) != 0 or (data2 & SFP_AOC_1G_MASK) != 0:
                ret = 'Optical'
            else:
                return None
        else:
            return None
        return ret

    def test_bit(self, n, bitpos):
        try:
            mask = 1 << bitpos
            if (n & mask) == 0:
                return 0
            else:
                return 1
        except Exception:
            return -1

    def __get_compliance_sfp_cable_type(self, raw_data):
        data = struct.unpack("B", raw_data)[0]
        aoc_list = [0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x10, 0x11, 0x12,
                    0x17, 0x18, 0x1A, 0x1B, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x24,
                    0x25, 0x26, 0x27, 0x31, 0x33, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46]
        dac_list = [0x08, 0x0B, 0x0C, 0x0D, 0x13, 0x14, 0x15, 0x16, 0x19, 0x1C,
                    0x1D, 0x30, 0x32, 0x40, 0x50, 0x51, 0x52, 0x53, 0x54, 0x55]

        if data in aoc_list:
            ret = 'Optical'
        elif data in dac_list:
            ret = 'Copper'
        else:
            ret = None

        return ret

    def get_transceiver_cable_type(self):
        if not self.get_presence():
            return None

        if self.sfp_type is None:
            self.refresh_xcvr_api()

        try:
            if self.sfp_type == QSFP_TYPE:
                offset = 128
                ext_compliance_offset = offset + XCVR_EXTENDED_COMPLIANCE_OFFSET_QSFP
                raw_data2 = 0
            elif self.sfp_type == SFP_TYPE:
                offset = 0
                ext_compliance_offset = offset + XCVR_EXTENDED_COMPLIANCE_OFFSET_SFP
                raw_data2 = self.read_eeprom(offset + 3 + XCVR_COMPLIANCE_CODE_OFFSET, 1)
                if raw_data2 is None:
                    return None
            elif self.sfp_type == QSFP_DD_TYPE:
                return "Optical"
            else:
                return
            raw_data = self.read_eeprom(offset + XCVR_COMPLIANCE_CODE_OFFSET, 1)
            if raw_data is None:
                return None
            transceiver_cable_type = self.__get_sfp_cable_type(raw_data, raw_data2)
            if transceiver_cable_type is None:
                sfp_ext_compliance_raw = self.read_eeprom(ext_compliance_offset,
                                                          XCVR_EXTENDED_COMPLIANCE_WIDTH)
                if sfp_ext_compliance_raw is None:
                    return None
                transceiver_cable_type = self.__get_compliance_sfp_cable_type(sfp_ext_compliance_raw)
            if (transceiver_cable_type is None) and (self.sfp_type == SFP_TYPE):
                cable_active_raw = self.read_eeprom(offset + 5 + XCVR_COMPLIANCE_CODE_OFFSET, 1)
                if cable_active_raw is None:
                    return None
                cable_active = struct.unpack("B", cable_active_raw)[0]
                if cable_active & 0x08 != 0:
                    transceiver_cable_type = "Optical"
            if transceiver_cable_type is None:
                transceiver_cable_type = 'OTHERS'
            return transceiver_cable_type
        except Exception as err:
            self.plat_comm.log_error(str(err))
        return None

    def _get_sfp_connector_type(self, raw_data):
        data = struct.unpack("B", raw_data)[0]
        ret = None
        if self.sfp_type == SFP_TYPE:
            if (0x1 <= data <= 0xd) or (0x23 <= data <= 0x28):
                ret = cable_type_map['sfp_optical']
            elif 0x20 <= data <= 0x22:
                ret = cable_type_map['sfp_cable']
        else:
            ret = None
        return ret

    def _get_sfp_cable_type(self, raw_data):
        data = struct.unpack("B", raw_data)[0]
        ret = None
        if self.sfp_type == QSFP_TYPE:
            if (data >> 4) >= 0b1010:
                ret = cable_type_map['qsfp_copper_cable']
            elif (data >> 4) < 0b1000 or (data >> 4) == 0b1001:
                ret = cable_type_map['qsfp_optical']
            elif (data >> 4) == 0b1000:
                ret = cable_type_map['qsfp_other_cable']
        elif self.sfp_type == SFP_TYPE:
            if self.test_bit(data, 2) == 1:
                ret = cable_type_map['sfp_cable']
            else:
                ret = cable_type_map['sfp_optical']
        elif self.sfp_type == QSFP_DD_TYPE:
            ret = cable_type_map['qsfp_optical']
        else:
            ret = None
        return ret

    def get_cable_type(self):
        if not self.get_presence():
            return None

        if self.sfp_type is None:
            self.refresh_xcvr_api()

        try:
            if self.sfp_type == QSFP_TYPE:
                offset = 128
                sfp_device_tech_raw = self.read_eeprom((offset + QSFP_DEVICE_TECH_OFFSET),
                                                       QSFP_DEVICE_TECH_WIDTH)
                if sfp_device_tech_raw is None:
                    return None
                cable_type = self._get_sfp_cable_type(sfp_device_tech_raw)
            elif self.sfp_type == SFP_TYPE:
                offset = 0
                sfp_compliance_code_raw = self.read_eeprom((offset + 5 + XCVR_COMPLIANCE_CODE_OFFSET),
                                                           1)
                if sfp_compliance_code_raw is None:
                    return None
                cable_type = self._get_sfp_cable_type(sfp_compliance_code_raw)
                # verify the connector type of module
                if cable_type is None:
                    sfp_connector_type_raw = self.read_eeprom((offset + XCVR_CONNECTOR_OFFSET),
                                                              XCVR_CONNECTOR_WIDTH)
                    if sfp_connector_type_raw is None:
                        return None
                    cable_type = self._get_sfp_connector_type(sfp_connector_type_raw)
            elif self.sfp_type == QSFP_DD_TYPE:
                cable_type = cable_type_map['qsfp_optical']
            else:
                return None
            return cable_type
        except Exception as err:
            self.plat_comm.log_error(str(err))
        return None

    def get_plug_record(self):
        """
        Retrieves the presence change event record of the SFP

        Returns:
            bool: True if SFP presence changed, False if not
        """
        try:
            plug_sysfs_path = self.__get_file_path("plug_record")
            result = self.plat_comm.read_file(plug_sysfs_path)
            if self.plat_comm.is_valid_value(result):
                return int(result) == 1
        except Exception as err:
            self.plat_comm.log_error(str(err))

        return False

    def get_interrupt(self):
        """
        Retrieves the interrupt status of SFP

        Returns:
            A Boolean, True if interrupt enabled, False if disabled
        """
        if not self.get_presence():
            return False

        if self.sfp_type is None:
            self.refresh_xcvr_api()

        if not self.sfp_type:
            return False

        if self.sfp_type == SFP_TYPE:
            return False

        try:
            sysfs_path = self.__get_file_path("interrupt")
            result = self.plat_comm.read_file(sysfs_path)
            if self.plat_comm.is_valid_value(result):
                return int(result) == 1
        except Exception as err:
            self.plat_comm.log_error(str(err))

        return False


    # other unimportant DeviceBase abstract functions implementation
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

    def get_revision(self):
        """
        Retrieves the hardware revision of the device

        Returns:
            string: Revision value of device
        """
        return "N/A"

    def get_status(self):
        """
        Retrieves the operational status of the device

        Returns:
            A boolean value, True if device is operating properly, False if not
        """
        return self.get_presence()
