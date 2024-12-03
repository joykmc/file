# -*- coding: UTF-8 -*-

"""
Module contains an implementation of SONiC Platform Base API and
provides the component information which are available in the platform
"""

try:
    import os
    import sys
    import re
    import binascii
    import hashlib
    import datetime
    import logging
    import time
    from sonic_platform_base.component_base import ComponentBase
    from sonic_platform.plat_common import PlatCommon
    from sonic_platform.plat_common import CommonCfg
    from vendor_sonic_platform import hooks
    from vendor_sonic_platform.device import DeviceCfg
except ImportError as import_error:
    raise ImportError(str(import_error) + "- required module not found") from import_error

RETRY_TIMES = 3
DELAY_SECS = 3
ENABLED = 1
DISABLED = 0
CPU_UPGRADE_MASTER_BIOS = 0
CPU_UPGRADE_SLAVE_BIOS = 1
BMC_NAME = "BMC"
CPU_NAME = "CPU"

class Component(ComponentBase):
    """Platform-specific Component class"""

    def __init__(self, comp_type, index=0, comp_info=None):
        """
        Args:
            comp_typ: string,
            index: int, start from 0
            comp_info: dict,
        """
        ComponentBase.__init__(self)
        
        self.comp_type = comp_type
        self.index = index + 1
        self.sysfs_path = None
        self.jtag_group = None
        self.plat_comm = PlatCommon(debug=CommonCfg.DEBUG)
        self.__init_component_cfg(comp_info)
        self.fwup_log_file = "/var/log/fw_upgrade.log"
        self.__init_logging()
        self.cpld_upgrade_sysfs = CommonCfg.S3IP_FW_CTL_PATH + "/cpld_update_ctrl"
        if hasattr(DeviceCfg, "NEW_BMC_RESTFUL"):
            self.new_restful = DeviceCfg.NEW_BMC_RESTFUL
        else:
            # when call RESTful, default BMC RESTful V2.0
            self.new_restful = True

    def __init_logging(self):
        self.logger = logging.getLogger("COMPONENT")
        self.logger.setLevel(logging.INFO)
        fmt_str = '%(asctime)s [%(name)s] %(levelname)s: %(message)s'
        formatter = logging.Formatter(fmt_str, datefmt="%Y-%m-%d %H:%M:%S")
        handler = logging.FileHandler(self.fwup_log_file, delay=True)
        handler.setFormatter(formatter)

        if not self.logger.hasHandlers():
            self.logger.addHandler(handler)

    # Logging facility
    def __fw_log(self, message):
        return self.logger.info(message)

    def __init_component_cfg(self, comp_info):
        try:
            self.name = comp_info["name"]
            self.description = comp_info["desc"]
            self.method = comp_info["method"]
            if self.method == CommonCfg.BY_SYSFS:
                if self.comp_type == CommonCfg.COMPONENT_TYPE_CPLD:
                    self.sysfs_path = os.path.join(CommonCfg.S3IP_CPLD_PATH, "cpld{}".format(self.index))
                elif self.comp_type == CommonCfg.COMPONENT_TYPE_FPGA:
                    self.sysfs_path = os.path.join(CommonCfg.S3IP_FPGA_PATH, "fpga{}".format(self.index))
        except (KeyError, ValueError) as err:
            self.plat_comm.log_error("init component sysfs path error:{}".format(str(err)))

    def __get_file_path(self, file_name):
        if self.sysfs_path is not None:
            return os.path.join(self.sysfs_path, file_name)
        return None

    def get_name(self):
        """
        Retrieves the name of the component

        Returns:
            A string containing the name of the component
        """
        return self.name

    def get_type(self):
        """
        Retrieves the type of the component

        Returns:
            A string containing the type of the component, eg:"BIOS", "CPLD", "FPGA"
        """
        return self.comp_type

    def get_description(self):
        """
        Retrieves the description of the component

        Returns:
            A string containing the description of the component
        """
        return self.description

    def get_firmware_version(self):
        """
        Retrieves the firmware version of the component

        Note: the firmware version will be read from HW

        Returns:
            A string containing the firmware version of the component
        """
        fw_version = CommonCfg.NULL_VALUE

        if self.comp_type == CommonCfg.COMPONENT_TYPE_BIOS:
            fw_version = self._get_bios_version()
        elif self.comp_type == CommonCfg.COMPONENT_TYPE_UBOOT:
            fw_version = self._get_uboot_version()
        elif self.comp_type == CommonCfg.COMPONENT_TYPE_BMC:
            fw_version = self._get_bmc_version()
        elif self.comp_type == CommonCfg.COMPONENT_TYPE_CPLD:
            fw_version = self._get_cpld_version()
        elif self.comp_type == CommonCfg.COMPONENT_TYPE_FPGA:
            fw_version = self._get_cpld_version()

        return fw_version.strip()

    def get_available_firmware_version(self, image_path):
        """
        Retrieves the available firmware version of the component

        Note: the firmware version will be read from image

        Args:
            image_path: A string, path to firmware image

        Returns:
            A string containing the available firmware version of the component
        """
        raise NotImplementedError

    def get_firmware_update_notification(self, image_path):
        """
        Retrieves a notification on what should be done in order to complete
        the component firmware update

        Args:
            image_path: A string, path to firmware image

        Returns:
            A string containing the component firmware update notification if required.
            By default 'None' value will be used, which indicates that no actions are required
        """
        raise NotImplementedError

    def install_firmware(self, image_path, flash="master"):
        """
        Installs firmware to the component

        This API performs firmware installation only: this may/may not be the same as firmware update.
        In case platform component requires some extra steps (apart from calling Low Level Utility)
        to load the installed firmware (e.g, reboot, power cycle, etc.) - this must be done manually by user

        Note: in case immediate actions are required to complete the component firmware update
        (e.g., reboot, power cycle, etc.) - will be done automatically by API and no return value provided

        Args:
            image_path: A string, path to firmware image

        Returns:
            A boolean, True if install was successful, False if not
        """
        raise NotImplementedError

    def update_firmware(self, image_path, flash="master"):
        """
        Updates firmware of the component

        This API performs firmware update: it assumes firmware installation and loading in a single call.
        In case platform component requires some extra steps (apart from calling Low Level Utility)
        to load the installed firmware (e.g, reboot, power cycle, etc.) - this will be done automatically by API

        Args:
            image_path: A string, path to firmware image

        Raises:
            RuntimeError: update failed
        """
        if (self.comp_type in CommonCfg.COMPONENT_TYPE_BMC) or ((self.comp_type in CommonCfg.COMPONENT_TYPE_BIOS) and (BMC_NAME in DeviceCfg.BIOS_UPDATE_METHOD)):
            msg = "Upgrade {}, path {}, extra {}".format(self.comp_type, image_path, flash)
            self.__fw_log(msg)
            for i in range(RETRY_TIMES):
                if not self.plat_comm.upload_firmware_to_bmc(image_path, self.new_restful):
                    msg = "Failed to upload_{} {}, {}".format(self.comp_type, flash, image_path)
                    self.__fw_log(msg)
                    if i >= RETRY_TIMES - 1:
                        return False
                    time.sleep(DELAY_SECS)
                else:
                    break

            file_name = os.path.basename(image_path)
            if flash == "both":
                if not self.plat_comm.upgrade_bmc_bios_by_bmc(self.comp_type, "slave", file_name, self.new_restful):
                    msg = "Failed to program_{} {}, {}".format(self.comp_type, "slave", image_path)
                    self.__fw_log(msg)
                    return False
                if not self.plat_comm.upgrade_bmc_bios_by_bmc(self.comp_type, "master", file_name, self.new_restful):
                    msg = "Failed to program_{} {}, {}".format(self.comp_type, "master", image_path)
                    self.__fw_log(msg)
                    return False
            else:
                if not self.plat_comm.upgrade_bmc_bios_by_bmc(self.comp_type, flash, file_name, self.new_restful):
                    msg = "Failed to program_{} {}, {}".format(self.comp_type, flash, image_path)
                    self.__fw_log(msg)
                    return False
            self.__fw_log("Upgrade {}, program bmc flash done, reboot it".format(self.comp_type))
            if self.comp_type in CommonCfg.COMPONENT_TYPE_BMC:
                self.__reboot_bmc()
                self.__wait_update(CommonCfg.BMC_DEFAULT_IP)
            self.__fw_log("Upgrade {} full process done".format(self.comp_type))
            return True

        if (self.comp_type in CommonCfg.COMPONENT_TYPE_BIOS) and (CPU_NAME in DeviceCfg.BIOS_UPDATE_METHOD):
            return self.__upgrade_bios_by_cpu(flash, image_path)

        if self.comp_type in CommonCfg.COMPONENT_TYPE_FPGA:
            self.__fw_log("Upgrading {}".format(self.comp_type))
            if not os.path.isfile(image_path):
                self.__fw_log("Fpga image file {} not exist".format(image_path))
                return False
            ret, _, raw_file = self.__image_verify(image_path)
            if not ret:
                self.__fw_log("FPGA, {} firmware image verify failed!".format(image_path))
                return False

            cmd = 'fpga_update %s' % (raw_file)
            self.__fw_log("Will exec [fpga_update {0}]".format(raw_file))
            status, _ = self.plat_comm.exec_system_cmd(cmd)
            if status:
                return False
            return True

        if self.comp_type in CommonCfg.COMPONENT_TYPE_CPLD:
            msg = "CPLD Upgrade: paths {}".format(image_path)
            self.__fw_log(msg)
            return self.__cpu_update_cplds(image_path)

        self.__fw_log("Not support this component {} upgrade!".format(self.comp_type))
        return False

    def __reboot_bmc(self):
        cmd = "reboot"
        status, out = self.plat_comm.exec_restful_rawcmd(cmd, self.new_restful)
        if not status:
            self.__fw_log("get_onie_version exception: %s" % str(out))
            return False
        return True

    def __wait_update(self, bmc_address):
        for i in range(55):
            self.__fw_log(
                "\r {} start erasing->writing->verify->reboot, please wait.........:{}s".format(bmc_address, (i + 1) * 6))
            sys.stdout.flush()
            time.sleep(6)
        try_count = 1
        while True:
            if try_count == 10:
                self.__fw_log("\n {0} Already tried 10 times, {0} :network lost,please check the update whether ended\n".format(bmc_address))
                break
            self.__fw_log("\n {} start try to reconnect host:,count={}\n".format(bmc_address, str(try_count)))
            backinfo = int(os.system('ping -c 3 %s > /dev/null 2>&1' % bmc_address))
            if backinfo == 0:
                time.sleep(60) # waiting restful api service ready
                self.__wait_bmc_restful_service()
                self.__fw_log("\n {} :bmc update end,please check the firmware verion.\n".format(bmc_address))
                break
            time.sleep(3)
            try_count += 1

    def __wait_bmc_restful_service(self):
        try_count = 1
        while True:
            bmc_info = {}
            if try_count == 5:
                self.__fw_log("\n Already tried 5 times, BMC restful services not ready,please check the update whether ended\n")
                break
            self.__fw_log("\n start retry to reconnect host restful api:,count={}\n".format(str(try_count)))

            try:
                bmc_info = self.plat_comm.request_get(CommonCfg.BMC_VERSION_GET_API)
                if bmc_info:
                    self.__fw_log("\nbmc restful api ready, ready to restart pmon service.\n")
                    break
                
                time.sleep(20)
                try_count += 1
            except (TypeError, ValueError) as error:
                self.__fw_log("\nBmc restful requeset get: {}, retry...".format(str(error)))
                time.sleep(20)
                try_count += 1

    def __upgrade_bios_by_cpu(self, flash, fw_path):
        """
        Execute BIOS upgrade process via AMI afulnx_64 under SONiC/DiagOS
        """
        ret = True
        support_me_recovery_ver = DeviceCfg.SUPPORT_ME_RECOVER_VER.split('.')
        support_me_recovery = True
        bios_ver = self._get_bios_version()
        for index, value in enumerate(bios_ver.split(".")):
            if int(value) > int(support_me_recovery_ver[index]):
                break 

            if int(value) == int(support_me_recovery_ver[index]):
                continue
            
            support_me_recovery = False
            break
                
        flash_list = ["master", "slave", "both"]
        if flash not in flash_list:
            self.__fw_log("Failed: BIOS flash should be master,slave or both!")
            return False

        if not os.path.isfile(fw_path):
            self.__fw_log("Failed: {} not found!".format(fw_path))
            return False

        # verify firmware, get raw image
        ret, _, raw_file = self.__image_verify(fw_path)
        if not ret:
            self.__fw_log("firmware image verify failed!")
            return False

        if flash == "master":
            select_value_list = [CPU_UPGRADE_MASTER_BIOS]
        elif flash == "slave":
            select_value_list = [CPU_UPGRADE_SLAVE_BIOS]
        else:  # both
            select_value_list = [CPU_UPGRADE_MASTER_BIOS, CPU_UPGRADE_SLAVE_BIOS]

        for value in select_value_list:
            # select bios flash
            if support_me_recovery:
                self.plat_comm.exec_system_cmd("lpctool 0xb2 0xa8")
                time.sleep(15)
                status, oem2 = self.plat_comm.exec_system_cmd("lpctool 0xb2 0xaa")
                self.__fw_log("oem2:{} ,{}".format(status, oem2))
            # enable cpu upgrade bios
            if not self.plat_comm.write_file(CommonCfg.S3IP_BIOS_UPDATE_EN_PATH, ENABLED):
                self.__fw_log("enable cpu upgrade bios failed!")
                return False

            if value == CPU_UPGRADE_MASTER_BIOS:
                self.plat_comm.write_file(CommonCfg.S3IP_BIOS_MASTER_WP_PATH, ENABLED)
            elif value == CPU_UPGRADE_SLAVE_BIOS:
                self.plat_comm.write_file(CommonCfg.S3IP_BIOS_SLAVE_WP_PATH, ENABLED)
            if support_me_recovery:
                status, mode = self.plat_comm.exec_system_cmd(DeviceCfg.PCI_CHECK_CMD)
                if mode.strip() != '2':
                    self.__fw_log("Set ME recovery mode failed!")
                    # return False
                    ret &= False
                    break

                self.__fw_log("Set ME recovery mode success!")

            if not self.plat_comm.write_file(CommonCfg.S3IP_BIOS_UP_FLASH_SEL_PATH, value):
                self.__fw_log("select cpu upgrade {} bios failed!".format(flash))
                ret &= False
                break

            # upgrade bios flash
            status, output = self.plat_comm.exec_system_cmd("afulnx_64 {} /B /P /N /ME /K /L /X".format(raw_file))
            self.__fw_log(output)
            ret &= False if status else True
            time.sleep(5)

            # close cpu upgrade bios
            self.plat_comm.write_file(CommonCfg.S3IP_BIOS_UPDATE_EN_PATH, DISABLED)

        return ret

    def __cpu_update_cplds(self, fw_files):
        raw_file = ""
        
        # Support list, check integrity
        ret, cpld_name, raw_file = self.__image_verify(fw_files)
        if not ret:
            self.__fw_log("{} firmware image verify failed!".format(fw_files))
            return False

        jtag_group = self.__get_cpld_jatg_group(cpld_name)
        if not jtag_group:
            self.__fw_log("can not get cpld {} jatg group".format(cpld_name))
            return False

        self.__fw_log("cpld_names:{0} cpld_paths:{1}".format(cpld_name, fw_files))
        # Do upgrade each CPLD requested
        
        ctrl_jtag_switch = [
                CommonCfg.GPIO_SYSFS_PATH.format(self.__get_jtag_en(jtag_group)),
                CommonCfg.GPIO_SYSFS_PATH.format(self.__get_jtag_select(jtag_group))
        ]

        ctrl_switch_vals = self.__get_cpld_jatg_chan_sel(cpld_name)
        if not ctrl_switch_vals:
            self.__fw_log("cpld_names:{0}, get jatg channel select fail!".format(cpld_name))
            return False

        if os.path.exists(CommonCfg.S3IP_CPLD_UPDATE_ENABLE_PATH):
            status = self.plat_comm.write_file(CommonCfg.S3IP_CPLD_UPDATE_ENABLE_PATH, ENABLED)
            if not status:
                self.__fw_log("Enable cpu upgarde cpld fail!")
                return False

            chk_val = self.plat_comm.read_file(CommonCfg.S3IP_CPLD_UPDATE_ENABLE_PATH)
            if str(ENABLED) not in chk_val:
                self.__fw_log("Enable cpu upgarde cpld fail! Get {}".format(chk_val))
                return False

        counts = len(ctrl_jtag_switch)
        for reg_idx in range(counts):
            # Write REG and verify
            self.__fw_log("self.write_cpld_reg_int({0}, {1})".format(ctrl_jtag_switch[reg_idx],
                    ctrl_switch_vals[reg_idx]))
            if not self.__write_cpld_reg_int(ctrl_jtag_switch[reg_idx], ctrl_switch_vals[reg_idx]):
                return False

        # Select CPLD
        self.__fw_log("exec self.choose_cpld_cmd({0})".format(cpld_name))
        cmd = self.__choose_cpld_cmd(cpld_name)
        if cmd == -1:
            return False
        self.__fw_log("exec self.check_reg_value({0})".format(cmd))
        # Verify write command by read it back
        if int(self.__check_reg_value(cmd)) == -1:
            self.__fw_log("exec self.check_reg_value({0}) failed!".format(cmd))
            return False
        self.__fw_log("Ispvm will update cpld.")
        # Write CPLD flash with ispvme tool
        cmd = "ispvm dll /usr/lib/libgpio.so {0} --tdo {1} --tdi {2} --tms {3} --tck {4}".format(
                raw_file, self.__get_jtag_tdo(jtag_group),
                self.__get_jtag_tdi(jtag_group),
                self.__get_jtag_tms(jtag_group),
                self.__get_jtag_tck(jtag_group)
            )
        self.__fw_log("cmd:{0}".format(cmd))
        status, output = self.plat_comm.exec_system_cmd(cmd)
        self.__fw_log(output)
        if status:
            return False

        self.__fw_log("upgrade %s done." % cpld_name)
        self.__fw_log("Upgrade cpld all done, set GPIO back to default")
        # All done, set GPIO back to default
        # default 1: /sys/class/gpio/gpio837 back to 0, i.e. disable cpu jtag, Write REG and verify
        self.__write_cpld_reg_int(ctrl_jtag_switch[0], 0, False)
        self.__write_cpld_reg_int(ctrl_jtag_switch[1], 0, False)

        # default 2: selelct basecpld, 寄存器值回写默认值0xff
        cmd = self.__choose_cpld_cmd("default")
        # Verify write command by read it back
        if int(self.__check_reg_value(cmd)) == -1:
            return False

        if os.path.exists(CommonCfg.S3IP_CPLD_UPDATE_ENABLE_PATH):
            status = self.plat_comm.write_file(CommonCfg.S3IP_CPLD_UPDATE_ENABLE_PATH, DISABLED)
            if not status:
                self.__fw_log("Disable cpu upgarde cpld fail!")
                return False

            chk_val = self.plat_comm.read_file(CommonCfg.S3IP_CPLD_UPDATE_ENABLE_PATH)
            if str(DISABLED) not in chk_val:
                self.__fw_log("Dixable cpu upgarde cpld fail! Get {}".format(chk_val))
                return False
                
        self.__fw_log("Upgrade cpld {} done".format(cpld_name))
        return True

    def __find_symlink_realpath_gpio_num(self, islink):
        if not os.path.exists(islink):
            self.__fw_log("error, {} is not exist".format(islink))
            return CommonCfg.NULL_VALUE
        if os.path.islink(islink):
            real_path = os.path.realpath(islink)
            self.__fw_log("islink symlink is [{}].".format(real_path))
        else:
            self.__fw_log("error, {} is not symlink".format(islink))
            return CommonCfg.NULL_VALUE

        gpio_name = real_path.split("/")[-1]
        gpio_num = re.findall(r'\d+', gpio_name)[0]
        self.__fw_log("gpio No.{}".format(gpio_num))

        return gpio_num

    def __get_jtag_tdo(self, jtag_group):
        tdo = self.__find_symlink_realpath_gpio_num(
                CommonCfg.S3IP_JATG_GPIO_PATH.format(jtag_group) + "/jtag_tdo")
        self.__fw_log("tdo gpio No.{}".format(tdo))
        return tdo

    def __get_jtag_tdi(self, jtag_group):
        tdi = self.__find_symlink_realpath_gpio_num(
                CommonCfg.S3IP_JATG_GPIO_PATH.format(jtag_group) + "/jtag_tdi")
        self.__fw_log("tdo gpio No.{}".format(tdi))
        return tdi

    def __get_jtag_tms(self, jtag_group):
        tms = self.__find_symlink_realpath_gpio_num(
                CommonCfg.S3IP_JATG_GPIO_PATH.format(jtag_group) + "/jtag_tms")
        self.__fw_log("tdo gpio No.{}".format(tms))
        return tms

    def __get_jtag_tck(self, jtag_group):
        tck = self.__find_symlink_realpath_gpio_num(
                CommonCfg.S3IP_JATG_GPIO_PATH.format(jtag_group) + "/jtag_tck")
        self.__fw_log("tdo gpio No.{}".format(tck))
        return tck

    def __get_jtag_en(self, jtag_group):
        enable = self.__find_symlink_realpath_gpio_num(
                CommonCfg.S3IP_JATG_GPIO_PATH.format(jtag_group) + "/jtag_en")
        self.__fw_log("tdo gpio No.{}".format(enable))
        return enable

    def __get_jtag_select(self, jtag_group):
        sel = self.__find_symlink_realpath_gpio_num(
                CommonCfg.S3IP_JATG_GPIO_PATH.format(jtag_group) + "/jtag_mux_sel")
        self.__fw_log("tdo gpio No.{}".format(sel))
        return sel

    def __check_reg_value(self, cmd):
        val = (cmd.split(' ')[1]).replace('\'', '')
        status, _ = self.plat_comm.exec_system_cmd(cmd)
        if status:
            self.__fw_log("cmd {} exec fail!".format(cmd))
            return -1

        output_t = self.plat_comm.read_file(self.cpld_upgrade_sysfs)
        if output_t is None:
            self.__fw_log("cmd  cat {} exec fail!".format(self.cpld_upgrade_sysfs))
            return -1
        self.__fw_log("check_reg_value():val={0}, output_t={1}".format(val, output_t))
        if int(output_t) == int(val):
            return 0
        self.__fw_log("invoke output:%s, val = %s" % (output_t, val))
        return -1

    # Write REG with int value, validate it with read back
    def __write_cpld_reg_int(self, reg_path, value, validate=True):
        msg = "write_cpld_reg: path {0}, val {1}, validate {2}".format(reg_path, value, validate)
        self.__fw_log(msg)
        # Write
        status = self.plat_comm.write_file(reg_path + "/value", value) 
        if not status:
            return False

        if not validate:
            return True
        # Verify
        output = self.plat_comm.read_file(reg_path + "/value")
        if output is None:
            return False

        try:
            if int(output) == value:
                self.__fw_log("output {}, expected value {}".format(output, value))
                return True
        except (TypeError, ValueError) as write_err:
            self.__fw_log("output {}, expected value {}".format(output, value))
            self.__fw_log("Get error {}".format(str(write_err)))
        return False

    def __choose_cpld_cmd(self, cpld_name):
        if cpld_name == "default":
            return "echo 255 > " + self.cpld_upgrade_sysfs

        cpld_ctl_sel = self.__get_cpld_jatg_ctl_sel(cpld_name)
        if cpld_ctl_sel is None:
            self.__fw_log("Error: invalid cpld '%s'\n" % cpld_name)
            return -1

        cmd = "echo {} > ".format(cpld_ctl_sel) + self.cpld_upgrade_sysfs
        return cmd

    def __image_verify(self, firmware_file):
        """
        Verify firmware file.

            @param flash_type: "bmc", "bios", "cpld" .etc
            @param firmware_file: the fimware file;

            @return: status, True or False;
                string, raw firmware image path.
        """
        if ".hpm" in firmware_file:
            count = 0
            total_size = 0
            curr_time = datetime.datetime.now().strftime("%d%H%M%S")
            work_path = "/tmp/{}.{}/".format(os.path.basename(firmware_file), curr_time)
            if not os.path.isdir(work_path):
                os.makedirs(work_path)

            if not os.path.isfile(firmware_file):
                self.__fw_log("{} not exist.".format(firmware_file))
                return False, None, None

            try:
                with open(firmware_file, "rb") as image_file:
                    image_file.seek(-2, 2)
                    count = binascii.b2a_hex(image_file.read())
                    image_file.close()
            except IOError:
                self.__fw_log("open file failed")
                return count, None, None

            count = int(count, 16) + 2
            total_size = os.path.getsize(firmware_file)
            self.__fw_log("total_size:%d, count:%d" % (total_size, count))
            cmd = "tail -c {0} {1} > {2}public.pem.bak".format(count, firmware_file, work_path)
            sta, out = self.plat_comm.exec_system_cmd(cmd)
            self.__fw_log("invoke {}, info_log:{}.".format(cmd, out))
            if sta != 0:
                return sta, None, None

            cmd = "dd if={0}public.pem.bak of={0}public.pem bs={1} count=1 >/dev/null 2>&1".format(work_path, count-2)
            sta, out = self.plat_comm.exec_system_cmd(cmd)
            self.__fw_log("invoke {}, info_log:{}.".format(cmd, out))
            if sta != 0:
                return sta, None, None

            cmd = "dd if={0} of={1}signimage.hpm.bak bs={2} count=1 >/dev/null 2>&1".format(firmware_file, work_path, total_size-count)
            sta, out = self.plat_comm.exec_system_cmd(cmd)
            self.__fw_log("invoke {}, info_log:{}.".format(cmd, out))
            if sta != 0:
                return sta, None, None

            cmd = "tail -c 256 {0}signimage.hpm.bak > {0}sign.bin".format(work_path)
            sta, out = self.plat_comm.exec_system_cmd(cmd)
            self.__fw_log("invoke {}, info_log:{}.".format(cmd, out))
            if sta != 0:
                return sta, None, None

            cmd = "dd if={0}signimage.hpm.bak of={0}image.hpm.bak bs={1} count=1 >/dev/null 2>&1".format(work_path, total_size-count-256)
            sta, out = self.plat_comm.exec_system_cmd(cmd)
            self.__fw_log("invoke {}, info_log:{}.".format(cmd, out))
            if sta != 0:
                return sta, None, None

            _hash = hashlib.sha256()
            with open("{0}image.hpm.bak".format(work_path), "rb") as verify_file:
                _file = verify_file.read()
            _hash.update(_file)
            hash_info = _hash.hexdigest() + " {0}image.hpm.bak\n".format("/tmp/")
            with open("{0}checkSum.bin".format(work_path), 'w', encoding="utf-8") as verify_file:
                verify_file.write(hash_info)

            cmd = "openssl dgst -sha256 -verify {0}public.pem -signature {0}sign.bin {0}checkSum.bin".format(work_path)
            sta, out = self.plat_comm.exec_system_cmd(cmd)
            self.__fw_log("invoke {}, info_log:{}.".format(cmd, out))
            if sta != 0:
                return sta, None, None

            fw_name = ""
            if  "Verified OK" in out.strip():
                fw_name = self.__firmware_type("{0}image.hpm.bak".format(work_path))
                cmd = "({0} < {1}image.hpm.bak > {1}image) >/dev/null 2>&1".format("{ dd bs=3 skip=1 count=0; cat; }", work_path)
                sta, out = self.plat_comm.exec_system_cmd(cmd)
                self.__fw_log("invoke {}, info_log:{}.".format(cmd, out))
                if sta != 0:
                    return sta, None, None
            else:
                self.__fw_log("image verified failed!")
                return False, None, None

            return True, fw_name, "{}image".format(work_path)

        if ".inspur" in firmware_file:
            tmp_dir = "/tmp/image-update"
            public_key_file_path = "/etc/public_key/capub.pem"
            sign_file = "inspur-image.sign"
            tar_file = "inspur-image.tar"
            type_file = "image.type"
            raw_file = "image.raw"
            tail_raw_file = "tail.raw"

            if not os.path.isfile(firmware_file):
                self.__fw_log("{} not exist.".format(firmware_file))
                return False, None, None

            self.__fw_log("firmware verify start")
            if not os.path.exists(tmp_dir):
                os.makedirs(tmp_dir)
            for file in os.listdir(tmp_dir):
                file_path = os.path.join(tmp_dir, file)
                if os.path.isfile(file_path):
                    os.remove(file_path)

            self.__fw_log("step1, decompress base tar image...")
            tar_cmd = "tar -xvf {} -C {}".format(firmware_file, tmp_dir)
            status, _ = self.plat_comm.exec_system_cmd(tar_cmd)
            if status:
                self.__fw_log("decompress base tar image failed")
                return False, None, None

            self.__fw_log("step2, image signature verify...")
            sign_file_path = os.path.join(tmp_dir, sign_file)
            tar_file_path = os.path.join(tmp_dir, tar_file)
            if not os.path.isfile(sign_file_path) or not os.path.isfile(tar_file_path):
                self.__fw_log("image signature files do not existed.")
                return False, None, None

            verify_cmd = "openssl dgst -sha256 -verify {} -signature {} {}".format(
                        public_key_file_path,
                        sign_file_path,
                        tar_file_path
                        )
            status, _ = self.plat_comm.exec_system_cmd(verify_cmd)
            if status:
                self.__fw_log("image signature verify failed")
                return False, None, None

            self.__fw_log("step3, decompress raw tar image...")
            tar_cmd = "tar xvf {} -C {}".format(tar_file_path, tmp_dir)
            status, _ = self.plat_comm.exec_system_cmd(tar_cmd)
            if status:
                self.__fw_log("decompress raw tar image failed")
                return False, None, None

            self.__fw_log("step4, image type check...")
            type_file_path = os.path.join(tmp_dir, type_file)
            raw_file_path = os.path.join(tmp_dir, raw_file)
            if not os.path.isfile(type_file_path) or not os.path.isfile(raw_file_path):
                self.__fw_log("image type file does not existed.")
                return False, None, None

            fw_name = ''
            with open(type_file_path, "r") as image_file:
                image_type = image_file.read().strip()

            fw_name = image_type.upper()

            if "CPLD" in fw_name:
                self.__fw_log("step5, image splite")
                tail_raw_file_path = os.path.join(tmp_dir, tail_raw_file)
                splite_cmd = "dd if={} bs=1  skip=20 of={}".format(raw_file_path, tail_raw_file_path)
                status, _ = self.plat_comm.exec_system_cmd(splite_cmd)
                if status:
                    self.__fw_log("generate tail raw image failed")
                    return False, None, None

                self.__fw_log("verify end, raw image return.")
                return True, fw_name, tail_raw_file_path

            self.__fw_log("verify end, raw image return.")
            return True, fw_name, raw_file_path

        return False, None, None

    def __firmware_type(self, firmware_file):
        fw_id = 0
        cpld_id = 0
        try:
            with open(firmware_file, "rb") as image_file:
                image_file.seek(0, 0)
                fw_id = binascii.b2a_hex(image_file.read(1))
                image_file.seek(2, 0)
                cpld_id = binascii.b2a_hex(image_file.read(1))
                image_file.close()
        except IOError:
            self.__fw_log("open file failed")
            return cpld_id
        fw_type = {1: "BMC", 2: "BIOS", 3: "CPLD"}
        fw_name = fw_type[int(fw_id)]

        self.__fw_log("firmware type is {0}\n".format(fw_name))
        if int(fw_id )== 3:
            fw_name = self.__get_cpld_name_by_image_index(int(cpld_id))
            if not fw_name:
                self.__fw_log("can not get cpld name by index: {}\n".format(cpld_id))
                return 0

            self.__fw_log("cpld type is {0}\n".format(fw_name))

        return fw_name

    def __get_cpld_jatg_group(self, name):
        try:
            cpld_info_list = DeviceCfg.CHASSIS_COMPONENT_INFO[CommonCfg.COMPONENT_TYPE_CPLD]
            for info in cpld_info_list:
                if info[CommonCfg.CPLD_NAME_KEY] == name:
                    return info[CommonCfg.CPLD_JTAG_GROUP_KEY]
            
            self.__fw_log("can not get cpld group:{}".format(name))
            return None
        except (KeyError, ValueError) as error:
            self.__fw_log("can not get cpld group:{}, get error {}".format(name, str(error)))
            return None

    def __get_cpld_jatg_chan_sel(self, name):
        try:
            cpld_info_list = DeviceCfg.CHASSIS_COMPONENT_INFO[CommonCfg.COMPONENT_TYPE_CPLD]
            for info in cpld_info_list:
                if info[CommonCfg.CPLD_NAME_KEY] == name:
                    return info[CommonCfg.CPLD_JTAG_CHANNEL_SEL_KEY]
            
            self.__fw_log("can not get cpld jatg channel select:{}".format(name))
            return None
        except (KeyError, ValueError) as error:
            self.__fw_log("can not get cpld jatg channel select:{}, get error {}".format(name, str(error)))
            return None 

    def __get_cpld_jatg_ctl_sel(self, name):
        try:
            cpld_info_list = DeviceCfg.CHASSIS_COMPONENT_INFO[CommonCfg.COMPONENT_TYPE_CPLD]
            for info in cpld_info_list:
                if info[CommonCfg.CPLD_NAME_KEY] == name:
                    return info[CommonCfg.CPLD_JTAG_CPLD_SEL_KEY]
            
            self.__fw_log("can not get cpld jatg select:{}".format(name))
            return None
        except (KeyError, ValueError) as error:
            self.__fw_log("can not get cpld jatg select:{}, get error {}".format(name, str(error)))
            return None 

    def __get_cpld_name_by_image_index(self, index):
        try:
            cpld_info_list = DeviceCfg.CHASSIS_COMPONENT_INFO[CommonCfg.COMPONENT_TYPE_CPLD]
            for info in cpld_info_list:
                if info[CommonCfg.CPLD_INDEX_KEY] == index:
                    return info[CommonCfg.CPLD_NAME_KEY]

            if index == DeviceCfg.FPGA_COMP_INDEX:
                return DeviceCfg.FPGA_COMP_NAME
            
            self.__fw_log("can not get cpld name by index:{}".format(index))
            return None
        except (KeyError, ValueError) as error:
            self.__fw_log("can not get cpld name by index:{}, get error {}".format(index, str(error)))
            return None 

    def auto_update_firmware(self, image_path, boot_type):
        """
        Updates firmware of the component, sonic-202205

        This API performs firmware update automatically based on boot_type: it assumes firmware installation
        and/or creating a loading task during the reboot, if needed, in a single call.
        In case platform component requires some extra steps (apart from calling Low Level Utility)
        to load the installed firmware (e.g, reboot, power cycle, etc.) - this will be done automatically during the reboot.
        The loading task will be created by API.

        Args:
            image_path: A string, path to firmware image
            boot_type: A string, reboot type following the upgrade
                         - none/fast/warm/cold

        Returns:
            Output: A return code
                return_code: An integer number, status of component firmware auto-update
                    - return code of a positive number indicates successful auto-update
                        - status_installed = 1
                        - status_updated = 2
                        - status_scheduled = 3
                    - return_code of a negative number indicates failed auto-update
                        - status_err_boot_type = -1
                        - status_err_image = -2
                        - status_err_unknown = -3

        Raises:
            RuntimeError: auto-update failure cause
        """
        raise NotImplementedError

    # for firmwared
    def get_name_by_restful(self):
        raise NotImplementedError
    def get_firmware_version_by_restful(self):
        raise NotImplementedError
    def get_firmware_version_path(self):
        raise NotImplementedError

    def get_presence(self):
        """
        Retrieves the presence of the component

        Returns:
            boolean: True for present
        """
        if self.get_firmware_version() != CommonCfg.NULL_VALUE:
            return True
        return False

    def _get_bmc_version(self):
        """
        Retrieves the firmware version of bmc

        Returns:
            string: The firmware versions of the bmc
        """
        if self.method == CommonCfg.BY_RESTFUL:
            if self.new_restful:
                return self.plat_comm.get_bmc_version_by_restful()

            fw_info = self.plat_comm.get_bmc_firmware_info_by_rawcmd()
            if fw_info:
                current = fw_info.get("current_boot")
                return fw_info.get(current.lower())

            return CommonCfg.NULL_VALUE

        return self.__get_bmc_version_by_ipmi()


    def _get_bios_version(self):
        """
        Retrieves the firmware version of bios

        Returns:
            string: The firmware versions of the bios
        """
        version = self.plat_comm.read_file(CommonCfg.BIOS_VERSION_SYSFS_PATH)
        if self.plat_comm.is_valid_value(version):
            return version
        return CommonCfg.NULL_VALUE

    def _get_uboot_version(self):
        """
        Retrieves the firmware version of uboot

        Returns:
            string: The firmware versions of the uboot
        """
        if hasattr(hooks, "get_uboot_version"):
            return hooks.get_uboot_version()

        uboot_version = CommonCfg.NULL_VALUE
        try:
            with open("/dev/mtd0", buffering=0, mode="r", encoding="utf-8") as uboot_fp:
                uboot_fp.seek(0xf00000)
                output = uboot_fp.read(30)
                uboot_version = re.findall(r"[\d]+\.[\d]+\.[\d]+", output)[0]
        except (KeyError, ValueError) as err:
            self.plat_comm.log_error("Get uboot version error:{}".format(str(err)))
        return uboot_version

    def _get_cpld_version(self):
        """
        Retrieves the firmware version of cpld

        Returns:
            string: The firmware versions of the cpld
        """
        if self.method == CommonCfg.BY_SYSFS:
            version = self.plat_comm.read_file(self.__get_file_path("firmware_version"))
            if self.plat_comm.is_valid_value(version):
                return version
            return CommonCfg.NULL_VALUE

        return self.plat_comm.get_cpld_version_by_restful(self.name)

    def __get_bmc_version_by_ipmi(self):
        """
        Retrieves BMC firmware version by ipmitool

        Returns:
            string: The firmware versions of the bmc
        """
        version = CommonCfg.NULL_VALUE

        ret, output = self.plat_comm.exec_system_cmd(CommonCfg.BMC_VERSION_IPMI_CMD)
        if ret:
            return CommonCfg.NULL_VALUE

        try:
            byte_list = output.split()
            version = ".".join([
                str(int(byte_list[2])),
                str(int(byte_list[3])),
                str(int(byte_list[11]))
            ])
        except (TypeError, ValueError) as ver_error:
            self.plat_comm.log_error("Get bmc version error:{}".format(str(ver_error)))

        return version

    def get_current_boot_flash(self):
        """
        Retrieves the current booting flash of component

        Returns:
            string: 'master' or 'slave', "N/A" for Fail
        """
        if self.comp_type in [CommonCfg.COMPONENT_TYPE_BIOS, CommonCfg.COMPONENT_TYPE_UBOOT]:
            if hasattr(hooks, "get_bios_current_boot_flash"):
                return hooks.get_bios_current_boot_flash()

            flash_path = os.path.join(CommonCfg.S3IP_EXTEND_PATH, "fw_ctl/bios_boot_flash_status")
            flash = self.plat_comm.read_file(flash_path)
            if self.plat_comm.is_valid_value(flash):
                try:
                    if int(flash, 0) == 1:
                        return "master"
                    if int(flash, 0) == 0:
                        return "slave"
                except (TypeError, ValueError) as err:
                    self.plat_comm.log_error(str(err))

        if self.comp_type == CommonCfg.COMPONENT_TYPE_BMC:
            if self.new_restful:
                flash, _ = self.plat_comm.get_bmc_boot_flash_by_restful()
                return flash

            fw_info = self.plat_comm.get_bmc_firmware_info_by_rawcmd()
            if fw_info:
                return fw_info.get("current_boot")
            return CommonCfg.NULL_VALUE

        if self.comp_type in [CommonCfg.COMPONENT_TYPE_CPLD, CommonCfg.COMPONENT_TYPE_FPGA]:
            if self.method == CommonCfg.BY_RESTFUL:
                return self.plat_comm.get_cpld_boot_flash_by_restful(self.name)

            flash_path = self.__get_file_path("boot_flash_status")
            flash = self.plat_comm.read_file(flash_path)
            if self.plat_comm.is_valid_value(flash):
                try:
                    if self.comp_type == CommonCfg.COMPONENT_TYPE_CPLD:
                        if int(flash, 0) == 1:
                            return "internal"
                        if int(flash, 0) == 2:
                            return "external"
                    else:
                        if int(flash, 0) == 1:
                            return "external"
                        if int(flash, 0) == 2:
                            return "internal"
                except (TypeError, ValueError) as err:
                    self.plat_comm.log_error(str(err))

        return CommonCfg.NULL_VALUE

    def set_next_boot_flash(self, flash):
        """
        Set the next booting flash of component

        Returns:
            Boolean: True for success
        """
        if self.comp_type in [CommonCfg.COMPONENT_TYPE_BIOS, CommonCfg.COMPONENT_TYPE_UBOOT]:
            if hasattr(hooks, "set_bios_next_boot_flash"):
                return hooks.set_bios_next_boot_flash(flash)

            flash_path = os.path.join(CommonCfg.S3IP_EXTEND_PATH, "fw_ctl/bios_boot_flash_select")
            value = 0 if flash == "master" else 1
            return self.plat_comm.write_file(flash_path, value)

        if self.comp_type == CommonCfg.COMPONENT_TYPE_BMC:
            if self.new_restful:
                return self.plat_comm.set_bmc_boot_flash_by_restful(flash)
            
            return self.plat_comm.set_bmc_boot_flash_by_rawcmd(flash)

        # unsupported
        return False

    def get_next_boot_flash(self):
        """
        Retrieves the next booting flash of component

        Returns:
            string: 'master' or 'slave', "N/A" for Fail
        """
        if self.comp_type in [CommonCfg.COMPONENT_TYPE_BIOS, CommonCfg.COMPONENT_TYPE_UBOOT]:
            if hasattr(hooks, "get_bios_next_boot_flash"):
                return hooks.get_bios_next_boot_flash()

            flash_path = os.path.join(CommonCfg.S3IP_EXTEND_PATH, "fw_ctl/bios_boot_flash_select")
            flash = self.plat_comm.read_file(flash_path)
            if self.plat_comm.is_valid_value(flash):
                try:
                    if int(flash, 0) == 0:
                        return "master"
                    if int(flash, 0) == 1:
                        return "slave"
                except (TypeError, ValueError) as err:
                    self.plat_comm.log_error(str(err))

        if self.comp_type == CommonCfg.COMPONENT_TYPE_BMC:
            if self.new_restful:
                _, flash = self.plat_comm.get_bmc_boot_flash_by_restful()
                return flash
            
            fw_info = self.plat_comm.get_bmc_firmware_info_by_rawcmd()
            if fw_info:
                return fw_info.get("next_boot")

        # unsupported
        return CommonCfg.NULL_VALUE
