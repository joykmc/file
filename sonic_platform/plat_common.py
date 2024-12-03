# -*- coding: UTF-8 -*-

try:
    import os
    import requests
    import json
    import subprocess
    import syslog
    import random
    import time
    import fcntl
    import urllib3
    from sonic_py_common import device_info
    from urllib3.exceptions import InsecureRequestWarning
    urllib3.disable_warnings(InsecureRequestWarning)
except ImportError as e:
    raise ImportError(str(e) + "- required module not found") from e


class CommonCfg(object):
    DEBUG                          =                                   False
    RETRY_MAX_CNT                  =                                       3
    RETRY_INTERVAL_BASE            =                                       3

    """ platform api supported access-method """
    BY_SYSFS                       =                                 "sysfs"
    BY_RESTFUL                     =                               "restful"
    BY_CACHE                       =                                 "cache"
    BY_IPMI                        =                                  "ipmi"

    """ bmc new restful api V2.0 """
    BMC_DEFAULT_IP                 =                               "240.1.1.1"
    ROOT_URL                       =               "https://" + BMC_DEFAULT_IP
    BMC_RESTFUL_VER_API            = ROOT_URL + "/api/common/bmc/rest_version"
    BMC_VERSION_GET_API            =     ROOT_URL + "/api/common/bmc/versions"
    BMC_BOOTINFO_GET_API           =     ROOT_URL + "/api/common/bmc/bootinfo"
    BMC_BOOT_SET_API               =     ROOT_URL + "/api/common/bmc/nextboot"
    CPLD_LIST_GET_API              =        ROOT_URL + "/api/common/cpld/list"
    CPLD_VERSION_GET_API           =     ROOT_URL + "/api/common/cpld/version"
    CPLD_VERSIONS_GET_API          =    ROOT_URL + "/api/common/cpld/versions"
    FANTRAY_NUM_GET_API            =      ROOT_URL + "/api/common/fantray/num"
    FANTRAY_PRESENCE_GET_API       = ROOT_URL + "/api/common/fantray/presence"
    FANTRAY_INFO_GET_API           =     ROOT_URL + "/api/common/fantray/info"
    FANTRAY_SPEED_GET_API          =    ROOT_URL + "/api/common/fantray/speed"
    FANTRAY_LED_GET_API            =      ROOT_URL + "/api/common/fantray/led"
    PSU_PRESENCE_GET_API           =     ROOT_URL + "/api/common/psu/presence"
    PSU_INFO_GET_API               =         ROOT_URL + "/api/common/psu/info"
    PSU_STATUS_GET_API             =       ROOT_URL + "/api/common/psu/status"
    PSU_PWR_STATUS_GET_API         = ROOT_URL + "/api/common/psu/power_status"
    SENSOR_ALL_INFO_GET_API        =       ROOT_URL + "/api/common/sensor/all"
    SENSOR_ONE_INFO_GET_API        =          ROOT_URL + "/api/common/sensor/"
    BMC_UPLOAD_API                 =           ROOT_URL + "/api/common/upload"
    BMC_UPGRADE_API                = ROOT_URL + "/api/common/firmware/upgrade"
    BMC_CPLD_UPGRADE_API           =     ROOT_URL + "/api/common/cpld/upgrade"
    FAN_LED_API                    =     ROOT_URL + "/api/common/test/fan/led"
    FAN_MODE_API                   =    ROOT_URL + "/api/common/test/fan/mode"
    FAN_SPEED_API                  =   ROOT_URL + "/api/common/test/fan/speed"
    PSU_FAN_SPEED_API              = ROOT_URL + "/api/common/test/psu/fanctrl"
    BMC_RAWCMD_API                 =        ROOT_URL + "/api/common/hw/rawcmd"
    BMC_POST_DATA_API              =    ROOT_URL + "/api/common/test/postdata"

    """ old bmc restful api V1.0 """
    OLD_BMC_RAWCMD_API             =               ROOT_URL + "/api/hw/rawcmd"
    OLD_BMC_UPLOAD_API             =          ROOT_URL + "/api/fwimage_upload"
    OLD_BMC_UPGRADE_API            =        ROOT_URL + "/api/firmware/upgrade"
    OLD_BMC_CPLD_UPGRADE_API       =            ROOT_URL + "/api/cpld/upgrade"
    OLD_BMC_RAWCMD_API             =               ROOT_URL + "/api/hw/rawcmd"

    """ s3ip sysfs file DIR """
    S3IP_ROOT_DIR                  =                            "/sys_switch"
    S3IP_TEMP_DIR                  =                           "/temp_sensor"
    S3IP_VOLT_DIR                  =                            "/vol_sensor"
    S3IP_CURR_DIR                  =                           "/curr_sensor"
    S3IP_SFP_DIR                   =                           "/transceiver"
    S3IP_EEPROM_DIR                =                             "/syseeprom"
    S3IP_WTD_DIR                   =                              "/watchdog"
    S3IP_LED_DIR                   =                                "/sysled"
    S3IP_CPLD_DIR                  =                                  "/cpld"
    S3IP_FPGA_DIR                  =                                  "/fpga"
    S3IP_SLOT_DIR                  =                                  "/slot"
    S3IP_FAN_DIR                   =                                   "/fan"
    S3IP_VR_DIR                    =                                    "/vr"
    S3IP_PSU_DIR                   =                                   "/psu"
    S3IP_MOTOR_DIR                 =                                 "/motor"
    S3IP_EXTEND_DIR                =                                "/extend"
    S3IP_FW_CTL_DIR                =                                "/fw_ctl"
    S3IP_JTAG_GPIO_DIR             =                           "/jtag_gpio{}"          

    S3IP_SFP_PATH                  =             S3IP_ROOT_DIR + S3IP_SFP_DIR
    S3IP_TEMP_PATH                 =            S3IP_ROOT_DIR + S3IP_TEMP_DIR
    S3IP_VOLT_PATH                 =            S3IP_ROOT_DIR + S3IP_VOLT_DIR
    S3IP_CURR_PATH                 =            S3IP_ROOT_DIR + S3IP_CURR_DIR
    S3IP_EEPROM_PATH               =          S3IP_ROOT_DIR + S3IP_EEPROM_DIR
    S3IP_WTD_PATH                  =             S3IP_ROOT_DIR + S3IP_WTD_DIR
    S3IP_LED_PATH                  =             S3IP_ROOT_DIR + S3IP_LED_DIR
    S3IP_CPLD_PATH                 =            S3IP_ROOT_DIR + S3IP_CPLD_DIR
    S3IP_FPGA_PATH                 =            S3IP_ROOT_DIR + S3IP_FPGA_DIR
    S3IP_SLOT_PATH                 =            S3IP_ROOT_DIR + S3IP_SLOT_DIR
    S3IP_FAN_PATH                  =             S3IP_ROOT_DIR + S3IP_FAN_DIR
    S3IP_VR_PATH                   =              S3IP_ROOT_DIR + S3IP_VR_DIR
    S3IP_PSU_PATH                  =             S3IP_ROOT_DIR + S3IP_PSU_DIR
    S3IP_EXTEND_PATH               =          S3IP_ROOT_DIR + S3IP_EXTEND_DIR
    S3IP_FW_CTL_PATH               =        S3IP_EXTEND_PATH+ S3IP_FW_CTL_DIR
    S3IP_JATG_GPIO_PATH            =    S3IP_FW_CTL_PATH + S3IP_JTAG_GPIO_DIR

    """ s3ip sysfs BIOS update sysfs """
    S3IP_BIOS_UPDATE_EN_FILE       =                       "/bios_boot_flash_select_en"
    S3IP_BIOS_MASTER_WP_FILE       =                                 "/primary_bios_wp"
    S3IP_BIOS_SLAVE_WP_FILE        =                               "/secondary_bios_wp"
    S3IP_BIOS_UP_FLASH_SEL_FILE    =                          "/bios_boot_flash_select"
    S3IP_BIOS_UPDATE_EN_PATH       =       S3IP_FW_CTL_PATH + S3IP_BIOS_UPDATE_EN_FILE
    S3IP_BIOS_MASTER_WP_PATH       =       S3IP_FW_CTL_PATH + S3IP_BIOS_MASTER_WP_FILE
    S3IP_BIOS_SLAVE_WP_PATH        =        S3IP_FW_CTL_PATH + S3IP_BIOS_SLAVE_WP_FILE
    S3IP_BIOS_UP_FLASH_SEL_PATH    =    S3IP_FW_CTL_PATH + S3IP_BIOS_UP_FLASH_SEL_FILE

    S3IP_CPLD_UPDATE_ENABLE_FILE   =                             "/cpld_update_enable"
    S3IP_CPLD_UPDATE_ENABLE_PATH   =  S3IP_FW_CTL_PATH +  S3IP_CPLD_UPDATE_ENABLE_FILE

    """ system command """
    BMC_VERSION_IPMI_CMD           =                "ipmitool raw 0x06 0x01"
    BIOS_VERSION_DMI_CMD           =             "dmidecode -s bios-version"

    """ other sysfs file DIR """
    BIOS_VERSION_SYSFS_PATH        =        "/sys/class/dmi/id/bios_version"
    GPIO_SYSFS_PATH                =                "/sys/class/gpio/gpio{}"

    """ constant definitions """
    NULL_VALUE                     =                                    "N/A"

    LED_COLOR_DARK                 =                                      0
    LED_COLOR_GREEN                =                                      1
    LED_COLOR_YELLOW               =                                      2
    LED_COLOR_RED                  =                                      3
    LED_COLOR_BLUE                 =                                      4
    LED_COLOR_GREEN_FLASHING       =                                      5
    LED_COLOR_YELLOW_FLASHING      =                                      6
    LED_COLOR_RED_FLASHING         =                                      7
    LED_COLOR_BLUE_FLASHING        =                                      8

    COMPONENT_TYPE_BIOS            =                                 "bios"
    COMPONENT_TYPE_BMC             =                                  "bmc"
    COMPONENT_TYPE_CPLD            =                                 "cpld"
    COMPONENT_TYPE_FPGA            =                                 "fpga"
    COMPONENT_TYPE_UBOOT           =                                "uboot"
    COMPONENT_TYPE_L2SWITCH        =                             "l2switch"

    CPLD_NAME_KEY                  =                                 "name"
    CPLD_INDEX_KEY                 =                           "cpld_index"
    CPLD_JTAG_GROUP_KEY            =                           "jtag_group"
    CPLD_JTAG_CHANNEL_SEL_KEY      =                        "jtag_chan_sel"
    CPLD_JTAG_CPLD_SEL_KEY         =                        "jtag_cpld_sel"

    TEMPERATURE_FACTOR             =                                 1000.0
    VOLTAGE_FACTOR                 =                                 1000.0
    CURRENT_FACTOR                 =                                 1000.0
    POWER_FACTOR                   =                              1000000.0

    """ Possible fan directions (relative to port-side of device) """
    FAN_DIRECTION_B2F_VAL          =                                      1
    FAN_DIRECTION_F2B_VAL          =                                      0
    FAN_DIRECTION_B2F_STR          =                                  "B2F"
    FAN_DIRECTION_F2B_STR          =                                  "F2B"
    FAN_DEFAULT_TOLERANCE          =                                     30
    FAN_DIRECTION_MAP              = {
        FAN_DIRECTION_F2B_VAL : FAN_DIRECTION_F2B_STR,
        FAN_DIRECTION_B2F_VAL : FAN_DIRECTION_B2F_STR
    }

    COLOR_MAP                      = {
        "green" : LED_COLOR_GREEN,
        "amber" : LED_COLOR_YELLOW,
        "yellow": LED_COLOR_YELLOW,
        "red"   : LED_COLOR_RED,
        "blue"  : LED_COLOR_BLUE,
        "off"   : LED_COLOR_DARK,
        "green-flashing": LED_COLOR_GREEN_FLASHING,
        "amber-flashing": LED_COLOR_YELLOW_FLASHING,
        "red-flashing"  : LED_COLOR_RED_FLASHING,
        "blue-flashing" : LED_COLOR_BLUE_FLASHING,
    }
    FIRMWARE_ID_DICT = {
        1: "BMC",
        2: "BIOS",
        3: "CPLD"
    }

