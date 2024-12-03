# -*- coding: UTF-8 -*-

"""
Module contains an implementation of SONiC Platform Base API and
provides the hardware devices information which are available in the platform
"""

try:
    import re
    import os.path
    import binascii
    import glob
    from sonic_platform.plat_common import PlatCommon
    from sonic_platform.plat_common import CommonCfg
    from vendor_sonic_platform.device import DeviceCfg
    from vendor_sonic_platform import hooks
except ImportError as err:
    raise ImportError(str(err) + "- required module not found") from err


class Fru(object):
    """Platform-specific fru class"""

    def __init__(self, index, name, method="sysfs", ipmi_id=-1):
        """
        Fru initial

        Args:
            index: int, start from 0
            name: str, fru name
            method: str, eg. 'sysfs', 'restful', 'cache'
            ipmi_id: int, default is -1, only useful when ipmitool fru print
        """
        self.index = index
        self.name = name
        self.method = method
        self.ipmi_id = ipmi_id
        self.i2c_bus = None
        self.i2c_addr = None
        self.plat_common = PlatCommon(debug=CommonCfg.DEBUG)
        if self.method == CommonCfg.BY_SYSFS:
            self.__init_i2c_attr()
        else:
            if hasattr(DeviceCfg, "NEW_BMC_RESTFUL"):
                self.new_restful = DeviceCfg.NEW_BMC_RESTFUL
            else:
                # when call RESTful, default BMC RESTful V2.0
                self.new_restful = True

    def __init_i2c_attr(self):
        try:
            self.eeprom_sysfs_path = os.path.join(CommonCfg.S3IP_EXTEND_PATH,"fru", "{}_fru".format(self.name))
            self.eeprom_wp_sysfs_path = os.path.join(CommonCfg.S3IP_EXTEND_PATH, "fru", "{}_fru_wp".format(self.name))
            if os.path.isfile(self.eeprom_sysfs_path):
                eeprom_i2c_client_path = os.readlink(self.eeprom_sysfs_path)
                i2c_attr = eeprom_i2c_client_path.rsplit('/', 2)[1]
                self.i2c_bus = i2c_attr.split("-")[0]
                self.i2c_addr = "0x{}".format(i2c_attr.split("-")[1].lstrip("00"))
        except Exception as error:
            self.plat_common.log_error(str(error))

    def get_name(self):
        """
        Retrieves the name of the fru device

        Returns:
            string: The name of the fru device
        """
        return self.name

    def get_fru_info(self):
        """
        Retrieves the fru information

        Returns:
            dict
        """
        if self.method == CommonCfg.BY_SYSFS:
            return self.__get_fru_by_sysfs()

        if self.method in [CommonCfg.BY_IPMI, CommonCfg.BY_RESTFUL]:
            return self.__get_fru_by_ipmi()

        return None

    def get_presence(self):
        """
        Retrieves the presence of the fru

        Returns:
            bool: True if fru is present, False if not
        """
        return True

    def get_status(self):
        """
        Retrieves the operational status of the device

        Returns:
            A boolean value, True if device is operating properly, False if not
        """
        return self.get_presence()

    def write_fru(self, binary_file):
        """
        program the fru eeprom use input binary file.

        Args:
            binary_file: string
        Returns:
            A boolean value, True if programing success, False if not
        """
        if not os.path.exists(binary_file):
            self.plat_common.log_error("{} does not exist!".format(binary_file))
            return False

        if hasattr(hooks, "write_fru"):
            return hooks.write_fru(self.name, binary_file)

        if not self.set_write_protect(False):
            return False

        if self.method == CommonCfg.BY_SYSFS:
            return self.__write_fru_by_sysfs(binary_file)

        return self.__write_fru_by_restful(binary_file)

    def set_write_protect(self, enable):
        """
        Set the fru write protect control

        Args:
            enable: boolean, True for enable write protect.
        Returns:
            A boolean value, True if set success, False if not
        """
        if hasattr(hooks, "set_fru_write_protect"):
            return hooks.set_fru_write_protect(self.name, enable)

        if self.method == CommonCfg.BY_SYSFS:
            value = "1" if enable else "0"
            ret = self.plat_common.write_file(self.eeprom_wp_sysfs_path, value)
            return ret
        else:
            cmd = "fru_protect.sh {}".format("open" if enable else "close")
            ret, output = self.plat_common.exec_restful_rawcmd(cmd, self.new_restful)
            self.plat_common.log_info(output)
            if ret and "success" in output:
                return True
        return False

    def __get_fru_by_sysfs(self):
        """
        Retrieves the fru information

        Returns:
            dict
        """
        if self.i2c_bus is None or self.i2c_addr is None:
            return {}

        fru_dict = {}
        cmd = "frudump_eeprom -b {} -a {}".format(self.i2c_bus, self.i2c_addr)
        ret_code, fru_info = self.plat_common.exec_system_cmd(cmd)
        if ret_code == 0:
            try:
                for line in fru_info.splitlines():
                    kv = line.split(' : ')
                    if len(kv) != 2:
                        continue
                    fru_key = kv[0].strip()
                    fru_dict[fru_key] = kv[1].strip()
            except Exception as error:
                self.plat_common.log_error("parse {} fru fail:{}".format(self.name, str(error)))
        else:
            self.plat_common.log_error("dump {} fru fail!".format(self.name))

        return fru_dict

    def __get_fru_by_ipmi(self):
        """
        Retrieves the fru information

        Returns:
            dict
        """
        chassis_extra_index = 1
        board_extra_index = 1
        product_extra_index = 1
        fru_dict = {}
        cmd = "ipmitool fru print {}".format(self.ipmi_id)
        ret_code, fru_info = self.plat_common.exec_system_cmd(cmd)
        if ret_code == 0:
            try:
                for line in fru_info.split("\n"):
                    kv = line.split(' : ')
                    if len(kv) != 2:
                        continue
                    fru_key = kv[0].strip()
                    if "Chassis Extra" in fru_key:
                        fru_key = fru_key + " {}".format(chassis_extra_index)
                        chassis_extra_index += 1
                    elif "Board Extra" in fru_key:
                        fru_key = fru_key + " {}".format(board_extra_index)
                        board_extra_index += 1
                    elif "Product Extra" in fru_key:
                        fru_key = fru_key + " {}".format(product_extra_index)
                        product_extra_index += 1

                    fru_dict[fru_key] = kv[1].strip()
            except Exception as error:
                self.plat_common.log_error("parse {} fru fail:{}".format(self.name, str(error)))
        else:
            self.plat_common.log_error("dump {} fru fail!".format(self.name))

        return fru_dict

    def __write_fru_by_sysfs(self, binary_file):
        """ Program the fru eeprom by sysfs.

        Args:
            binary_file: string

        Returns:
            A boolean value, True if programing success, False if not
        """
        cmd = "cat {} > {}".format(binary_file, self.eeprom_sysfs_path)
        status, _ = self.plat_common.exec_system_cmd(cmd)
        return status == 0

    def __write_fru_by_restful(self, binary_file):
        """ Program the fru eeprom by restful.

        Args:
            binary_file: string

        Returns:
            A boolean value, True if programing success, False if not
        """
        bin_name = os.path.basename(binary_file)
        if not self.plat_common.upload_firmware_to_bmc(binary_file, self.new_restful):
            self.plat_common.log_error("upload {} to bmc failed".format(binary_file))
            return False

        cmd = "write_bmc_fru.sh {} {}".format(self.name, bin_name)
        ret, output = self.plat_common.exec_restful_rawcmd(cmd, self.new_restful)
        self.plat_common.log_info(output)

        return ret


class Bmc(object):
    """Platform-specific BMC class"""

    def __init__(self):
        self.plat_common = PlatCommon(debug=CommonCfg.DEBUG)
        self.diag_case_dict = {
                                'memory': 'm',
                                'rtc': 'r',
                                'cpu': 'c',
                                'gpio': 'g',
                                'i2c': 's',
                                'emmc': 'e',
                                'spi': 'p',
                                'mdio': 'd'
                            }
        if hasattr(DeviceCfg, "NEW_BMC_RESTFUL"):
            self.new_restful = DeviceCfg.NEW_BMC_RESTFUL
        else:
            # when call RESTful, default BMC RESTful V2.0
            self.new_restful = True

    def reboot_bmc(self):
        """ Reboot BMC

        Return:
            boolean, return True if response is ok, False if not
        """
        cmd = "reboot"
        ret, _ = self.plat_common.exec_restful_rawcmd(cmd, self.new_restful, resp_required=False)
        if not ret:
            return False
        return True

    def exec_raw_cmd(self, command):
        """ Run command under BMC OS

        Args:
            command: str

        Return:
            boolean, return True if response is ok, False if not
            string, command output
        """
        ret, output = self.plat_common.exec_restful_rawcmd(command, self.new_restful)
        return ret, output

    def get_diag_case_list(self):
        """ Get BMC diagnosis case list

        Return:
            list, bmc diagnosis case list
        """
        return self.diag_case_dict.keys()

    def exec_diag_case(self, case=None):
        """ Run bmc test cases

        Args:
            case name: str, if not specified case name, then run all cases.

        Return:
            boolean, return True if response is ok, False if not
            string, case output
        """
        if case is None:
            cmd = "base_test -a"
        elif case in self.diag_case_dict.keys():
            cmd = "base_test -{}".format(self.diag_case_dict[case])
        else:
            return False, "Not Support"

        ret, out = self.exec_raw_cmd(cmd)
        return ret, out