class Logger(object):
    """
    Logger class for SONiC Python applications
    """
    LOG_FACILITY_DAEMON = syslog.LOG_DAEMON
    LOG_FACILITY_USER = syslog.LOG_USER

    LOG_OPTION_NDELAY = syslog.LOG_NDELAY
    LOG_OPTION_PID = syslog.LOG_PID

    LOG_PRIORITY_ERROR = syslog.LOG_ERR
    LOG_PRIORITY_WARNING = syslog.LOG_WARNING
    LOG_PRIORITY_NOTICE = syslog.LOG_NOTICE
    LOG_PRIORITY_INFO = syslog.LOG_INFO
    LOG_PRIORITY_DEBUG = syslog.LOG_DEBUG

    DEFAULT_LOG_FACILITY = LOG_FACILITY_USER
    DEFAULT_LOG_OPTION = LOG_OPTION_NDELAY

    def __init__(self, log_facility=DEFAULT_LOG_FACILITY, log_option=DEFAULT_LOG_OPTION):
        self._syslog = syslog

        # Initialize syslog
        self._syslog.openlog(logoption=log_option, facility=log_facility)

        # Set the default minimum log priority to LOG_PRIORITY_NOTICE
        self.set_min_log_priority(self.LOG_PRIORITY_NOTICE)

    def __del__(self):
        try:
            self._syslog.closelog()
        except TypeError:
            pass

    #
    # Methods for setting minimum log priority
    #

    def set_min_log_priority(self, priority):
        """
        Sets the minimum log priority level to <priority>. All log messages
        with a priority lower than <priority> will not be logged

        Args:
            priority: The minimum priority at which to log messages
        """
        self._min_log_priority = priority

    def set_min_log_priority_error(self):
        """
        Convenience function to set minimum log priority to LOG_PRIORITY_ERROR
        """
        self.set_min_log_priority(self.LOG_PRIORITY_ERROR)

    def set_min_log_priority_warning(self):
        """
        Convenience function to set minimum log priority to LOG_PRIORITY_WARNING
        """
        self.set_min_log_priority(self.LOG_PRIORITY_WARNING)

    def set_min_log_priority_notice(self):
        """
        Convenience function to set minimum log priority to LOG_PRIORITY_NOTICE
        """
        self.set_min_log_priority(self.LOG_PRIORITY_NOTICE)

    def set_min_log_priority_info(self):
        """
        Convenience function to set minimum log priority to LOG_PRIORITY_INFO
        """
        self.set_min_log_priority(self.LOG_PRIORITY_INFO)

    def set_min_log_priority_debug(self):
        """
        Convenience function to set minimum log priority to LOG_PRIORITY_DEBUG
        """
        self.set_min_log_priority(self.LOG_PRIORITY_DEBUG)

    #
    # Methods for logging messages
    #

    def log(self, priority, msg, also_print_to_console=False):
        if self._min_log_priority >= priority:
            # Send message to syslog
            self._syslog.syslog(priority, msg)

        # Send message to console
        if also_print_to_console:
            print(msg)

    def log_error(self, msg, also_print_to_console=False):
        self.log(self.LOG_PRIORITY_ERROR, msg, also_print_to_console)

    def log_warning(self, msg, also_print_to_console=False):
        self.log(self.LOG_PRIORITY_WARNING, msg, also_print_to_console)

    def log_notice(self, msg, also_print_to_console=False):
        self.log(self.LOG_PRIORITY_NOTICE, msg, also_print_to_console)

    def log_info(self, msg, also_print_to_console=False):
        self.log(self.LOG_PRIORITY_INFO, msg, also_print_to_console)

    def log_debug(self, msg, also_print_to_console=False):
        self.log(self.LOG_PRIORITY_DEBUG, msg, also_print_to_console)