class MgmtPort(object):
    """Manufacture-specific MgmtPort class"""

    def __init__(self):
        self.iface = "eth0"
        self.plat_common = PlatCommon(debug=CommonCfg.DEBUG)

    def get_name(self):
        """
        Retrieves the name of the device

        Returns:
            string: The name of the device
        """
        return self.iface

    def get_status(self):
        """ Retrieves the management port running status

        Return:
            dict, running status of management port
            eg. {
            "Link": True/False,
            "Firmware": "3.25",
            "Speed": "1000" or "100" or "10",
            "Duplex": "full" or "half",
            "AN": True/False,
            }
        """
        iface_info = {}

        cmd = "ethtool {}".format(self.iface)
        _, output = self.plat_common.exec_system_cmd(cmd)
        for line in output.splitlines():
            if re.search("Link detected", line):
                status = line.split(':')[1].strip()
                if status == "yes":
                    iface_info["Link"] = True
                else:
                    iface_info["Link"] = False
            if re.search("Auto-negotiation", line):
                status = line.split(':')[1].strip()
                if status == "on":
                    iface_info["AN"] = True
                else:
                    iface_info["AN"] = False
            if re.search("Duplex", line):
                iface_info["Duplex"] = line.split(':')[1].strip().lower()
            if re.search("Speed", line):
                iface_info["Speed"] = line.split(':')[1].strip("Mb/s").strip()

        cmd = "ethtool -i {}".format(self.iface)
        _, output = self.plat_common.exec_system_cmd(cmd)
        for line in output.splitlines():
            if re.search("firmware-version", line):
                iface_info["Firmware"] = line.split(":")[1].split(',')[0].strip()
                break

        return iface_info

    def get_error_counter(self):
        """ Read eth0 counter statistics, find error

        Return:
            boolean, True for fcs/crc occured
            int, number of fcs/crc error counter
        """
        ret = False
        count = 0
        cmd = "ethtool -S eth0 | grep crc"
        _, output = self.plat_common.exec_system_cmd(cmd)
        try:
            for line in output.splitlines():
                count += int(line.split(":")[1])
                if count > 0:
                    ret = True
        except Exception as error:
            self.plat_common.log_error(str(error))
        return ret, count

class VoltageRegulator(object):
    """Platform-specific Voltage Regulator class"""

    def __init__(self, index, name, method):
        """
        Voltage Regulator initial

        Args:
            index: int, start from 0
            name: string, the vr name
            method: string, eg. 'sysfs', 'restful', 'cache'
        """
        self.index = index + 1
        self.sysfs_path = None
        self.plat_comm = PlatCommon(debug=CommonCfg.DEBUG)
        self.name = name
        self.method = method
        if self.method == CommonCfg.BY_SYSFS:
            self.sysfs_path = os.path.join(CommonCfg.S3IP_VR_PATH, "vr{}".format(self.index))
        if self.method == CommonCfg.BY_RESTFUL:
            if hasattr(DeviceCfg, "NEW_BMC_RESTFUL"):
                self.new_restful = DeviceCfg.NEW_BMC_RESTFUL
            else:
                # when call RESTful, default BMC RESTful V2.0
                self.new_restful = True

    def __get_file_path(self, file_name):
        if self.sysfs_path is not None:
            return os.path.join(self.sysfs_path, file_name)
        return None

    def get_name(self):
        """
        Retrieves the name of the vr

        Returns:
            A string containing the name of the vr
        """
        return self.name

    def get_model(self):
        """
        Retrieves the model number (or part number) of the device

        Returns:
            string: Model/part number of device
        """
        if hasattr(hooks, "get_vr_type"):
            return hooks.get_vr_type(self.name)

        if self.method == CommonCfg.BY_SYSFS:
            model = self.plat_comm.read_file(self.__get_file_path("type"))
            if self.plat_comm.is_valid_value(model):
                return model
        return "VR"

    def get_firmware_version(self):
        """
        Retrieves the firmware version of the vr

        Note: the firmware version will be read from HW

        Returns:
            A string containing the firmware version of the vr
        """
        if hasattr(hooks, "get_vr_version"):
            return hooks.get_vr_version(self.name)

        if self.method == CommonCfg.BY_SYSFS:
            fw_version = self.plat_comm.read_file(self.__get_file_path("version"))
            if self.plat_comm.is_valid_value(fw_version):
                return fw_version

        if self.method == CommonCfg.BY_RESTFUL:
            cmd = "vr-util --version {}".format(self.name)
            status, output = self.plat_comm.exec_restful_rawcmd(cmd, self.new_restful)
            if status and output:
                try:
                    version = output.split(":")[1].strip()
                except Exception as serr:
                    self.plat_comm.log_error(str(serr))
            return version
        return CommonCfg.NULL_VALUE


class Led(object):
    """Manufacture-specific LED class"""
    panel_led_list = ["sys", "psu", "fan", "bmc", "id"]
    def __init__(self, index, name, method):
        """
        LED initial

        Args:
            index: int, start from 0
            name: str, the LED name, include 'sys', 'psu', 'fan', 'bmc'
            method: str, eg. 'sysfs', 'restful', 'cache'
        """
        self.index = index + 1
        self.panel_led_path = None
        self.name = name
        self.method = method
        self.plat_comm = PlatCommon(debug=CommonCfg.DEBUG)
        self.port_led_path = os.path.join(CommonCfg.S3IP_SFP_PATH, "eth*", "led_status")
        if self.method == CommonCfg.BY_SYSFS:
            self.panel_led_path = CommonCfg.S3IP_LED_PATH

    def __get_file_path(self, file_name):
        if self.panel_led_path is not None:
            return os.path.join(self.panel_led_path, file_name)
        return None

    def get_name(self):
        """
        Retrieves the name of the panel led

        Returns:
            A string containing the name of the panel led
        """
        return self.name

    def set_led_status(self, color):
        """
        Set panel system led color

        Args:
            color: str, 'red', 'green', 'yellow', 'off'

        Returns:
            boolean
        """
        value = CommonCfg.COLOR_MAP.get(color)
        if value is None:
            self.plat_comm.log_error("invalid color [{}] for {}!".format(color, self.name))
            return False

        if self.name == "port":
            ret = True
            port_number = self.plat_comm.read_file(os.path.join(CommonCfg.S3IP_SFP_PATH, "number"))
            if not self.plat_comm.is_valid_value(port_number):
                return False

            for file in glob.glob(self.port_led_path):
                ret &= self.plat_comm.write_file(file, value)
            return ret
        elif self.name in Led.panel_led_list:
            if self.method == CommonCfg.BY_SYSFS:
                led_path = self.__get_file_path("{}_led_status".format(self.name))
                return self.plat_comm.write_file(led_path, value)
            else:
                restful_led_map = {
                    "sys": "system",
                    "psu": "system_power",
                    "fan": "system_fan",
                    "id":  "location",
                    "bmc": "bmc"
                }
                rawcmd = "led-util --set_info {0} {1} 0".format(restful_led_map[self.name], color)
                ret, out = self.plat_comm.exec_restful_rawcmd(rawcmd)
                if ret and "success" in out:
                    return True

        return False

    def get_led_status(self):
        """
        Gets panel system led color

        Returns:
            A string for led color
        """
        if self.name in Led.panel_led_list:
            if self.method == CommonCfg.BY_SYSFS:
                led_path = self.__get_file_path("{}_led_status".format(self.name))
                raw_value = self.plat_comm.read_file(led_path)
                if self.plat_comm.is_valid_value(raw_value):
                    for color, value in CommonCfg.COLOR_MAP.items():
                        if value == int(raw_value):
                            return color
            else:
                restful_led_map = {
                    "sys": "system_led",
                    "psu": "system_power_led",
                    "fan": "system_fan_led",
                    "id":  "location_led",
                    "bmc": "bmc_led"
                }
                rawcmd = "led-util --get_info | grep {}".format(restful_led_map[self.name])
                ret, out = self.plat_comm.exec_restful_rawcmd(rawcmd)
                if ret and out:
                    return out.split(":")[1].strip()
        return CommonCfg.NULL_VALUE

    def set_led_control_enable(self, enable):
        """
        Set the led control mode

        Args:
            enable: boolean, True for enable software control mode.
        Returns:
            A boolean value, True if set success, False if not
        """
        value = "1" if enable else "0"
        if self.name == "port":
            port_control_path = os.path.join(CommonCfg.S3IP_EXTEND_PATH, "system/port_control_*")
            ret = True
            for file in glob.glob(port_control_path):
                ret &= self.plat_comm.write_file(file, value)
            return ret
        elif self.name in Led.panel_led_list:
            if self.method == CommonCfg.BY_SYSFS:
                led_ctrl_en_path = self.__get_file_path("{}_led_ctrl_en".format(self.name))
                return self.plat_comm.write_file(led_ctrl_en_path, value)
            else:
                # when bmc control led, default return True
                return True

        return False