class PlatCommon(Logger):
    SENSOR_CACHE_FILE = "/var/run/platform_cache/sensor_rev.json"
    PSU_CACHE_FILE = "/var/run/platform_cache/psu_rev.json"
    FAN_CACHE_FILE = "/var/run/platform_cache/fan_rev.json"

    def __init__(self, debug=False):
        Logger.__init__(self)

        if debug is True:
            self.set_min_log_priority_info()

    def is_float(self, val):
        """ Verify value type is float or not

        Args:
            val: any value

        Returns:
            Boolean: True for float
        """
        try:
            float(val)
            if str(val) in ['inf', 'infinity', 'INF', 'INFINITY',
                            'True', 'NAN', 'nan', 'False',
                            '-inf', '-INF', '-INFINITY', '-infinity',
                            'NaN', 'Nan']:
                return False
            return True
        except (TypeError, ValueError):
            return False

    def is_valid_value(self, val):
        """ Verify value is valid or not

        Args:
            val: any value

        Returns:
            Boolean: True for valie
        """
        if val in ["", "NA", "N/A", None, -99999.0]:
            return False
        return True

    def read_file(self, file_path):
        """ Read file

        Args:
            file_path:str, full path of sysfs file

        Returns:
            String: return file content
        """
        try:
            if not os.path.exists(file_path):
                return None

            with open(file_path, 'r', encoding="utf-8") as filed:
                data = filed.read().strip()
        except IOError as error:
            self.log_error("read {} error:{}".format(file_path, str(error)))
            data = None

        self.log_info("read {} content={}.".format(file_path, data))
        return data

    def write_file(self, file_path, value):
        """ Write sysfs file

        Args:
            file_path:str, full path of sysfs file
            value:int,float,or str

        Returns:
            Boolean: True if write file successfully, False if not
        """
        try:
            if not os.path.exists(file_path) or value is None:
                return False

            with open(file_path, 'w', encoding="utf-8") as filed:
                filed.write(str(value))
        except IOError as error:
            self.log_error("write {} failed:{}".format(file_path, str(error)))
            return False

        return True

    def request_get(self, url, header=None, timeout=120):
        """ Call restful get interface and parse the return results (RESTful V2.0)

        Args:
            url: str, url address
            header: dict-str
            timeout: int

        Returns:
            dict
        """
        for _ in range(CommonCfg.RETRY_MAX_CNT):
            try:
                response = requests.get(url, headers=header, timeout=timeout, verify=False)
                self.log_info("request {}, header={}, response={}&{}".format(url,
                                                                              header,
                                                                              response.status_code,
                                                                              response.text))
                if response.status_code == 200 and self.is_response_success(response.text):
                    return json.loads(response.text).get('data')
            except Exception as error:
                self.log_notice("request {} get:{}".format(url, str(error)))
            time.sleep(CommonCfg.RETRY_INTERVAL_BASE + random.random())

        return None

    def request_post(self, url, header, data, new_restful=True, timeout=None, resp_required=True):
        """ Call restful post interface and parse the return results

        Args:
            url: str, address
            header: dict-str
            data: dict-str
            new_restful: bool, default True for RESTful V2.0
            timeout: int
            resp_required: boolean

        Returns:
            Boolean: return True if response is ok, False if not
            obj: response text
        """
        for _ in range(CommonCfg.RETRY_MAX_CNT):
            try:
                response = requests.post(url, headers=header, data=data, timeout=timeout, verify=False)
                self.log_info("request {}, header={}, data={}, response={}&{}".format(url,
                                                                                       header,
                                                                                       data,
                                                                                       response.status_code,
                                                                                       response.text))
                if not resp_required:
                    return True, None
                if response.status_code == 200 and self.is_response_success(response.text):
                    if new_restful:
                        return True, json.loads(response.text).get('data')
                    else:
                        return True, json.loads(response.text).get('description')
            except Exception as error:
                self.log_notice("request {} get:{}".format(url, str(error)))
            time.sleep(CommonCfg.RETRY_INTERVAL_BASE + random.random())

        return False, None

    def is_response_success(self, ret_val):
        """ Check whether a restful communication is successful

        Args:
            ret_val: dict, restful interface response

        Returns:
            Boolean: return True if status is ok, False if not
        """
        try:
            if 'status' in json.loads(ret_val) and json.loads(ret_val).get('status') == 'ok':
                return True
            return False
        except Exception as error:
            self.log_error(str(error))
        return False

    def exec_system_cmd(self, cmd):
        """ Execute a system command

        Args:
            cmd:str, a system command

        Returns:
            int: the return code
            string: output of the command
        """
        try:
            proc = subprocess.Popen(cmd, shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            out, err = proc.communicate()
            proc.wait()
            self.log_info("execute {} return code={}, out={}, err={}".format(cmd, proc.returncode, out, err))
            return proc.returncode, bytes.decode(out).strip()
        except Exception as error:
            self.log_error("execute {} error:{}".format(cmd, str(error)))

        return 1, None

    def exec_restful_rawcmd(self, command, new_restful=True, timeout=None, resp_required=True):
        """ Run command by restful raw api under BMC

        Args:
            command: str
            new_restful: bool, default True for RESTful V2.0
            timeout: int
            resp_required: bool, default True for waiting response

        Return:
            Boolean: return True if response is ok, False if not
            String: command output
        """
        post_json = {}
        post_json["Command"] = command

        if new_restful:
            url = CommonCfg.BMC_RAWCMD_API
        else:
            url = CommonCfg.OLD_BMC_RAWCMD_API
        ret, output = self.request_post(url,
                                        header=None,
                                        data=json.dumps(post_json),
                                        new_restful=new_restful,
                                        timeout=timeout,
                                        resp_required=resp_required)
        if ret and new_restful:
            output = output.get("Outputs").strip()
        return ret, output

    def get_platform_name(self):
        """ Get hardware platform name

        Returns:
            string
        """
        return device_info.get_platform()

    def get_one_sensor_info_by_restful(self, sensor_name):
        """ Get one sensor status (RESTful V2.0)

        Args:
            sensor_name: string, specified sensor name

        Returns:
            dict: None for Fail
            example:
            {
                "Critical_High": 3.63,
                "Critical_Low": 2.97,
                "Warning_High": 3.37,
                "Warning_Low": 2.67,
                "Value": 3.3
            }
        """
        try:
            url = CommonCfg.SENSOR_ONE_INFO_GET_API + sensor_name
            response = self.request_get(url)
            # response format eg:
            # {
            #     "voltage": {
            #         "BB_P3V3_STBY_V": {
            #             "Max": 3.63,
            #             "Min": 2.97,
            #             "Value": 3.3
            #         }
            #     }
            # }

            if response is not None and isinstance(response, dict):
                for _, value in response.items():
                    if len(value) == 1:
                        return value.get(sensor_name)
        except Exception as error:
            self.log_error("Get {} info error:{}".format(sensor_name, str(error)))

        return None

    def get_all_sensor_info_by_restful(self, sensor_type):
        """ Get sensors status for temperature/current/voltage (RESTful V2.0)

        Args:
            sensor_type: string, specified sensor type, temperature/current/voltage

        Returns:
            dict: None for Fail
            example:
            {
                "BB_P3V3_STBY_V": {
                    "Critical_High": 3.63,
                    "Critical_Low": 2.67,
                    "Warning_High": 3.37,
                    "Warning_Low": 2.97,
                    "Value": 3.3
                },
                "SFP_P3V3_V": {
                    "Critical_High": 3.63,
                    "Critical_Low": 2.67,
                    "Warning_High": 3.37,
                    "Warning_Low": 2.97,
                    "Value": 3.3
                }
            }
        """
        try:
            response = self.request_get(CommonCfg.SENSOR_ALL_INFO_GET_API)
            # response format eg:
            # {
            #     "voltage": {
            #         "BB_P3V3_STBY_V": {
            #             "Max": 3.63,
            #             "Min": 2.97,
            #             "Value": 3.3
            #         }
            #     },
            #     "current": {
            #         "BB_P12V_CPU_C": {
            #             "Max": 15.6,
            #             "Min": 0.0,
            #             "Value": null
            #         },
            #     }
            # }

            if response is not None and isinstance(response, dict):
                return response.get(sensor_type)
        except Exception as error:
            self.log_error("Get sensor info error:{}".format(str(error)))

        return None

    def get_all_cpld_version_by_restful(self):
        """
        Retrieves all firmware version of cpld (RESTful V2.0)

        Returns:
            dict: The firmware versions of the cpld, eg.
            {
                "CTRL_CPLD":"2.0.1",
                "CPU_CPLD":"2.0.2",
            }
        """
        versions = {}
        try:
            response = self.request_get(CommonCfg.CPLD_VERSIONS_GET_API)
            # response format eg:
            # {
            #     "CTRL_CPLD": {
            #         "flash": "internal",
            #         "version": "1.0.0"
            #    }
            # }

            if response is not None and isinstance(response, dict):
                for fw_name, fw_info in response.items():
                    versions[fw_name] = fw_info.get("version")
        except Exception as error:
            self.log_error("Get cpld version error:{} ".format(str(error)))

        return versions

    def get_cpld_version_by_restful(self, cpld_name):
        """
        Retrieves the firmware version of cpld (RESTful V2.0)

        Args:
            cpld_name: str, the cpld name

        Returns:
            string: The firmware versions of the cpld
        """
        version = CommonCfg.NULL_VALUE
        try:
            data = {"cpld": cpld_name}
            response = self.request_get(CommonCfg.CPLD_VERSION_GET_API, data)
            # response format eg:
            # {
            #     "flash": "internal",
            #     "version": "1.0.0"
            # }

            if response is not None and isinstance(response, dict):
                return response.get("version")
        except Exception as error:
            self.log_error("Get {} version error:{} ".format(cpld_name, str(error)))

        return version

    def get_cpld_boot_flash_by_restful(self, cpld_name):
        """
        Retrieves the firmware boot flash of cpld (RESTful V2.0)

        Args:
            cpld_name: str, the cpld name

        Returns:
            string: The flash position of the cpld, eg 'internal' or 'external'
        """
        flash = CommonCfg.NULL_VALUE
        try:
            data = {"cpld": cpld_name}
            response = self.request_get(CommonCfg.CPLD_VERSION_GET_API, data)
            # response format eg:
            # {
            #     "flash": "internal",
            #     "version": "1.0.0"
            # }

            if response is not None and isinstance(response, dict):
                return response.get("flash")
        except Exception as error:
            self.log_error("Get {} boot flash error:{} ".format(cpld_name, str(error)))

        return flash

    def get_bmc_version_by_restful(self):
        """
        Retrieves BMC firmware version by restful api (RESTful V2.0)

        Returns:
            string: bmc version
        """
        version = CommonCfg.NULL_VALUE
        ver_info = self.request_get(CommonCfg.BMC_VERSION_GET_API)
        boot_info = self.request_get(CommonCfg.BMC_BOOTINFO_GET_API)

        try:
            if ver_info is not None and boot_info is not None:
                current_flash = boot_info.get("Current")
                if current_flash != "N/A":
                    return ver_info.get(current_flash.capitalize())
        except Exception as error:
            self.log_error("Get bmc version error:{} ".format(str(error)))

        return version

    def get_bmc_boot_flash_by_restful(self):
        """
        Retrieves BMC boot flash by restful api (RESTful V2.0)

        Returns:
            tuple(current, next): eg.('master', 'master')
        """
        boot_info = self.request_get(CommonCfg.BMC_BOOTINFO_GET_API)
        try:
            if boot_info is not None:
                current_flash = boot_info.get("Current")
                next_flash = boot_info.get("Next")
                return current_flash, next_flash
        except Exception as error:
            self.log_error("Get bmc boot flash error:{} ".format(str(error)))

        return CommonCfg.NULL_VALUE, CommonCfg.NULL_VALUE

    def set_bmc_boot_flash_by_restful(self, flash):
        """
        Set BMC boot flash by restful api (RESTful V2.0)

        Args:
            flash: A string of flash name, eg 'master' or 'slave'

        Returns:
            Boolean: True for set success
        """
        if flash not in ["master", "slave"]:
            self.log_warning("invalid bmc boot flash:[{}]".format(flash))
            return False

        data = {"Flash": flash}
        ret, _ = self.request_post(CommonCfg.BMC_BOOT_SET_API, header=None, data=json.dumps(data))
        return ret

    #### fan restful api #####
    def get_fantray_presence_by_restful(self, index):
        """
        Retrieves the fantray presence status (RESTful V2.0)

        Args:
            index: int, the fantray index

        Returns:
            boolean: True if present else False
        """
        data = {"fantray": str(index)}
        try:
            response = self.request_get(CommonCfg.FANTRAY_PRESENCE_GET_API, data)
            # response format eg:
            # {
            #     "Present": "yes"
            # }

            if response is not None and isinstance(response, dict):
                if response.get("Present") == "yes":
                    return True
        except Exception as error:
            self.log_error("Get fan presence error:{}".format(str(error)))

        return False

    def get_fantray_mfr_by_restful(self, index):
        """
        Retrieves the fantray inventory info (RESTful V2.0)

        Args:
            index: int, the fantray index

        Returns:
            dict: fantray inventory info, empty dict if failed.
            example:
            {
                "AirFlow": "F2B",
                "PN": "L6UWH002-SD-R ",
                "Rotors": 2,
                "SN": "6022201123325"
            }
        """
        data = {"fantray": str(index)}
        return self.request_get(CommonCfg.FANTRAY_INFO_GET_API, data)

    def get_fantray_speed_info_by_restful(self, index):
        """
        Retrieves the fantray speed info (RESTful V2.0)

        Args:
            index: int, the fantray index

        Returns:
            dict: fantray speed info, empty dict if failed.
            example:
            {
                "Rotor1": {
                    "Speed": 5760.0,
                    "SpeedMax": 27600.0,
                    "SpeedMin": 0.0
                },
                "Rotor2": {
                    "Speed": 5040.0,
                    "SpeedMax": 24000.0,
                    "SpeedMin": 0.0
                },
                "pwm": 1.0
            }
        """
        data = {"fantray": str(index)}
        speed_info = self.request_get(CommonCfg.FANTRAY_SPEED_GET_API, data)
        return speed_info

    def get_fantray_led_by_restful(self, index):
        """
        Retrieves the fantray led info (RESTful V2.0)

        Args:
            index: int, the fantray index

        Returns:
            string: color string, 'red', 'green', 'off'.
        """
        data = {"fantray": str(index)}

        try:
            response = self.request_get(CommonCfg.FANTRAY_LED_GET_API, data)
            # response format eg:
            # {
            #     "color": "green"
            # }

            if response is not None and isinstance(response, dict):
                return response.get("color")
        except Exception as error:
            self.log_error("Get fan led error:{}".format(str(error)))

        return CommonCfg.NULL_VALUE

    ### psu restful api ###
    def get_psu_presence_by_restful(self, psu_index):
        """
        Retrieves the psu presence status (RESTful V2.0)

        Args:
            psu_index: int, the psu index

        Returns:
            boolean: True if present else False
        """
        data = {"psu": str(psu_index)}
        try:
            response = self.request_get(CommonCfg.PSU_PRESENCE_GET_API, data)
            # response format eg:
            # {
            #     "Present": "yes"
            # }

            if response is not None and isinstance(response, dict):
                if response.get("Present") == "yes":
                    return True
        except Exception as error:
            self.log_error("Get psu presence error:{}".format(str(error)))

        return False

    def get_psu_mfr_by_restful(self, psu_index):
        """
        Retrieves the psu inventory info (RESTful V2.0)

        Args:
            psu_index: int, the psu index

        Returns:
            dict: psu inventory info, empty dict if failed.
            example:
            {
                "AirFlow": "F2B",
                "FW_Version": "2.2.2",
                "HW_Version": "2.2.2",
                "PN": "CFN219K20070X01",
                "SN": "YZCF-03168-101",
                "Vender": "Delta"
            }
        """
        data = {"psu": str(psu_index)}
        return self.request_get(CommonCfg.PSU_INFO_GET_API, data)

    def get_psu_power_status_by_restful(self, psu_index):
        """
        Retrieves the psu input and output status info (RESTful V2.0)

        Args:
            psu_index: int, the psu index

        Returns:
            dict: psu input and output status info, empty dict if failed.
            example:
            {
                "Inputs": {
                    "Current": {
                        "HighAlarm": 5,
                        "LowAlarm": 0,
                        "Unit": "A",
                        "Value": 3
                    },
                    "Power": {
                        "HighAlarm": 1100,
                        "LowAlarm": 0,
                        "Unit": "W",
                        "Value": 1000
                    },
                    "Status": "Normal",
                    "Type": "DC",
                    "Voltage": {
                        "HighAlarm": 320,
                        "LowAlarm": 180,
                        "Unit": "V",
                        "Value": 220
                    }
                },
                "Outputs": {
                    ...
                }
            }
        """
        data = {"psu": str(psu_index)}
        return self.request_get(CommonCfg.PSU_PWR_STATUS_GET_API, data)

    def get_psu_status_by_restful(self, psu_index):
        """
        Retrieves the psu fan/temperature status info (RESTful V2.0)

        Args:
            psu_index: int, the psu index

        Returns:
            dict: psu fan/temperature status info, empty dict if failed.
            example:
            {
                "AirFlow": "F2B",
                "FW_Version": "2.2.2",
                "FanSpeed": {
                    "Max": -99999,
                    "Min": -99999,
                    "Value": -99999
                },
                "HW_Version": "2.2.2",
                "InputStatus": "Normal",
                "InputType": "DC",
                "OutputStatus": "Normal",
                "PN": "CFN219K20070X01",
                "SN": "YZCF-03168-101",
                "Temperature": {
                    "Max": -99999,
                    "Min": -99999,
                    "Value": -99999
                },
                "Vender": "Delta"
            }
        """
        data = {"psu": str(psu_index)}
        return self.request_get(CommonCfg.PSU_STATUS_GET_API, data)

    ## get device information from cache files
    ## for psu, fan, sensor
    def init_bmc_post_cache(self, enable):
        """
        Init bmc post device info by restful api (RESTful V2.0)

        Args:
            enable:boolean, True for enable bmc post device info function

        Returns:
            boolean
        """
        data = {"status": "{}".format("enable" if enable else "disable")}
        ret, _ = self.request_post(url=CommonCfg.BMC_POST_DATA_API, header=None, data=json.dumps(data))
        if ret:
            response = self.request_get(url=CommonCfg.BMC_POST_DATA_API)
            if enable and response == "enable":
                return True
            if not enable and response == "disable":
                return True

        return False

    ########## get fan/psu/sensor peripheral information by cache #######
    def __load_cache(self, file_path):
        """
        Read cache file

        Args:
            file_path:str, full path of sysfs file

        Returns:
            data: str
        """
        data = ""
        for _ in range(5):
            try:
                if os.path.exists(file_path):
                    with open(file_path, 'r', encoding="utf-8") as fd:
                        fcntl.flock(fd.fileno(), fcntl.LOCK_EX)
                        data = json.load(fd)

                    self.log_info("load cache {} content={}.".format(file_path, data))
                    return data
                else:
                    # restful cache file doesn't exist, wait one second
                    time.sleep(1)
            except Exception as error:
                self.log_notice("retry read file {}: {}".format(file_path, str(error)))
            time.sleep(0.1)

        self.log_notice("load cache file {} failed.".format(file_path))
        return data

    def get_fantray_presence_by_cache(self, index):
        """
        Retrieves the fantray presence status

        Args:
            index: int, the fantray index

        Returns:
            boolean: True if present else False
        """
        try:
            fan_name = "fan{}".format(index)
            cache_info = self.__load_cache(self.FAN_CACHE_FILE)
            if cache_info and isinstance(cache_info.get(fan_name), dict):
                if cache_info.get(fan_name).get("Present") == "yes":
                    return True
        except Exception as error:
            self.log_error("Get fan cache presence error:{}".format(str(error)))

        return False

    def get_fantray_mfr_by_cache(self, index):
        """
        Retrieves the fantray inventory info

        Args:
            index: int, the fantray index

        Returns:
            dict: fantray inventory info, empty dict if failed.
            example:
            {
                "AirFlow": "F2B",
                "PN": "L6UWH002-SD-R ",
                "Rotors": 2,
                "SN": "6022201123325",
                "pwm": 50,
                ...
            }
        """
        try:
            fan_name = "fan{}".format(index)
            cache_info = self.__load_cache(self.FAN_CACHE_FILE)
            if cache_info and isinstance(cache_info, dict):
                return cache_info.get(fan_name)
        except Exception as error:
            self.log_error("Get fan cache mfr info error:{}".format(str(error)))

        return None

    def get_fantray_speed_info_by_cache(self, index):
        """
        Retrieves the fantray speed info

        Args:
            index: int, the fantray index

        Returns:
            dict: fantray speed info, empty dict if failed.
            example:
            {
                "Rotor1": {
                    "Speed": 5760.0,
                    "SpeedMax": 27600.0,
                    "SpeedMin": 0.0
                },
                "Rotor2": {
                    "Speed": 5040.0,
                    "SpeedMax": 24000.0,
                    "SpeedMin": 0.0
                },
                "pwm": 1.0
            }
        """
        try:
            fan_name = "fan{}".format(index)
            cache_info = self.__load_cache(self.FAN_CACHE_FILE)
            if cache_info and isinstance(cache_info.get(fan_name), dict):
                return cache_info.get(fan_name).get("speed")
        except Exception as error:
            self.log_error("Get fan cache speed error:{}".format(str(error)))

        return None

    def get_fantray_led_by_cache(self, index):
        """
        Retrieves the fantray led info

        Args:
            index: int, the fantray index

        Returns:
            string: color string, 'red', 'green', 'off'.
        """
        try:
            fan_name = "fan{}".format(index)
            cache_info = self.__load_cache(self.FAN_CACHE_FILE)
            if cache_info and isinstance(cache_info.get(fan_name), dict):
                return cache_info.get(fan_name).get("color")
        except Exception as error:
            self.log_error("Get fan cache led error:{}".format(str(error)))
        return CommonCfg.NULL_VALUE

    def get_psu_presence_by_cache(self, psu_index):
        """
        Retrieves the psu presence status

        Args:
            psu_index: int, the psu index

        Returns:
            boolean: True if present else False
        """
        try:
            psu_name = "psu{}".format(psu_index)
            cache_info = self.__load_cache(self.PSU_CACHE_FILE)
            if cache_info and isinstance(cache_info.get(psu_name), dict):
                if cache_info.get(psu_name).get("Present") == "yes":
                    return True
        except Exception as error:
            self.log_error("Get psu cache presence error:{}".format(str(error)))

        return False

    def get_psu_info_by_cache(self, psu_index):
        """
        Retrieves the psu inventory info

        Args:
            psu_index: int, the psu index

        Returns:
            dict: psu inventory info, empty dict if failed.
        """
        try:
            psu_name = "psu{}".format(psu_index)
            cache_info = self.__load_cache(self.PSU_CACHE_FILE)
            if cache_info and isinstance(cache_info, dict):
                return cache_info.get(psu_name)
        except Exception as error:
            self.log_error("Get psu cache info error:{}".format(str(error)))

        return None

    def get_one_sensor_info_by_cache(self, sensor_name):
        """ Get one sensor status

        Args:
            sensor_name: string, specified sensor name

        Returns:
            dict: None for Fail
        """
        try:
            cache_info = self.__load_cache(self.SENSOR_CACHE_FILE)
            if cache_info and isinstance(cache_info, dict):
                for _, value in cache_info.items():
                    if sensor_name in value:
                        return value.get(sensor_name)
        except Exception as error:
            self.log_error("Get cache {} info error:{}".format(sensor_name, str(error)))

        return None


    ##############  common function api ###########
    def get_available_firmware_version(self, fw_name):
        """
        Retrieves the available firmware version of the component

        Returns:
            A string containing the available firmware version of the component
        """
        try:
            platform = self.get_platform_name()
            if os.path.exists("/.dockerenv"):
                file_path = "/usr/share/sonic/platform/{}".format("compare_file.json")
            else:
                file_path = "/usr/share/sonic/device/{}/{}".format(platform, "compare_file.json")
            if not os.path.exists(file_path):
                return CommonCfg.NULL_VALUE

            with open(file_path, "r", encoding='utf-8') as filed:
                ver_dict = json.load(filed)
                if fw_name in ver_dict.keys():
                    return ver_dict.get(fw_name)
        except Exception:
            pass

        return CommonCfg.NULL_VALUE

    ##############  firmware upgrade api by bmc restful ###########
    def upload_firmware_to_bmc(self, file_path, new_restful=True, timeout=None):
        """
        Call restful post interface to upload file to BMC.
        both support RESTful V1.0 and RESTful V2.0

        Args:
            file_path: str, the firmware file path
            new_restful: bool, default True for RESTful V2.0
            timeout: int

        Returns:
            Boolean: return True if response is ok, False if not
        """
        if not os.path.isfile(file_path):
            self.log_error("{} does not existed!".format(file_path))
            return False

        data = open(file_path, "rb").read()
        header = {
            "Content-Type": "application/binary",
            "X-Original-Filename": os.path.basename(file_path)
        }
        if new_restful:
            url = CommonCfg.BMC_UPLOAD_API
        else:
            url = CommonCfg.OLD_BMC_UPLOAD_API

        try:
            ret, _ = self.request_post(url, header, data, new_restful, timeout, True)
            return ret
        except Exception as error:
            self.log_error("upload file faild:{}".format(str(error)))

        return False

    def upgrade_bmc_bios_by_bmc(self, fw_type, fw_flash, fw_image, new_restful=True, timeout=None):
        """
        Call restful post interface to upgrade bmc/bios firmware,
        both support RESTful V1.0 and RESTful V2.0

        Args:
            fw_type: str, 'bmc' or 'bios'
            flash: str, 'master', 'slave' or 'both'
            fw_image: str, firmware file name
            new_restful: bool, default True for RESTful V2.0
            timeout: int, default None

        Returns:
            Boolean: return True if response is ok, False if not
        """
        request_data = {"Name": fw_type, "Flash": fw_flash}
        header = {"X-Original-Filename": fw_image}
        if new_restful:
            url = CommonCfg.BMC_UPGRADE_API
        else:
            url = CommonCfg.OLD_BMC_UPGRADE_API
        try:
            ret, _ = self.request_post(url, header, json.dumps(request_data), new_restful, timeout)
            return ret
        except Exception as error:
            self.log_error("upgrade bmc failed:{}".format(str(error)))

        return False

    def upgrade_cpld_by_bmc(self, fw_type, fw_image, new_restful=True, timeout=None):
        """
        Call restful post interface to upgrade cpld firmware,
        both support RESTful V1.0 and RESTful V2.0

        Args:
            fw_type: str, 'CTRL_CPLD','CPU_CPLD' ...
            fw_image: str, firmware file name
            new_restful: bool, default True for RESTful V2.0
            timeout: int, default None

        Returns:
            Boolean: return True if response is ok, False if not
        """
        request_data = {"Names": fw_type, "Files": fw_image}
        if new_restful:
            url = CommonCfg.BMC_CPLD_UPGRADE_API
        else:
            url = CommonCfg.OLD_BMC_CPLD_UPGRADE_API
        try:
            ret, _ = self.request_post(url, None, json.dumps(request_data), new_restful, timeout)
            return ret
        except Exception as error:
            self.log_error("upgrade cpld failed:{}".format(str(error)))

        return False

    ######### diagnosis debug api by bmc RESTful ################
    def set_fan_control_mode_by_restful(self, mode, timeout=None):
        """
        Set fan speed control mode by BMC RESTful, now only support RESTful V2.0

        Args:
            mode: str, 'manual','auto'
            timeout: int, default None

        Returns:
            Boolean: return True if response is ok, False if not
        """
        request_data = {"mode": mode}
        ret, _ = self.request_post(CommonCfg.FAN_MODE_API, None, json.dumps(request_data), True, timeout)
        return ret

    def set_fantray_led_by_restful(self, fan_index, color, timeout=None):
        """
        Set fantray led by BMC RESTful, now only support RESTful V2.0

        Args:
            fan_index: int, fan index
            color: str, 'red','green', 'yellow', 'off'
            timeout: int, default None

        Returns:
            Boolean: return True if response is ok, False if not
        """
        request_data = {"fantray": fan_index, "color": color}
        ret, _ = self.request_post(CommonCfg.FAN_LED_API, None, json.dumps(request_data), True, timeout)
        return ret

    def set_fantray_speed_by_restful(self, fan_index, pwm, timeout=None):
        """
        Set fantray speed by BMC RESTful, now only support RESTful V2.0

        Args:
            fan_index: int, fan index
            pwm: int
            timeout: int, default None

        Returns:
            Boolean: return True if response is ok, False if not
        """
        request_data = {"fantray": fan_index, "pwm": pwm}
        ret, _ = self.request_post(CommonCfg.FAN_SPEED_API, None, json.dumps(request_data), True, timeout)
        return ret

    def set_psu_fan_speed_by_restful(self, psu_index, pwm, timeout=None):
        """
        Set psu fan speed by BMC RESTful, now only support RESTful V2.0

        Args:
            psu_index: int, psu index
            pwm: int
            timeout: int, default None

        Returns:
            Boolean: return True if response is ok, False if not
        """
        request_data = {"psu": psu_index, "pwm": pwm}
        ret, _ = self.request_post(CommonCfg.PSU_FAN_SPEED_API, None, json.dumps(request_data), True, timeout)
        return ret

    ############# bmc communication only by old RESTful V1.0 ####################
    def get_bmc_firmware_info_by_rawcmd(self, timeout=None):
        """ Get bmc firmware info by RESTful v1.0 rawcmd interface

        Args:
            timeout: int, default None

        Returns:
            Dict: return dict if response is ok, empty dict if not
        """
        boot_info = {}
        cmd = "fw-util --get_info bmc"
        ret, output = self.exec_restful_rawcmd(cmd, False, timeout)
        if ret and output:
            try:
                for line in output.splitlines():
                    if "current_boot" in line:
                        boot_info["current_boot"] = line.split(":")[1].strip()
                    if "next_boot" in line:
                        boot_info["next_boot"] = line.split(":")[1].strip()
                    if "master_version" in line:
                        boot_info["master"] = line.split(":")[1].strip()
                    if "slave_version" in line:
                        boot_info["slave"] = line.split(":")[1].strip()
            except Exception as parse_err:
                self.log_warning(str(parse_err))

        return boot_info

    def set_bmc_boot_flash_by_rawcmd(self, flash):
        """ Set bmc next boot flash by RESTful v1.0 rawcmd interface

        Args:
            flash: str, master or slave

        Returns:
            Bool: return True if response is ok, False if not
        """
        if flash not in ["master", "slave"]:
            self.log_warning("invalid bmc boot flash:[{}]".format(flash))
            return False

        cmd =  "fw-util --set_info bmc {}".format(flash)
        ret, _ = self.exec_restful_rawcmd(cmd, False)
        return ret