class Eload(object):
    """Manufacture-specific Eload class"""

    def __init__(self):
        self.plat_comm = PlatCommon(CommonCfg.DEBUG)

    def _convert_hex_to_string(self, arr, start, end):
        try:
            ret_str = ''
            for n in range(start, end):
                ret_str += arr[n]
            return str.strip(binascii.unhexlify(ret_str))
        except Exception as error:
            return str(error)

    def get_id(self, sfp_obj):
        """
        Retrieves the identifier of the Eload

        Args:
            sfp_obj: A object of chassis Sfp

        Returns:
            A integer containing the identifier of the Eload
        """
        if sfp_obj is None:
            return 0

        id_byte_raw = sfp_obj.read_eeprom(0, 1)
        if id_byte_raw is None:
            return None
        return id_byte_raw[0]

    def get_serial(self, sfp_obj):
        """
        Retrieves the serial number of the Eload

        Args:
            sfp_obj: A object of chassis Sfp

        Returns:
            A string, the serial number of the xcvr

            If there is an issue with reading the xcvr, None should be returned.
        """
        if sfp_obj is None:
            return CommonCfg.NULL_VALUE

        return sfp_obj.get_serial()

    def get_vendor_name(self, sfp_obj):
        """
        Retrieves the vendor name of the Eload

        Args:
            sfp_obj: A object of chassis Sfp

        Returns:
            A string containing the identifier of the Eload
        """
        if sfp_obj is None:
            return "N/A"

        vendor_name_width = 16
        try:
            oid = self.get_id(sfp_obj)
            if oid in [0x11, 0x0d]: # QSFP56, QSFP112
                offset = 148
            elif oid in [0x18, 0x1b]: # QSFP-DD, DSFP
                offset = 129
            elif oid == 0x03: # SFP56
                offset = 20
            else:
                self.plat_comm.log_warning("Unsupported Eload identifier {}".format(oid))
                return None

            vendor_name_raw = sfp_obj.read_eeprom(offset, vendor_name_width)
            vendor_name = self._convert_hex_to_string(vendor_name_raw, 0, vendor_name_width)
            return vendor_name
        except Exception as error:
            self.plat_comm.log_error(str(error))
        return None

    def get_eeprom_reset_status(self, sfp_obj):
        """
        Retrieves the Eload reset status by access its eeprom register

        Args:
            sfp_obj: A object of chassis Sfp

        Returns:
            A Boolean, True if reset enabled, False if disabled
        """
        if sfp_obj is None:
            return False

        try:
            oid = self.get_id(sfp_obj)
            if oid in [0x11, 0x0d]: # QSFP56, QSFP112
                offset = 29
            elif oid in [0x18, 0x1b]: # QSFP-DD, DSFP
                offset = 19
            else:
                return False
            eeprom_raw = sfp_obj.read_eeprom(offset, 1)
            if eeprom_raw is not None:
                if eeprom_raw[0] & 0x04 == 0:
                    return True
        except Exception as error:
            self.plat_comm.log_error(str(error))

        return False

    def get_eeprom_lpmode(self, sfp_obj):
        """
        Retrieves the Eload lpmode status by access its eeprom register

        Args:
            sfp_obj: A object of chassis Sfp

        Returns:
            A Boolean, True if lpmode enabled, False if disabled
        """
        if sfp_obj is None:
            return False

        try:
            oid = self.get_id(sfp_obj)
            if oid in [0x11, 0x0d]: # QSFP56, QSFP112
                offset = 29
            elif oid in [0x18, 0x1b]: # QSFP-DD, DSFP
                offset = 19
            else:
                return False
            eeprom_raw = sfp_obj.read_eeprom(offset, 1)
            if eeprom_raw is not None:
                if eeprom_raw[0] & 0x08:
                    return True
        except Exception as error:
            self.plat_comm.log_error(str(error))

        return False

    def set_eeprom_interrupt(self, sfp_obj, interrupt):
        """
        Set the Eload interrupt control by access its eeprom register

        Args:
            sfp_obj: A object of chassis Sfp
            interrupt: A Boolean, True if enable interrupt

        Returns:
            A Boolean, True if interrupt setting success, False if failed
        """
        if sfp_obj is None:
            return False

        try:
            oid = self.get_id(sfp_obj)
            if oid in [0x11, 0x0d]: # QSFP56, QSFP112
                offset = 29
            elif oid in [0x18, 0x1b]: # QSFP-DD, DSFP
                offset = 19
            else:
                return False

            eeprom_raw = sfp_obj.read_eeprom(offset, 1)
            if eeprom_raw is not None:
                if interrupt: # bit 1
                    raw_value = eeprom_raw[0] & 0xFD
                else:
                    raw_value = eeprom_raw[0] | 0x02
                arr = bytearray([raw_value])
                return sfp_obj.write_eeprom(offset, len(arr), arr)
        except Exception as error:
            self.plat_comm.log_error(str(error))

        return False

    def get_eeprom_tx_disable(self, sfp_obj):
        """
        Retrieves the Eload tx disable status by access its eeprom register

        Args:
            sfp_obj: A object of chassis Sfp

        Returns:
            A Boolean, True if tx disable enabled, False if disabled
        """
        if sfp_obj is None:
            return False

        try:
            oid = self.get_id(sfp_obj)
            if oid == 0x03: # SFP56
                offset = 256 + 0xFA * 128 + 0xAE
            else:
                return False
            eeprom_raw = sfp_obj.read_eeprom(offset, 1)
            if eeprom_raw is not None:
                if eeprom_raw[0] & 0x08:
                    return True
        except Exception as error:
            self.plat_comm.log_error(str(error))

        return False

    def set_eeprom_tx_fault(self, sfp_obj, tx_fault):
        """
        Set the Eload tx fault control by access its eeprom register

        Args:
            sfp_obj: A object of chassis Sfp
            tx_fault: A Boolean, True if enable tx_fault

        Returns:
            A Boolean, True if tx-fault setting success, False if failed
        """
        if sfp_obj is None:
            return False

        value = 1 if tx_fault else 0
        arr = bytearray([value])
        try:
            oid = self.get_id(sfp_obj)
            if oid == 0x03: # SFP56
                offset = 256 + 0xFA * 128 + 0xC0
            else:
                return False
            return sfp_obj.write_eeprom(offset, len(arr), arr)
        except Exception as error:
            self.plat_comm.log_error(str(error))

        return False

    def set_eeprom_rx_los(self, sfp_obj, rx_los):
        """
        Set the Eload rx-los control by access its eeprom register

        Args:
            sfp_obj: A object of chassis Sfp
            rx_los: A Boolean, True if enable rx_los

        Returns:
            A Boolean, True if rx-los setting success, False if failed
        """
        if sfp_obj is None:
            return False

        value = 1 if rx_los else 0
        arr = bytearray([value])
        try:
            oid = self.get_id(sfp_obj)
            if oid == 0x03: # SFP56
                offset = 256 + 0xFA * 128 + 0xBE
            else:
                return False
            return sfp_obj.write_eeprom(offset, len(arr), arr)
        except Exception as error:
            self.plat_comm.log_error(str(error))

        return False

    def set_eeprom_power_control(self, sfp_obj, value):
        """
        Set the Eload power control by access its eeprom register

        Args:
            sfp_obj: A object of chassis Sfp
            value: A Int, register value for power control

        Returns:
            A Boolean, True if power setting success, False if failed
        """
        if sfp_obj is None:
            return False

        try:
            oid = self.get_id(sfp_obj)
            if oid in [0x11, 0x0d]: # QSFP56, QSFP112
                offset = 256 + 139
            elif oid in [0x18, 0x1b]: # QSFP-DD, DSFP
                offset = 200
            elif oid == 0x03: # SFP56
                offset = 256 + 0xFA * 128 + 0xC8
            else:
                self.plat_comm.log_warning("Unsupported Eload identifier {}".format(oid))
                return False

            arr = bytearray([value])
            return sfp_obj.write_eeprom(offset, len(arr), arr)
        except Exception as error:
            self.plat_comm.log_error(str(error))

        return None
