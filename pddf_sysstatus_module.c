/*
 * Copyright 2019 Broadcom.
 * The term “Broadcom” refers to Broadcom Inc. and/or its subsidiaries.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 *
 * A pddf kernel module for system status registers
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/jiffies.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/sysfs.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/dmi.h>
#include <linux/kobject.h>
#include <linux/string.h>
#include "pddf_client_defs.h"
#include "pddf_sysstatus_defs.h"

#define MAIN_VERSION_MASK        0xc0
#define SECOND_VERSION_MASK      0x30
#define THIRD_VERSION_MASK       0x0f
#define HALF_VERSION_MASK        0xf0

#define PORT_MAX_NUM 128
static int plug_record_cache[PORT_MAX_NUM] = {0};
static struct mutex plug_record_cache_lock;

SYSSTATUS_DATA sysstatus_data = {0};

extern int board_i2c_cpld_read_new(unsigned short cpld_addr, char *name, u8 reg);
extern int board_i2c_cpld_write_new(unsigned short cpld_addr, char *name, u8 reg, u8 value);

static ssize_t do_attr_operation(struct device *dev, struct device_attribute *da, const char *buf, size_t count);
ssize_t show_sysstatus_data(struct device *dev, struct device_attribute *da, char *buf);
ssize_t show_sysstatus_version(struct device *dev, struct device_attribute *da, char *buf);
ssize_t show_cpld_version_two_part(struct device *dev, struct device_attribute *da, char *buf);
ssize_t store_sysstatus_data(struct device *dev, struct device_attribute *da, const char *buf, size_t count);
ssize_t show_sysstatus_word_data(struct device *dev, struct device_attribute *da, char *buf);
ssize_t store_sysstatus_word_data(struct device *dev, struct device_attribute *da, const char *buf, size_t count);
ssize_t show_sysstatus_word_data_reverse(struct device *dev, struct device_attribute *da, char *buf);
ssize_t store_sysstatus_word_data_reverse(struct device *dev, struct device_attribute *da, const char *buf, size_t count);
ssize_t show_sysstatus_wdt_delay(struct device *dev, struct device_attribute *da, char *buf);
ssize_t store_sysstatus_wdt_delay(struct device *dev, struct device_attribute *da, const char *buf, size_t count);
ssize_t show_sysstatus_wdt_strb_interval(struct device *dev, struct device_attribute *da, char *buf);
ssize_t store_sysstatus_wdt_strb_interval(struct device *dev, struct device_attribute *da, const char *buf, size_t count);
ssize_t show_led_ctrl_byte(struct device *dev, struct device_attribute *da, char *buf);
ssize_t store_led_ctrl_byte(struct device *dev, struct device_attribute *da, const char *buf, size_t count);
ssize_t show_plug_record_cache(struct device *dev, struct device_attribute *da, char *buf);
ssize_t show_opposite_bit(struct device *dev, struct device_attribute *da, char *buf);
ssize_t store_opposite_bit(struct device *dev, struct device_attribute *da, const char *buf, size_t count);

PDDF_DATA_ATTR(attr_name, S_IWUSR|S_IRUGO, show_pddf_data, store_pddf_data, PDDF_CHAR, 32,
             (void*)&sysstatus_data.sysstatus_addr_attr.aname, NULL);
PDDF_DATA_ATTR(devname, S_IWUSR|S_IRUGO, show_pddf_data, store_pddf_data, PDDF_CHAR, 32,
             (void*)&sysstatus_data.sysstatus_addr_attr.devname, NULL);
PDDF_DATA_ATTR(attr_devaddr, S_IWUSR|S_IRUGO, show_pddf_data, store_pddf_data, PDDF_UINT32,
              sizeof(uint32_t),  (void*)&sysstatus_data.sysstatus_addr_attr.devaddr , NULL);
PDDF_DATA_ATTR(attr_offset, S_IWUSR|S_IRUGO, show_pddf_data, store_pddf_data, PDDF_UINT32,
              sizeof(uint32_t), (void*)&sysstatus_data.sysstatus_addr_attr.offset, NULL);
PDDF_DATA_ATTR(attr_mask, S_IWUSR|S_IRUGO, show_pddf_data, store_pddf_data, PDDF_UINT32,
              sizeof(uint32_t), (void*)&sysstatus_data.sysstatus_addr_attr.mask , NULL);
PDDF_DATA_ATTR(attr_len, S_IWUSR|S_IRUGO, show_pddf_data, store_pddf_data, PDDF_UINT32,
              sizeof(uint32_t), (void*)&sysstatus_data.sysstatus_addr_attr.len , NULL);
PDDF_DATA_ATTR(attr_ops, S_IWUSR, NULL, do_attr_operation, PDDF_CHAR, 8, (void*)&sysstatus_data, NULL);



static struct attribute *sysstatus_addr_attributes[] = {
    &attr_attr_name.dev_attr.attr,
    &attr_devname.dev_attr.attr,
    &attr_attr_devaddr.dev_attr.attr,
    &attr_attr_offset.dev_attr.attr,
    &attr_attr_mask.dev_attr.attr,
    &attr_attr_len.dev_attr.attr,
    &attr_attr_ops.dev_attr.attr,
    NULL
};

PDDF_DATA_ATTR(board_info, S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, 32, NULL, NULL);
PDDF_DATA_ATTR(cpld1_version, S_IRUGO, show_sysstatus_version, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld2_version, S_IRUGO, show_sysstatus_version, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld3_version, S_IRUGO, show_sysstatus_version, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld4_version, S_IRUGO, show_sysstatus_version, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld5_version, S_IRUGO, show_sysstatus_version, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld6_version, S_IRUGO, show_sysstatus_version, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld7_version, S_IRUGO, show_sysstatus_version, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld8_version, S_IRUGO, show_sysstatus_version, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld9_version, S_IRUGO, show_sysstatus_version, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld10_version, S_IRUGO, show_sysstatus_version, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld11_version, S_IRUGO, show_sysstatus_version, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld12_version, S_IRUGO, show_sysstatus_version, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld13_version, S_IRUGO, show_sysstatus_version, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld14_version, S_IRUGO, show_sysstatus_version, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld15_version, S_IRUGO, show_sysstatus_version, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld16_version, S_IRUGO, show_sysstatus_version, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld17_version, S_IRUGO, show_sysstatus_version, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld18_version, S_IRUGO, show_sysstatus_version, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld19_version, S_IRUGO, show_sysstatus_version, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld20_version, S_IRUGO, show_sysstatus_version, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld21_version, S_IRUGO, show_sysstatus_version, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld22_version, S_IRUGO, show_sysstatus_version, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld23_version, S_IRUGO, show_sysstatus_version, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld24_version, S_IRUGO, show_sysstatus_version, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld25_version, S_IRUGO, show_sysstatus_version, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld26_version, S_IRUGO, show_sysstatus_version, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld27_version, S_IRUGO, show_sysstatus_version, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld28_version, S_IRUGO, show_sysstatus_version, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld29_version, S_IRUGO, show_sysstatus_version, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld30_version, S_IRUGO, show_sysstatus_version, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld31_version, S_IRUGO, show_sysstatus_version, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld32_version, S_IRUGO, show_sysstatus_version, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);

PDDF_DATA_ATTR(cpld1_ver_two_part, S_IRUGO, show_cpld_version_two_part, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld2_ver_two_part, S_IRUGO, show_cpld_version_two_part, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld3_ver_two_part, S_IRUGO, show_cpld_version_two_part, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld4_ver_two_part, S_IRUGO, show_cpld_version_two_part, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld5_ver_two_part, S_IRUGO, show_cpld_version_two_part, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld6_ver_two_part, S_IRUGO, show_cpld_version_two_part, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld7_ver_two_part, S_IRUGO, show_cpld_version_two_part, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld8_ver_two_part, S_IRUGO, show_cpld_version_two_part, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld9_ver_two_part, S_IRUGO, show_cpld_version_two_part, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld10_ver_two_part, S_IRUGO, show_cpld_version_two_part, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld11_ver_two_part, S_IRUGO, show_cpld_version_two_part, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld12_ver_two_part, S_IRUGO, show_cpld_version_two_part, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld13_ver_two_part, S_IRUGO, show_cpld_version_two_part, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld14_ver_two_part, S_IRUGO, show_cpld_version_two_part, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld15_ver_two_part, S_IRUGO, show_cpld_version_two_part, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld16_ver_two_part, S_IRUGO, show_cpld_version_two_part, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld17_ver_two_part, S_IRUGO, show_cpld_version_two_part, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld18_ver_two_part, S_IRUGO, show_cpld_version_two_part, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld19_ver_two_part, S_IRUGO, show_cpld_version_two_part, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld20_ver_two_part, S_IRUGO, show_cpld_version_two_part, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld21_ver_two_part, S_IRUGO, show_cpld_version_two_part, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld22_ver_two_part, S_IRUGO, show_cpld_version_two_part, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld23_ver_two_part, S_IRUGO, show_cpld_version_two_part, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld24_ver_two_part, S_IRUGO, show_cpld_version_two_part, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld25_ver_two_part, S_IRUGO, show_cpld_version_two_part, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld26_ver_two_part, S_IRUGO, show_cpld_version_two_part, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld27_ver_two_part, S_IRUGO, show_cpld_version_two_part, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld28_ver_two_part, S_IRUGO, show_cpld_version_two_part, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld29_ver_two_part, S_IRUGO, show_cpld_version_two_part, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld30_ver_two_part, S_IRUGO, show_cpld_version_two_part, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld31_ver_two_part, S_IRUGO, show_cpld_version_two_part, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld32_ver_two_part, S_IRUGO, show_cpld_version_two_part, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);

PDDF_DATA_ATTR(rw_test_1, S_IWUSR|S_IRUGO|S_IWGRP, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(rw_test_2, S_IWUSR|S_IRUGO|S_IWGRP, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(rw_test_3, S_IWUSR|S_IRUGO|S_IWGRP, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(rw_test_4, S_IWUSR|S_IRUGO|S_IWGRP, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(rw_test_5, S_IWUSR|S_IRUGO|S_IWGRP, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(rw_test_6, S_IWUSR|S_IRUGO|S_IWGRP, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(rw_test_7, S_IWUSR|S_IRUGO|S_IWGRP, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(rw_test_8, S_IWUSR|S_IRUGO|S_IWGRP, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(rw_test_9, S_IWUSR|S_IRUGO|S_IWGRP, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(rw_test_10, S_IWUSR|S_IRUGO|S_IWGRP, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(rw_test_11, S_IWUSR|S_IRUGO|S_IWGRP, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(rw_test_12, S_IWUSR|S_IRUGO|S_IWGRP, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(rw_test_13, S_IWUSR|S_IRUGO|S_IWGRP, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(rw_test_14, S_IWUSR|S_IRUGO|S_IWGRP, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(rw_test_15, S_IWUSR|S_IRUGO|S_IWGRP, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(rw_test_16, S_IWUSR|S_IRUGO|S_IWGRP, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(rw_test_17, S_IWUSR|S_IRUGO|S_IWGRP, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(rw_test_18, S_IWUSR|S_IRUGO|S_IWGRP, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(rw_test_19, S_IWUSR|S_IRUGO|S_IWGRP, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(rw_test_20, S_IWUSR|S_IRUGO|S_IWGRP, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(rw_test_21, S_IWUSR|S_IRUGO|S_IWGRP, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(rw_test_22, S_IWUSR|S_IRUGO|S_IWGRP, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(rw_test_23, S_IWUSR|S_IRUGO|S_IWGRP, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(rw_test_24, S_IWUSR|S_IRUGO|S_IWGRP, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(rw_test_25, S_IWUSR|S_IRUGO|S_IWGRP, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(rw_test_26, S_IWUSR|S_IRUGO|S_IWGRP, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(rw_test_27, S_IWUSR|S_IRUGO|S_IWGRP, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(rw_test_28, S_IWUSR|S_IRUGO|S_IWGRP, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(rw_test_29, S_IWUSR|S_IRUGO|S_IWGRP, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(rw_test_30, S_IWUSR|S_IRUGO|S_IWGRP, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(rw_test_31, S_IWUSR|S_IRUGO|S_IWGRP, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(rw_test_32, S_IWUSR|S_IRUGO|S_IWGRP, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);

PDDF_DATA_ATTR(cpld1_flash_status, S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld2_flash_status, S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld3_flash_status, S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld4_flash_status, S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld5_flash_status, S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld6_flash_status, S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld7_flash_status, S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld8_flash_status, S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld9_flash_status, S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld10_flash_status, S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld11_flash_status, S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld12_flash_status, S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld13_flash_status, S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld14_flash_status, S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld15_flash_status, S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld16_flash_status, S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);

PDDF_DATA_ATTR(os_control, S_IWUSR|S_IRUGO|S_IWGRP, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);

PDDF_DATA_ATTR(power_module_status, S_IWUSR|S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(system_reset1, S_IWUSR|S_IRUGO, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(system_reset2, S_IWUSR|S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(system_reset3, S_IWUSR|S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(system_reset4, S_IWUSR|S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(system_reset5, S_IWUSR|S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(system_reset6, S_IWUSR|S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(system_reset7, S_IWUSR|S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(system_reset8, S_IWUSR|S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(misc1, S_IWUSR|S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(misc2, S_IWUSR|S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(misc3, S_IWUSR|S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);

PDDF_DATA_ATTR(bios_update_select_bmc, S_IWUSR|S_IRUGO|S_IWGRP, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(bios_flash_select, S_IWUSR|S_IRUGO|S_IWGRP, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(bios_flash_select_en, S_IWUSR|S_IRUGO|S_IWGRP, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(primary_bios_wp, S_IWUSR|S_IRUGO|S_IWGRP, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(bios_update_enable, S_IWUSR|S_IRUGO|S_IWGRP, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(bios_update_primary, S_IWUSR|S_IRUGO|S_IWGRP, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(bios_update_secondary, S_IWUSR|S_IRUGO|S_IWGRP, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(pri_bios_boot_success, S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(sec_bios_boot_success, S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(global_reset, S_IWUSR|S_IRUGO|S_IWGRP, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld_me_rcvr, S_IWUSR|S_IRUGO|S_IWGRP, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpu_fru_wp, S_IWUSR|S_IRUGO|S_IWGRP, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(smb_fru_wp, S_IWUSR|S_IRUGO|S_IWGRP, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(lc_fru_wp, S_IWUSR|S_IRUGO|S_IWGRP, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpu_reboot_type, S_IWUSR|S_IRUGO|S_IWGRP, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port_cpld1_txdisable_wp, S_IWUSR|S_IRUGO|S_IWGRP, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port_cpld2_txdisable_wp, S_IWUSR|S_IRUGO|S_IWGRP, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);

PDDF_DATA_ATTR(cpu_caterr_3v3, S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpu_error2_3v3, S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpu_error1_3v3, S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpu_error0_3v3, S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpu_nmi, S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpu_smi, S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpu_thermtrip_out, S_IRUGO, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpu_thermaltrip_times, S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpu_caterr_record, S_IWUSR|S_IRUGO|S_IWGRP, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpu_catter_times, S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);

PDDF_DATA_ATTR(wdt_en_pltrst_rst_mask, S_IWUSR|S_IRUGO|S_IWGRP, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(wdt_en, S_IWUSR|S_IRUGO|S_IWGRP, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(wdt_sta, S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(wdt_times_config, S_IWUSR|S_IRUGO|S_IWGRP, show_sysstatus_word_data, store_sysstatus_word_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);

/*For cpu watchdog time config, Different from wdt_time_config,  its two byte use big-endian mode, small addr is high byte*/
PDDF_DATA_ATTR(wdt_times_config_be, S_IWUSR|S_IRUGO|S_IWGRP, show_sysstatus_word_data_reverse, store_sysstatus_word_data_reverse, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);

PDDF_DATA_ATTR(wdt_remain_time, S_IRUGO, show_sysstatus_word_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);

PDDF_DATA_ATTR(wdt_delay, S_IWUSR|S_IRUGO|S_IWGRP, show_sysstatus_wdt_delay, store_sysstatus_wdt_delay, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(watchdog_strb_interval, S_IWUSR|S_IRUGO|S_IWGRP, show_sysstatus_wdt_strb_interval, store_sysstatus_wdt_strb_interval, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);

PDDF_DATA_ATTR(switch_temp, S_IWUSR|S_IRUGO, show_sysstatus_word_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);

PDDF_DATA_ATTR(port_led_ctrl_en_1, S_IWUSR|S_IRUGO, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port_led_ctrl_1, S_IWUSR|S_IRUGO, show_led_ctrl_byte, store_led_ctrl_byte, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);

PDDF_DATA_ATTR(port_led_ctrl_en_2, S_IWUSR|S_IRUGO, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port_led_ctrl_2, S_IWUSR|S_IRUGO, show_led_ctrl_byte, store_led_ctrl_byte, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);

PDDF_DATA_ATTR(port_led_ctrl_en_3, S_IWUSR|S_IRUGO, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port_led_ctrl_3, S_IWUSR|S_IRUGO, show_led_ctrl_byte, store_led_ctrl_byte, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);

/* port led status follow s3ip spec */
PDDF_DATA_ATTR(port_led_ctrl_s3ip_1, S_IWUSR|S_IRUGO, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port_led_ctrl_s3ip_2, S_IWUSR|S_IRUGO, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port_led_ctrl_s3ip_3, S_IWUSR|S_IRUGO, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);

PDDF_DATA_ATTR(pwrgd_vr_1, S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(pwrgd_vr_2, S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(pwrgd_vr_3, S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(pwrgd_vr_4, S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld_program_sta_1, S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld_program_sta_2, S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld_program_sta_3, S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld_program_sta_4, S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld_program_sta_5, S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld_program_sta_6, S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld_program_sta_7, S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld_program_sta_8, S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld_program_sta_9, S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(cpld_program_sta_10, S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(board_idx_1, S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(board_idx_2, S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(board_idx_3, S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(board_idx_4, S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(board_idx_5, S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(board_idx_6, S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(board_idx_7, S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(board_idx_8, S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(board_idx_9, S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(board_idx_10, S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(board_id_1, S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(board_id_2, S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(board_id_3, S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(board_id_4, S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(board_id_5, S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(board_id_6, S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(board_id_7, S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(board_id_8, S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(board_id_9, S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(board_id_10, S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(hw_ver_1, S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(hw_ver_2, S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(hw_ver_3, S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(hw_ver_4, S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(hw_ver_5, S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(hw_ver_6, S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(hw_ver_7, S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(hw_ver_8, S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(hw_ver_9, S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(hw_ver_10, S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(net_linkup_sta, S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(net_speed_sta, S_IRUGO, show_sysstatus_data, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);

PDDF_DATA_ATTR(pwren_vr_1, S_IWUSR|S_IRUGO, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(pwren_vr_2, S_IWUSR|S_IRUGO, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(pwren_vr_3, S_IWUSR|S_IRUGO, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(pwren_vr_4, S_IWUSR|S_IRUGO, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port_pwr_en, S_IWUSR|S_IRUGO, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port_pwr_en_1, S_IWUSR|S_IRUGO, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port_pwr_en_2, S_IWUSR|S_IRUGO, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port_pwr_en_3, S_IWUSR|S_IRUGO, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port_pwr_en_4, S_IWUSR|S_IRUGO, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port_pwr_en_5, S_IWUSR|S_IRUGO, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(pwr_wp, S_IWUSR|S_IRUGO, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(sw_init_enable_1, S_IWUSR|S_IRUGO, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(sw_init_enable_2, S_IWUSR|S_IRUGO, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(sw_init_enable_3, S_IWUSR|S_IRUGO, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);

PDDF_DATA_ATTR(pwren_ctrl_wp, S_IWUSR|S_IRUGO, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);

PDDF_DATA_ATTR(fcb_fru_wp, S_IWUSR|S_IRUGO, show_opposite_bit, store_opposite_bit, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(fcb_wdt_en, S_IWUSR|S_IRUGO, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(fcb_wdt_sta, S_IRUGO, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(fcb_wdt_config, S_IWUSR|S_IRUGO, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);

PDDF_DATA_ATTR(sys_led_ctrl_wp, S_IWUSR|S_IRUGO, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(fan_led_ctrl_wp, S_IWUSR|S_IRUGO, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(bmc_led_ctrl_wp, S_IWUSR|S_IRUGO, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(psu_led_ctrl_wp, S_IWUSR|S_IRUGO, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(id_led_ctrl_wp, S_IWUSR|S_IRUGO, show_sysstatus_data, store_sysstatus_data, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);

PDDF_DATA_ATTR(port1_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port2_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port3_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port4_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port5_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port6_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port7_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port8_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);

PDDF_DATA_ATTR(port9_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port10_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port11_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port12_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port13_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port14_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port15_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port16_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);

PDDF_DATA_ATTR(port17_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port18_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port19_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port20_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port21_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port22_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port23_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port24_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);

PDDF_DATA_ATTR(port25_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port26_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port27_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port28_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port29_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port30_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port31_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port32_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);

PDDF_DATA_ATTR(port33_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port34_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port35_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port36_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port37_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port38_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port39_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port40_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);

PDDF_DATA_ATTR(port41_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port42_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port43_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port44_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port45_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port46_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port47_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port48_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);

PDDF_DATA_ATTR(port49_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port50_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port51_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port52_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port53_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port54_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port55_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port56_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);

PDDF_DATA_ATTR(port57_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port58_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port59_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port60_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port61_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port62_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port63_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port64_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);

PDDF_DATA_ATTR(port65_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port66_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port67_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port68_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port69_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port70_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port71_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port72_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);

PDDF_DATA_ATTR(port73_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port74_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port75_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port76_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port77_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port78_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port79_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port80_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);

PDDF_DATA_ATTR(port81_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port82_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port83_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port84_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port85_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port86_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port87_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port88_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);

PDDF_DATA_ATTR(port89_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port90_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port91_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port92_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port93_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port94_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port95_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port96_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);

PDDF_DATA_ATTR(port97_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port98_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port99_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port100_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port101_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port102_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port103_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port104_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);

PDDF_DATA_ATTR(port105_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port106_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port107_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port108_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port109_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port110_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port111_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port112_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);

PDDF_DATA_ATTR(port113_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port114_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port115_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port116_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port117_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port118_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port119_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port120_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);

PDDF_DATA_ATTR(port121_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port122_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port123_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port124_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port125_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port126_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port127_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);
PDDF_DATA_ATTR(port128_plug_record, S_IRUGO, show_plug_record_cache, NULL, PDDF_UINT32, sizeof(uint32_t), NULL, NULL);


static struct attribute *sysstatus_data_attributes[] = {
    &attr_board_info.dev_attr.attr,
    &attr_cpld1_version.dev_attr.attr,
    &attr_cpld2_version.dev_attr.attr,
    &attr_cpld3_version.dev_attr.attr,
    &attr_cpld4_version.dev_attr.attr,
    &attr_cpld5_version.dev_attr.attr,
    &attr_cpld6_version.dev_attr.attr,
    &attr_cpld7_version.dev_attr.attr,
    &attr_cpld8_version.dev_attr.attr,
    &attr_cpld9_version.dev_attr.attr,
    &attr_cpld10_version.dev_attr.attr,
    &attr_cpld11_version.dev_attr.attr,
    &attr_cpld12_version.dev_attr.attr,
    &attr_cpld13_version.dev_attr.attr,
    &attr_cpld14_version.dev_attr.attr,
    &attr_cpld15_version.dev_attr.attr,
    &attr_cpld16_version.dev_attr.attr,
    &attr_cpld17_version.dev_attr.attr,
    &attr_cpld18_version.dev_attr.attr,
    &attr_cpld19_version.dev_attr.attr,
    &attr_cpld20_version.dev_attr.attr,
    &attr_cpld21_version.dev_attr.attr,
    &attr_cpld22_version.dev_attr.attr,
    &attr_cpld23_version.dev_attr.attr,
    &attr_cpld24_version.dev_attr.attr,
    &attr_cpld25_version.dev_attr.attr,
    &attr_cpld26_version.dev_attr.attr,
    &attr_cpld27_version.dev_attr.attr,
    &attr_cpld28_version.dev_attr.attr,
    &attr_cpld29_version.dev_attr.attr,
    &attr_cpld30_version.dev_attr.attr,
    &attr_cpld31_version.dev_attr.attr,
    &attr_cpld32_version.dev_attr.attr,

    &attr_cpld1_ver_two_part.dev_attr.attr,
    &attr_cpld2_ver_two_part.dev_attr.attr,
    &attr_cpld3_ver_two_part.dev_attr.attr,
    &attr_cpld4_ver_two_part.dev_attr.attr,
    &attr_cpld5_ver_two_part.dev_attr.attr,
    &attr_cpld6_ver_two_part.dev_attr.attr,
    &attr_cpld7_ver_two_part.dev_attr.attr,
    &attr_cpld8_ver_two_part.dev_attr.attr,
    &attr_cpld9_ver_two_part.dev_attr.attr,
    &attr_cpld10_ver_two_part.dev_attr.attr,
    &attr_cpld11_ver_two_part.dev_attr.attr,
    &attr_cpld12_ver_two_part.dev_attr.attr,
    &attr_cpld13_ver_two_part.dev_attr.attr,
    &attr_cpld14_ver_two_part.dev_attr.attr,
    &attr_cpld15_ver_two_part.dev_attr.attr,
    &attr_cpld16_ver_two_part.dev_attr.attr,
    &attr_cpld17_ver_two_part.dev_attr.attr,
    &attr_cpld18_ver_two_part.dev_attr.attr,
    &attr_cpld19_ver_two_part.dev_attr.attr,
    &attr_cpld20_ver_two_part.dev_attr.attr,
    &attr_cpld21_ver_two_part.dev_attr.attr,
    &attr_cpld22_ver_two_part.dev_attr.attr,
    &attr_cpld23_ver_two_part.dev_attr.attr,
    &attr_cpld24_ver_two_part.dev_attr.attr,
    &attr_cpld25_ver_two_part.dev_attr.attr,
    &attr_cpld26_ver_two_part.dev_attr.attr,
    &attr_cpld27_ver_two_part.dev_attr.attr,
    &attr_cpld28_ver_two_part.dev_attr.attr,
    &attr_cpld29_ver_two_part.dev_attr.attr,
    &attr_cpld30_ver_two_part.dev_attr.attr,
    &attr_cpld31_ver_two_part.dev_attr.attr,
    &attr_cpld32_ver_two_part.dev_attr.attr,

    &attr_rw_test_1.dev_attr.attr,
    &attr_rw_test_2.dev_attr.attr,
    &attr_rw_test_3.dev_attr.attr,
    &attr_rw_test_4.dev_attr.attr,
    &attr_rw_test_5.dev_attr.attr,
    &attr_rw_test_6.dev_attr.attr,
    &attr_rw_test_7.dev_attr.attr,
    &attr_rw_test_8.dev_attr.attr,
    &attr_rw_test_9.dev_attr.attr,
    &attr_rw_test_10.dev_attr.attr,
    &attr_rw_test_11.dev_attr.attr,
    &attr_rw_test_12.dev_attr.attr,
    &attr_rw_test_13.dev_attr.attr,
    &attr_rw_test_14.dev_attr.attr,
    &attr_rw_test_15.dev_attr.attr,
    &attr_rw_test_16.dev_attr.attr,
    &attr_rw_test_17.dev_attr.attr,
    &attr_rw_test_18.dev_attr.attr,
    &attr_rw_test_19.dev_attr.attr,
    &attr_rw_test_20.dev_attr.attr,
    &attr_rw_test_21.dev_attr.attr,
    &attr_rw_test_22.dev_attr.attr,
    &attr_rw_test_23.dev_attr.attr,
    &attr_rw_test_24.dev_attr.attr,
    &attr_rw_test_25.dev_attr.attr,
    &attr_rw_test_26.dev_attr.attr,
    &attr_rw_test_27.dev_attr.attr,
    &attr_rw_test_28.dev_attr.attr,
    &attr_rw_test_29.dev_attr.attr,
    &attr_rw_test_30.dev_attr.attr,
    &attr_rw_test_31.dev_attr.attr,
    &attr_rw_test_32.dev_attr.attr,

    &attr_cpld1_flash_status.dev_attr.attr,
    &attr_cpld2_flash_status.dev_attr.attr,
    &attr_cpld3_flash_status.dev_attr.attr,
    &attr_cpld4_flash_status.dev_attr.attr,
    &attr_cpld5_flash_status.dev_attr.attr,
    &attr_cpld6_flash_status.dev_attr.attr,
    &attr_cpld7_flash_status.dev_attr.attr,
    &attr_cpld8_flash_status.dev_attr.attr,
    &attr_cpld9_flash_status.dev_attr.attr,
    &attr_cpld10_flash_status.dev_attr.attr,
    &attr_cpld11_flash_status.dev_attr.attr,
    &attr_cpld12_flash_status.dev_attr.attr,
    &attr_cpld13_flash_status.dev_attr.attr,
    &attr_cpld14_flash_status.dev_attr.attr,
    &attr_cpld15_flash_status.dev_attr.attr,
    &attr_cpld16_flash_status.dev_attr.attr,

    &attr_os_control.dev_attr.attr,

    &attr_power_module_status.dev_attr.attr,
    &attr_system_reset1.dev_attr.attr,
    &attr_system_reset2.dev_attr.attr,
    &attr_system_reset3.dev_attr.attr,
    &attr_system_reset4.dev_attr.attr,
    &attr_system_reset5.dev_attr.attr,
    &attr_system_reset6.dev_attr.attr,
    &attr_system_reset7.dev_attr.attr,
    &attr_system_reset8.dev_attr.attr,
    &attr_misc1.dev_attr.attr,
    &attr_misc2.dev_attr.attr,
    &attr_misc3.dev_attr.attr,

    &attr_bios_update_select_bmc.dev_attr.attr,
    &attr_bios_flash_select.dev_attr.attr,
    &attr_bios_flash_select_en.dev_attr.attr,
    &attr_primary_bios_wp.dev_attr.attr,
    &attr_bios_update_enable.dev_attr.attr,
    &attr_bios_update_primary.dev_attr.attr,
    &attr_bios_update_secondary.dev_attr.attr,
    &attr_pri_bios_boot_success.dev_attr.attr,
    &attr_sec_bios_boot_success.dev_attr.attr,
    &attr_global_reset.dev_attr.attr,
    &attr_cpld_me_rcvr.dev_attr.attr,
    &attr_cpu_fru_wp.dev_attr.attr,
    &attr_smb_fru_wp.dev_attr.attr,
    &attr_lc_fru_wp.dev_attr.attr,
    &attr_cpu_reboot_type.dev_attr.attr,
    &attr_port_cpld1_txdisable_wp.dev_attr.attr,
    &attr_port_cpld2_txdisable_wp.dev_attr.attr,

    &attr_cpu_caterr_3v3.dev_attr.attr,
    &attr_cpu_error2_3v3.dev_attr.attr,
    &attr_cpu_error1_3v3.dev_attr.attr,
    &attr_cpu_error0_3v3.dev_attr.attr,
    &attr_cpu_nmi.dev_attr.attr,
    &attr_cpu_smi.dev_attr.attr,
    &attr_cpu_thermtrip_out.dev_attr.attr,
    &attr_cpu_thermaltrip_times.dev_attr.attr,
    &attr_cpu_caterr_record.dev_attr.attr,
    &attr_cpu_catter_times.dev_attr.attr,

    &attr_wdt_en_pltrst_rst_mask.dev_attr.attr,
    &attr_wdt_en.dev_attr.attr,
    &attr_wdt_sta.dev_attr.attr,
    &attr_wdt_times_config.dev_attr.attr,
    &attr_wdt_times_config_be.dev_attr.attr,
    &attr_wdt_remain_time.dev_attr.attr,

    &attr_wdt_delay.dev_attr.attr,
    &attr_watchdog_strb_interval.dev_attr.attr,

    &attr_switch_temp.dev_attr.attr,

    &attr_port_led_ctrl_en_1.dev_attr.attr,
    &attr_port_led_ctrl_1.dev_attr.attr,

    &attr_port_led_ctrl_en_2.dev_attr.attr,
    &attr_port_led_ctrl_2.dev_attr.attr,

    &attr_port_led_ctrl_en_3.dev_attr.attr,
    &attr_port_led_ctrl_3.dev_attr.attr,

    &attr_port_led_ctrl_s3ip_1.dev_attr.attr,
    &attr_port_led_ctrl_s3ip_2.dev_attr.attr,
    &attr_port_led_ctrl_s3ip_3.dev_attr.attr,

    &attr_pwrgd_vr_1.dev_attr.attr,
    &attr_pwrgd_vr_2.dev_attr.attr,
    &attr_pwrgd_vr_3.dev_attr.attr,
    &attr_pwrgd_vr_4.dev_attr.attr,
    &attr_pwren_vr_1.dev_attr.attr,
    &attr_pwren_vr_2.dev_attr.attr,
    &attr_pwren_vr_3.dev_attr.attr,
    &attr_pwren_vr_4.dev_attr.attr,

    &attr_cpld_program_sta_1.dev_attr.attr,
    &attr_cpld_program_sta_2.dev_attr.attr,
    &attr_cpld_program_sta_3.dev_attr.attr,
    &attr_cpld_program_sta_4.dev_attr.attr,
    &attr_cpld_program_sta_5.dev_attr.attr,
    &attr_cpld_program_sta_6.dev_attr.attr,
    &attr_cpld_program_sta_7.dev_attr.attr,
    &attr_cpld_program_sta_8.dev_attr.attr,
    &attr_cpld_program_sta_9.dev_attr.attr,
    &attr_cpld_program_sta_10.dev_attr.attr,
    &attr_board_idx_1.dev_attr.attr,
    &attr_board_idx_2.dev_attr.attr,
    &attr_board_idx_3.dev_attr.attr,
    &attr_board_idx_4.dev_attr.attr,
    &attr_board_idx_5.dev_attr.attr,
    &attr_board_idx_6.dev_attr.attr,
    &attr_board_idx_7.dev_attr.attr,
    &attr_board_idx_8.dev_attr.attr,
    &attr_board_idx_9.dev_attr.attr,
    &attr_board_idx_10.dev_attr.attr,
    &attr_board_id_1.dev_attr.attr,
    &attr_board_id_2.dev_attr.attr,
    &attr_board_id_3.dev_attr.attr,
    &attr_board_id_4.dev_attr.attr,
    &attr_board_id_5.dev_attr.attr,
    &attr_board_id_6.dev_attr.attr,
    &attr_board_id_7.dev_attr.attr,
    &attr_board_id_8.dev_attr.attr,
    &attr_board_id_9.dev_attr.attr,
    &attr_board_id_10.dev_attr.attr,
    &attr_hw_ver_1.dev_attr.attr,
    &attr_hw_ver_2.dev_attr.attr,
    &attr_hw_ver_3.dev_attr.attr,
    &attr_hw_ver_4.dev_attr.attr,
    &attr_hw_ver_5.dev_attr.attr,
    &attr_hw_ver_6.dev_attr.attr,
    &attr_hw_ver_7.dev_attr.attr,
    &attr_hw_ver_8.dev_attr.attr,
    &attr_hw_ver_9.dev_attr.attr,
    &attr_hw_ver_10.dev_attr.attr,
    &attr_net_linkup_sta.dev_attr.attr,
    &attr_net_speed_sta.dev_attr.attr,

    &attr_port_pwr_en.dev_attr.attr,
    &attr_port_pwr_en_1.dev_attr.attr,
    &attr_port_pwr_en_2.dev_attr.attr,
    &attr_port_pwr_en_3.dev_attr.attr,
    &attr_port_pwr_en_4.dev_attr.attr,
    &attr_port_pwr_en_5.dev_attr.attr,
    &attr_pwr_wp.dev_attr.attr,
    &attr_sw_init_enable_1.dev_attr.attr,
    &attr_sw_init_enable_2.dev_attr.attr,
    &attr_sw_init_enable_3.dev_attr.attr,

    &attr_pwren_ctrl_wp.dev_attr.attr,

    &attr_fcb_fru_wp.dev_attr.attr,

    &attr_fcb_wdt_en.dev_attr.attr,
    &attr_fcb_wdt_sta.dev_attr.attr,
    &attr_fcb_wdt_config.dev_attr.attr,

    &attr_sys_led_ctrl_wp.dev_attr.attr,
    &attr_fan_led_ctrl_wp.dev_attr.attr,
    &attr_psu_led_ctrl_wp.dev_attr.attr,
    &attr_bmc_led_ctrl_wp.dev_attr.attr,
    &attr_id_led_ctrl_wp.dev_attr.attr,

    &attr_port1_plug_record.dev_attr.attr,
    &attr_port2_plug_record.dev_attr.attr,
    &attr_port3_plug_record.dev_attr.attr,
    &attr_port4_plug_record.dev_attr.attr,
    &attr_port5_plug_record.dev_attr.attr,
    &attr_port6_plug_record.dev_attr.attr,
    &attr_port7_plug_record.dev_attr.attr,
    &attr_port8_plug_record.dev_attr.attr,

    &attr_port9_plug_record.dev_attr.attr,
    &attr_port10_plug_record.dev_attr.attr,
    &attr_port11_plug_record.dev_attr.attr,
    &attr_port12_plug_record.dev_attr.attr,
    &attr_port13_plug_record.dev_attr.attr,
    &attr_port14_plug_record.dev_attr.attr,
    &attr_port15_plug_record.dev_attr.attr,
    &attr_port16_plug_record.dev_attr.attr,

    &attr_port17_plug_record.dev_attr.attr,
    &attr_port18_plug_record.dev_attr.attr,
    &attr_port19_plug_record.dev_attr.attr,
    &attr_port20_plug_record.dev_attr.attr,
    &attr_port21_plug_record.dev_attr.attr,
    &attr_port22_plug_record.dev_attr.attr,
    &attr_port23_plug_record.dev_attr.attr,
    &attr_port24_plug_record.dev_attr.attr,

    &attr_port25_plug_record.dev_attr.attr,
    &attr_port26_plug_record.dev_attr.attr,
    &attr_port27_plug_record.dev_attr.attr,
    &attr_port28_plug_record.dev_attr.attr,
    &attr_port29_plug_record.dev_attr.attr,
    &attr_port30_plug_record.dev_attr.attr,
    &attr_port31_plug_record.dev_attr.attr,
    &attr_port32_plug_record.dev_attr.attr,

    &attr_port33_plug_record.dev_attr.attr,
    &attr_port34_plug_record.dev_attr.attr,
    &attr_port35_plug_record.dev_attr.attr,
    &attr_port36_plug_record.dev_attr.attr,
    &attr_port37_plug_record.dev_attr.attr,
    &attr_port38_plug_record.dev_attr.attr,
    &attr_port39_plug_record.dev_attr.attr,
    &attr_port40_plug_record.dev_attr.attr,

    &attr_port41_plug_record.dev_attr.attr,
    &attr_port42_plug_record.dev_attr.attr,
    &attr_port43_plug_record.dev_attr.attr,
    &attr_port44_plug_record.dev_attr.attr,
    &attr_port45_plug_record.dev_attr.attr,
    &attr_port46_plug_record.dev_attr.attr,
    &attr_port47_plug_record.dev_attr.attr,
    &attr_port48_plug_record.dev_attr.attr,

    &attr_port49_plug_record.dev_attr.attr,
    &attr_port50_plug_record.dev_attr.attr,
    &attr_port51_plug_record.dev_attr.attr,
    &attr_port52_plug_record.dev_attr.attr,
    &attr_port53_plug_record.dev_attr.attr,
    &attr_port54_plug_record.dev_attr.attr,
    &attr_port55_plug_record.dev_attr.attr,
    &attr_port56_plug_record.dev_attr.attr,

    &attr_port57_plug_record.dev_attr.attr,
    &attr_port58_plug_record.dev_attr.attr,
    &attr_port59_plug_record.dev_attr.attr,
    &attr_port60_plug_record.dev_attr.attr,
    &attr_port61_plug_record.dev_attr.attr,
    &attr_port62_plug_record.dev_attr.attr,
    &attr_port63_plug_record.dev_attr.attr,
    &attr_port64_plug_record.dev_attr.attr,

    &attr_port65_plug_record.dev_attr.attr,
    &attr_port66_plug_record.dev_attr.attr,
    &attr_port67_plug_record.dev_attr.attr,
    &attr_port68_plug_record.dev_attr.attr,
    &attr_port69_plug_record.dev_attr.attr,
    &attr_port70_plug_record.dev_attr.attr,
    &attr_port71_plug_record.dev_attr.attr,
    &attr_port72_plug_record.dev_attr.attr,

    &attr_port73_plug_record.dev_attr.attr,
    &attr_port74_plug_record.dev_attr.attr,
    &attr_port75_plug_record.dev_attr.attr,
    &attr_port76_plug_record.dev_attr.attr,
    &attr_port77_plug_record.dev_attr.attr,
    &attr_port78_plug_record.dev_attr.attr,
    &attr_port79_plug_record.dev_attr.attr,
    &attr_port80_plug_record.dev_attr.attr,

    &attr_port81_plug_record.dev_attr.attr,
    &attr_port82_plug_record.dev_attr.attr,
    &attr_port83_plug_record.dev_attr.attr,
    &attr_port84_plug_record.dev_attr.attr,
    &attr_port85_plug_record.dev_attr.attr,
    &attr_port86_plug_record.dev_attr.attr,
    &attr_port87_plug_record.dev_attr.attr,
    &attr_port88_plug_record.dev_attr.attr,

    &attr_port89_plug_record.dev_attr.attr,
    &attr_port90_plug_record.dev_attr.attr,
    &attr_port91_plug_record.dev_attr.attr,
    &attr_port92_plug_record.dev_attr.attr,
    &attr_port93_plug_record.dev_attr.attr,
    &attr_port94_plug_record.dev_attr.attr,
    &attr_port95_plug_record.dev_attr.attr,
    &attr_port96_plug_record.dev_attr.attr,

    &attr_port97_plug_record.dev_attr.attr,
    &attr_port98_plug_record.dev_attr.attr,
    &attr_port99_plug_record.dev_attr.attr,
    &attr_port100_plug_record.dev_attr.attr,
    &attr_port101_plug_record.dev_attr.attr,
    &attr_port102_plug_record.dev_attr.attr,
    &attr_port103_plug_record.dev_attr.attr,
    &attr_port104_plug_record.dev_attr.attr,

    &attr_port105_plug_record.dev_attr.attr,
    &attr_port106_plug_record.dev_attr.attr,
    &attr_port107_plug_record.dev_attr.attr,
    &attr_port108_plug_record.dev_attr.attr,
    &attr_port109_plug_record.dev_attr.attr,
    &attr_port110_plug_record.dev_attr.attr,
    &attr_port111_plug_record.dev_attr.attr,
    &attr_port112_plug_record.dev_attr.attr,

    &attr_port113_plug_record.dev_attr.attr,
    &attr_port114_plug_record.dev_attr.attr,
    &attr_port115_plug_record.dev_attr.attr,
    &attr_port116_plug_record.dev_attr.attr,
    &attr_port117_plug_record.dev_attr.attr,
    &attr_port118_plug_record.dev_attr.attr,
    &attr_port119_plug_record.dev_attr.attr,
    &attr_port120_plug_record.dev_attr.attr,

    &attr_port121_plug_record.dev_attr.attr,
    &attr_port122_plug_record.dev_attr.attr,
    &attr_port123_plug_record.dev_attr.attr,
    &attr_port124_plug_record.dev_attr.attr,
    &attr_port125_plug_record.dev_attr.attr,
    &attr_port126_plug_record.dev_attr.attr,
    &attr_port127_plug_record.dev_attr.attr,
    &attr_port128_plug_record.dev_attr.attr,


    NULL
};



static const struct attribute_group pddf_sysstatus_addr_group = {
    .attrs = sysstatus_addr_attributes,
};


static const struct attribute_group pddf_sysstatus_data_group = {
    .attrs = sysstatus_data_attributes,
};


static struct kobject *sysstatus_addr_kobj;
static struct kobject *sysstatus_data_kobj;


ssize_t show_sysstatus_version(struct device *dev, struct device_attribute *da, char *buf)
{

    struct sensor_device_attribute *attr = to_sensor_dev_attr(da);

    SYSSTATUS_DATA *data = &sysstatus_data;
    struct SYSSTATUS_ADDR_ATTR *sysstatus_addr_attrs = NULL;
    int i, status;
    u8 bitmask;
    int primary_version;
    int secondary_version;
    int third_version;

    if(attr==NULL)
    {
        return -EINVAL;
    }

    for (i=0;i<MAX_ATTRS;i++)
    {
        if (strcmp(data->sysstatus_addr_attrs[i].aname, attr->dev_attr.attr.name) == 0 )
        {
            sysstatus_addr_attrs = &data->sysstatus_addr_attrs[i];

        }
    }

    if (sysstatus_addr_attrs==NULL )
    {
        pddf_err(SYSSTATUS, "%s is not supported attribute for this client\n",data->sysstatus_addr_attrs[i].aname);
        status = 0;
        return -EINVAL;
    }
    else
    {
        if (sysstatus_addr_attrs->devaddr==0xff)
        {
            return -EINVAL;
        }
        status = board_i2c_cpld_read_new(sysstatus_addr_attrs->devaddr, sysstatus_addr_attrs->devname, sysstatus_addr_attrs->offset);
    }

    if (sysstatus_addr_attrs->mask == 0xff) {
        status = status&sysstatus_addr_attrs->mask;
    }
    else
    {
        bitmask = (1 << sysstatus_addr_attrs->len) - 1;
        status = (status >> sysstatus_addr_attrs->mask) & bitmask;
    }

    primary_version = ((status & MAIN_VERSION_MASK) >> 6) + 1;
    secondary_version = (status & SECOND_VERSION_MASK) >> 4;
    third_version = status & THIRD_VERSION_MASK;

    return snprintf(buf, PAGE_SIZE, "%d.%d.%d\n", primary_version, secondary_version, third_version);
}

ssize_t show_cpld_version_two_part(struct device *dev, struct device_attribute *da, char *buf)
{

    struct sensor_device_attribute *attr = to_sensor_dev_attr(da);

    SYSSTATUS_DATA *data = &sysstatus_data;
    struct SYSSTATUS_ADDR_ATTR *sysstatus_addr_attrs = NULL;
    int i, status;
    u8 bitmask;
    int primary_version;
    int secondary_version;

    if(attr==NULL)
    {
        return -EINVAL;
    }

    for (i=0;i<MAX_ATTRS;i++)
    {
        if (strcmp(data->sysstatus_addr_attrs[i].aname, attr->dev_attr.attr.name) == 0 )
        {
            sysstatus_addr_attrs = &data->sysstatus_addr_attrs[i];

        }
    }

    if (sysstatus_addr_attrs==NULL )
    {
        pddf_err(SYSSTATUS, "%s is not supported attribute for this client\n",data->sysstatus_addr_attrs[i].aname);
        status = 0;
        return -EINVAL;
    }
    else
    {
        if (sysstatus_addr_attrs->devaddr==0xff)
        {
            return -EINVAL;
        }
        status = board_i2c_cpld_read_new(sysstatus_addr_attrs->devaddr, sysstatus_addr_attrs->devname, sysstatus_addr_attrs->offset);
    }

    if (sysstatus_addr_attrs->mask == 0xff) {
        status = status&sysstatus_addr_attrs->mask;
    }
    else
    {
        bitmask = (1 << sysstatus_addr_attrs->len) - 1;
        status = (status >> sysstatus_addr_attrs->mask) & bitmask;
    }

    primary_version = (status & HALF_VERSION_MASK) >> 4;
    secondary_version = status & THIRD_VERSION_MASK;

    return snprintf(buf, PAGE_SIZE, "%d.%d\n", primary_version, secondary_version);
}

ssize_t show_opposite_bit(struct device *dev, struct device_attribute *da, char *buf)
{

    struct sensor_device_attribute *attr = to_sensor_dev_attr(da);

    SYSSTATUS_DATA *data = &sysstatus_data;
    struct SYSSTATUS_ADDR_ATTR *sysstatus_addr_attrs = NULL;
    int i, status;
    u8 bitmask;

    if(attr==NULL)
    {
        return -EINVAL;
    }

    for (i=0;i<MAX_ATTRS;i++)
    {
        if (strcmp(data->sysstatus_addr_attrs[i].aname, attr->dev_attr.attr.name) == 0 )
        {
            sysstatus_addr_attrs = &data->sysstatus_addr_attrs[i];

        }
    }

    if (sysstatus_addr_attrs==NULL )
    {
        pddf_err(SYSSTATUS, "%s is not supported attribute for this client\n",data->sysstatus_addr_attrs[i].aname);
        status = 0;
        return -EINVAL;
    }
    else
    {
        if (sysstatus_addr_attrs->devaddr==0xff)
        {
            return -EINVAL;
        }
        status = board_i2c_cpld_read_new(sysstatus_addr_attrs->devaddr, sysstatus_addr_attrs->devname, sysstatus_addr_attrs->offset);
    }

    if (sysstatus_addr_attrs->mask == 0xff) {
        return sprintf(buf, "%d\n", (status&sysstatus_addr_attrs->mask));
    }
    else
    {
        bitmask = (1 << sysstatus_addr_attrs->len) - 1;
        status = (status >> sysstatus_addr_attrs->mask) & bitmask;
        return sprintf(buf, "%d\n", !status);
    }
}

ssize_t store_opposite_bit(struct device *dev, struct device_attribute *da, const char *buf, size_t count)
{
    int ret = 0;
    u8 val = 0;
    u8 bitmask;
    char *pattern = "pwren_vr";

    struct sensor_device_attribute *attr = to_sensor_dev_attr(da);

    SYSSTATUS_DATA *data = &sysstatus_data;
    struct SYSSTATUS_ADDR_ATTR *sysstatus_addr_attrs = NULL;
    int i, status, currval, wr_val;

    if(attr==NULL)
    {
        return -EINVAL;
    }

    ret = kstrtou8(buf, 0, &val);
	if (ret < 0)
    {
        pddf_err(SYSSTATUS, "Not support input value!\n");
        return ret;
    }

    for (i=0;i<MAX_ATTRS;i++)
    {
        if (strcmp(data->sysstatus_addr_attrs[i].aname, attr->dev_attr.attr.name) == 0 )
        {
            sysstatus_addr_attrs = &data->sysstatus_addr_attrs[i];

        }
    }

    if (sysstatus_addr_attrs==NULL )
    {
        pddf_err(SYSSTATUS, "%s is not supported attribute for this client\n",data->sysstatus_addr_attrs[i].aname);
        status = 0;
        return -EINVAL;
    }
    else
    {
        if (sysstatus_addr_attrs->devaddr==0xff)
        {
            return -EINVAL;
        }

        if (sysstatus_addr_attrs->mask == 0xff) {
            status = board_i2c_cpld_write_new(sysstatus_addr_attrs->devaddr, sysstatus_addr_attrs->devname, sysstatus_addr_attrs->offset, val);
            if (status >= 0) {
                return count;
            }
            else
            {
                return status;
            }
        }
        else
        {
            bitmask = (1 << sysstatus_addr_attrs->len) - 1;
            if(val > bitmask)
            {
                return -EINVAL;
            }
            currval = board_i2c_cpld_read_new(sysstatus_addr_attrs->devaddr, sysstatus_addr_attrs->devname, sysstatus_addr_attrs->offset);
            if (currval < 0)
            {
                return currval;
            }
            else
            {
                wr_val = (currval & ~(bitmask << sysstatus_addr_attrs->mask)) | (!val & bitmask) << sysstatus_addr_attrs->mask;
                ret = board_i2c_cpld_write_new(sysstatus_addr_attrs->devaddr, sysstatus_addr_attrs->devname, sysstatus_addr_attrs->offset, wr_val);
                if (ret < 0)
                {
                    return ret;
                }
                else
                {
                    return count;
                }
            }
        }
    }
}

ssize_t show_sysstatus_data(struct device *dev, struct device_attribute *da, char *buf)
{

    struct sensor_device_attribute *attr = to_sensor_dev_attr(da);

    SYSSTATUS_DATA *data = &sysstatus_data;
    struct SYSSTATUS_ADDR_ATTR *sysstatus_addr_attrs = NULL;
    int i, status;
    u8 bitmask;

    if(attr==NULL)
    {
        return -EINVAL;
    }

    for (i=0;i<MAX_ATTRS;i++)
    {
        if (strcmp(data->sysstatus_addr_attrs[i].aname, attr->dev_attr.attr.name) == 0 )
        {
            sysstatus_addr_attrs = &data->sysstatus_addr_attrs[i];

        }
    }

    if (sysstatus_addr_attrs==NULL )
    {
        pddf_err(SYSSTATUS, "%s is not supported attribute for this client\n",data->sysstatus_addr_attrs[i].aname);
        status = 0;
        return -EINVAL;
    }
    else
    {
        if (sysstatus_addr_attrs->devaddr==0xff)
        {
            return -EINVAL;
        }
        status = board_i2c_cpld_read_new(sysstatus_addr_attrs->devaddr, sysstatus_addr_attrs->devname, sysstatus_addr_attrs->offset);
    }

    if (sysstatus_addr_attrs->mask == 0xff) {
        return sprintf(buf, "%d\n", (status&sysstatus_addr_attrs->mask));
    }
    else
    {
        bitmask = (1 << sysstatus_addr_attrs->len) - 1;
        status = (status >> sysstatus_addr_attrs->mask) & bitmask;
        return sprintf(buf, "%d\n", status);
    }
}

ssize_t store_sysstatus_data(struct device *dev, struct device_attribute *da, const char *buf, size_t count)
{
    int ret = 0;
    u8 val = 0;
    u8 bitmask;
    char *pattern = "pwren_vr";

    struct sensor_device_attribute *attr = to_sensor_dev_attr(da);

    SYSSTATUS_DATA *data = &sysstatus_data;
    struct SYSSTATUS_ADDR_ATTR *sysstatus_addr_attrs = NULL;
    int i, status, currval, wr_val;

    if(attr==NULL)
    {
        return -EINVAL;
    }

    ret = kstrtou8(buf, 0, &val);
	if (ret < 0)
    {
        pddf_err(SYSSTATUS, "Not support input value!\n");
        return ret;
    }

    for (i=0;i<MAX_ATTRS;i++)
    {
        if (strcmp(data->sysstatus_addr_attrs[i].aname, attr->dev_attr.attr.name) == 0 )
        {
            sysstatus_addr_attrs = &data->sysstatus_addr_attrs[i];

        }
    }

    if (sysstatus_addr_attrs==NULL )
    {
        pddf_err(SYSSTATUS, "%s is not supported attribute for this client\n",data->sysstatus_addr_attrs[i].aname);
        status = 0;
        return -EINVAL;
    }
    else
    {
        if (sysstatus_addr_attrs->devaddr==0xff)
        {
            return -EINVAL;
        }

        if (sysstatus_addr_attrs->mask == 0xff) {
            status = board_i2c_cpld_write_new(sysstatus_addr_attrs->devaddr, sysstatus_addr_attrs->devname, sysstatus_addr_attrs->offset, val);
            if (strncmp(pattern, attr->dev_attr.attr.name, strlen(pattern))==0) {
                pddf_info(SYSSTATUS, "sysstatus set %s = %d\n", attr->dev_attr.attr.name, val);
            }
            if (status >= 0) {
                return count;
            }
            else
            {
                return status;
            }
        }
        else
        {
            bitmask = (1 << sysstatus_addr_attrs->len) - 1;
            if(val > bitmask)
            {
                return -EINVAL;
            }
            currval = board_i2c_cpld_read_new(sysstatus_addr_attrs->devaddr, sysstatus_addr_attrs->devname, sysstatus_addr_attrs->offset);
            if (currval < 0)
            {
                return currval;
            }
            else
            {
                wr_val = (currval & ~(bitmask << sysstatus_addr_attrs->mask)) | (val & bitmask) << sysstatus_addr_attrs->mask;
                ret = board_i2c_cpld_write_new(sysstatus_addr_attrs->devaddr, sysstatus_addr_attrs->devname, sysstatus_addr_attrs->offset, wr_val);
                if (strncmp(pattern, attr->dev_attr.attr.name, strlen(pattern))==0) {
                   pddf_info(SYSSTATUS, "sysstatus set %s = %d\n", attr->dev_attr.attr.name, wr_val);
                }
                if (ret < 0)
                {
                    return ret;
                }
                else
                {
                    return count;
                }
            }
        }
    }
}

ssize_t show_led_ctrl_byte(struct device *dev, struct device_attribute *da, char *buf)
{

    struct sensor_device_attribute *attr = to_sensor_dev_attr(da);

    SYSSTATUS_DATA *data = &sysstatus_data;
    struct SYSSTATUS_ADDR_ATTR *sysstatus_addr_attrs = NULL;
    int i, status;
    u8 bitmask;

    if(attr==NULL)
    {
        return -EINVAL;
    }

    for (i=0;i<MAX_ATTRS;i++)
    {
        if (strcmp(data->sysstatus_addr_attrs[i].aname, attr->dev_attr.attr.name) == 0 )
        {
            sysstatus_addr_attrs = &data->sysstatus_addr_attrs[i];

        }
    }

    if (sysstatus_addr_attrs==NULL )
    {
        pddf_err(SYSSTATUS, "%s is not supported attribute for this client\n",data->sysstatus_addr_attrs[i].aname);
        status = 0;
        return -EINVAL;
    }
    else
    {
        if (sysstatus_addr_attrs->devaddr==0xff)
        {
            return -EINVAL;
        }
        status = board_i2c_cpld_read_new(sysstatus_addr_attrs->devaddr, sysstatus_addr_attrs->devname, sysstatus_addr_attrs->offset);
    }

    if (sysstatus_addr_attrs->mask == 0xff) {
        status = status&sysstatus_addr_attrs->mask;
        /* s3ip define 3 for red and 2 for yellow, reverse value*/
        if (status == 2)
            return sprintf(buf, "%d\n", 3);
        else if (status == 3)
            return sprintf(buf, "%d\n", 2);
        else
            return sprintf(buf, "%d\n", status);
    }
    else
    {
        bitmask = (1 << sysstatus_addr_attrs->len) - 1;
        status = (status >> sysstatus_addr_attrs->mask) & bitmask;
        /* s3ip define 3 for red and 2 for yellow, reverse value*/
        if (status == 2)
            return sprintf(buf, "%d\n", 3);
        else if ( (status == 3))
            return sprintf(buf, "%d\n", 2);
        else
            return sprintf(buf, "%d\n", status);
    }
}

ssize_t store_led_ctrl_byte(struct device *dev, struct device_attribute *da, const char *buf, size_t count)
{
    int ret = 0;
    u8 val = 0;
    u8 bitmask;

    struct sensor_device_attribute *attr = to_sensor_dev_attr(da);

    SYSSTATUS_DATA *data = &sysstatus_data;
    struct SYSSTATUS_ADDR_ATTR *sysstatus_addr_attrs = NULL;
    int i, status, currval, wr_val, set_val;

    if(attr==NULL)
    {
        return -EINVAL;
    }

    ret = kstrtou8(buf, 0, &val);
	if (ret < 0)
    {
        pddf_err(SYSSTATUS, "Not support input value!\n");
        return ret;
    }

    for (i=0;i<MAX_ATTRS;i++)
    {
        if (strcmp(data->sysstatus_addr_attrs[i].aname, attr->dev_attr.attr.name) == 0 )
        {
            sysstatus_addr_attrs = &data->sysstatus_addr_attrs[i];

        }
    }

    if (sysstatus_addr_attrs==NULL )
    {
        pddf_err(SYSSTATUS, "%s is not supported attribute for this client\n",data->sysstatus_addr_attrs[i].aname);
        status = 0;
        return -EINVAL;
    }
    else
    {
        if (sysstatus_addr_attrs->devaddr==0xff)
        {
            return -EINVAL;
        }

        if (sysstatus_addr_attrs->mask == 0xff) {
            /* s3ip define 3 for red and 2 for yellow, reverse value*/
            if (val == 2)
                wr_val = 3;
            else if (val == 3)
                wr_val = 2;
            else
                wr_val = val;

            status = board_i2c_cpld_write_new(sysstatus_addr_attrs->devaddr, sysstatus_addr_attrs->devname, sysstatus_addr_attrs->offset, wr_val);
            if (status >= 0) {
                return count;
            }
            else
            {
                return status;
            }
        }
        else
        {
            bitmask = (1 << sysstatus_addr_attrs->len) - 1;
            if(val > bitmask)
            {
                return -EINVAL;
            }
            /* s3ip define 3 for red and 2 for yellow, reverse value*/
            if (val == 2)
                set_val = 3;
            else if (val == 3)
                set_val = 2;
            else
                set_val = val;

            currval = board_i2c_cpld_read_new(sysstatus_addr_attrs->devaddr, sysstatus_addr_attrs->devname, sysstatus_addr_attrs->offset);
            if (currval < 0)
            {
                return currval;
            }
            else
            {
                wr_val = (currval & ~(bitmask << sysstatus_addr_attrs->mask)) | (set_val & bitmask) << sysstatus_addr_attrs->mask;
                ret = board_i2c_cpld_write_new(sysstatus_addr_attrs->devaddr, sysstatus_addr_attrs->devname, sysstatus_addr_attrs->offset, wr_val);
                if (ret < 0)
                {
                    return ret;
                }
                else
                {
                    return count;
                }
            }
        }
    }
}

ssize_t show_sysstatus_word_data_reverse(struct device *dev, struct device_attribute *da, char *buf)
{

    struct sensor_device_attribute *attr = to_sensor_dev_attr(da);

    SYSSTATUS_DATA *data = &sysstatus_data;
    struct SYSSTATUS_ADDR_ATTR *sysstatus_addr_attrs = NULL;
    int i, status_hi, status_lo;

    if(attr==NULL)
    {
        return -EINVAL;
    }

    for (i=0;i<MAX_ATTRS;i++)
    {
        if (strcmp(data->sysstatus_addr_attrs[i].aname, attr->dev_attr.attr.name) == 0 )
        {
            sysstatus_addr_attrs = &data->sysstatus_addr_attrs[i];

        }
    }

    if (sysstatus_addr_attrs==NULL )
    {
        pddf_err(SYSSTATUS, "%s is not supported attribute for this client\n",data->sysstatus_addr_attrs[i].aname);
        return -EINVAL;
    }
    else
    {
        if (sysstatus_addr_attrs->devaddr==0xff)
        {
            return -EINVAL;
        }

        status_hi = board_i2c_cpld_read_new(sysstatus_addr_attrs->devaddr, sysstatus_addr_attrs->devname, sysstatus_addr_attrs->offset + 1);
        if (status_hi < 0) {
            return status_hi;
        }

        status_lo = board_i2c_cpld_read_new(sysstatus_addr_attrs->devaddr, sysstatus_addr_attrs->devname, sysstatus_addr_attrs->offset);
        if (status_lo < 0) {
            return status_lo;
        }

        return snprintf(buf, PAGE_SIZE, "%d\n", (u8)status_hi | (u8)status_lo);
    }
}

ssize_t store_sysstatus_word_data_reverse(struct device *dev, struct device_attribute *da, const char *buf, size_t count)
{
    int ret = 0;
    u16 val = 0;

    struct sensor_device_attribute *attr = to_sensor_dev_attr(da);

    SYSSTATUS_DATA *data = &sysstatus_data;
    struct SYSSTATUS_ADDR_ATTR *sysstatus_addr_attrs = NULL;
    int i, status;

    if(attr==NULL)
    {
        return -EINVAL;
    }

    ret = kstrtou16(buf, 0, &val);
	if (ret < 0)
    {
        pddf_err(SYSSTATUS, "Not support input value!\n");
        return ret;
    }

    for (i=0;i<MAX_ATTRS;i++)
    {
        if (strcmp(data->sysstatus_addr_attrs[i].aname, attr->dev_attr.attr.name) == 0 )
        {
            sysstatus_addr_attrs = &data->sysstatus_addr_attrs[i];
        }
    }

    if (sysstatus_addr_attrs==NULL )
    {
        pddf_err(SYSSTATUS, "%s is not supported attribute for this client\n",data->sysstatus_addr_attrs[i].aname);
        status = 0;

        return -EINVAL;
    }
    else
    {
        if (sysstatus_addr_attrs->devaddr==0xff)
        {
            return -EINVAL;
        }

        status = board_i2c_cpld_write_new(sysstatus_addr_attrs->devaddr, sysstatus_addr_attrs->devname, sysstatus_addr_attrs->offset + 1, (val >> 8) & 0xff);
        if (status < 0)
        {
            return status;
        }

        status = board_i2c_cpld_write_new(sysstatus_addr_attrs->devaddr, sysstatus_addr_attrs->devname, sysstatus_addr_attrs->offset, val & 0xff);
        if (status < 0)
        {
            return status;
        }

        return count;
    }
}

ssize_t show_sysstatus_word_data(struct device *dev, struct device_attribute *da, char *buf)
{

    struct sensor_device_attribute *attr = to_sensor_dev_attr(da);

    SYSSTATUS_DATA *data = &sysstatus_data;
    struct SYSSTATUS_ADDR_ATTR *sysstatus_addr_attrs = NULL;
    int i, status_hi, status_lo;
    u8 u8_hi, u8_lo;

    if(attr==NULL)
    {
        return -EINVAL;
    }

    for (i=0;i<MAX_ATTRS;i++)
    {
        if (strcmp(data->sysstatus_addr_attrs[i].aname, attr->dev_attr.attr.name) == 0 )
        {
            sysstatus_addr_attrs = &data->sysstatus_addr_attrs[i];

        }
    }

    if (sysstatus_addr_attrs==NULL )
    {
        pddf_err(SYSSTATUS, "%s is not supported attribute for this client\n",data->sysstatus_addr_attrs[i].aname);
        return -EINVAL;
    }
    else
    {
        if (sysstatus_addr_attrs->devaddr==0xff)
        {
            return -EINVAL;
        }

        status_hi = board_i2c_cpld_read_new(sysstatus_addr_attrs->devaddr, sysstatus_addr_attrs->devname, sysstatus_addr_attrs->offset);
        if (status_hi < 0) {
            return status_hi;
        }
        else{
            u8_hi = (u8)status_hi;
        }

        status_lo = board_i2c_cpld_read_new(sysstatus_addr_attrs->devaddr, sysstatus_addr_attrs->devname, sysstatus_addr_attrs->offset + 1);
        if (status_lo < 0) {
            return status_lo;
        }
        else{
            u8_lo = (u8)status_lo;
        }

        return snprintf(buf, PAGE_SIZE, "%d\n", (u8_hi << 8) | u8_lo);
    }
}

ssize_t store_sysstatus_word_data(struct device *dev, struct device_attribute *da, const char *buf, size_t count)
{
    int ret = 0;
    u16 val = 0;

    struct sensor_device_attribute *attr = to_sensor_dev_attr(da);

    SYSSTATUS_DATA *data = &sysstatus_data;
    struct SYSSTATUS_ADDR_ATTR *sysstatus_addr_attrs = NULL;
    int i, status;

    if(attr==NULL)
    {
        return -EINVAL;
    }

    ret = kstrtou16(buf, 0, &val);
	if (ret < 0)
    {
        pddf_err(SYSSTATUS, "Not support input value!\n");
        return ret;
    }

    for (i=0;i<MAX_ATTRS;i++)
    {
        if (strcmp(data->sysstatus_addr_attrs[i].aname, attr->dev_attr.attr.name) == 0 )
        {
            sysstatus_addr_attrs = &data->sysstatus_addr_attrs[i];
        }
    }

    if (sysstatus_addr_attrs==NULL )
    {
        pddf_err(SYSSTATUS, "%s is not supported attribute for this client\n",data->sysstatus_addr_attrs[i].aname);
        status = 0;

        return -EINVAL;
    }
    else
    {
        if (sysstatus_addr_attrs->devaddr==0xff)
        {
            return -EINVAL;
        }

        status = board_i2c_cpld_write_new(sysstatus_addr_attrs->devaddr, sysstatus_addr_attrs->devname, sysstatus_addr_attrs->offset, (val >> 8) & 0xff);
        if (status < 0)
        {
            return status;
        }

        status = board_i2c_cpld_write_new(sysstatus_addr_attrs->devaddr, sysstatus_addr_attrs->devname, sysstatus_addr_attrs->offset + 1, val & 0xff);
        if (status < 0)
        {
            return status;
        }

        return count;
    }
}

ssize_t show_sysstatus_wdt_strb_interval(struct device *dev, struct device_attribute *da, char *buf)
{

    struct sensor_device_attribute *attr = to_sensor_dev_attr(da);

    SYSSTATUS_DATA *data = &sysstatus_data;
    struct SYSSTATUS_ADDR_ATTR *sysstatus_addr_attrs = NULL;
    int i, status_hi, status_lo, interval;

    if(attr==NULL)
    {
        return -EINVAL;
    }

    for (i=0;i<MAX_ATTRS;i++)
    {
        if (strcmp(data->sysstatus_addr_attrs[i].aname, attr->dev_attr.attr.name) == 0 )
        {
            sysstatus_addr_attrs = &data->sysstatus_addr_attrs[i];

        }
    }

    if (sysstatus_addr_attrs==NULL )
    {
        pddf_err(SYSSTATUS, "%s is not supported attribute for this client\n",data->sysstatus_addr_attrs[i].aname);
        return -EINVAL;
    }
    else
    {
        if (sysstatus_addr_attrs->devaddr==0xff)
        {
            return -EINVAL;
        }

        status_lo = board_i2c_cpld_read_new(sysstatus_addr_attrs->devaddr, sysstatus_addr_attrs->devname, sysstatus_addr_attrs->offset);
        if (status_lo < 0) {
            return status_lo;
        }
        else {
            status_lo = status_lo & 0xff;
        }

        status_hi = board_i2c_cpld_read_new(sysstatus_addr_attrs->devaddr, sysstatus_addr_attrs->devname, sysstatus_addr_attrs->offset + 1);
        if (status_hi < 0) {
            return status_hi;
        }
        else {
            status_hi = status_hi & 0xff;
        }

        interval = ((status_hi << 8) | status_lo) & 0x1FFF;
        return sprintf(buf, "%d\n", interval);
    }
}

ssize_t store_sysstatus_wdt_strb_interval(struct device *dev, struct device_attribute *da, const char *buf, size_t count)
{
    int ret = 0;
    u16 val = 0;

    struct sensor_device_attribute *attr = to_sensor_dev_attr(da);

    SYSSTATUS_DATA *data = &sysstatus_data;
    struct SYSSTATUS_ADDR_ATTR *sysstatus_addr_attrs = NULL;
    int i, status;

    if(attr==NULL)
    {
        return -EINVAL;
    }

    ret = kstrtou16(buf, 0, &val);
	if (ret < 0)
    {
        pddf_err(SYSSTATUS, "Not support input value!\n");
        return ret;
    }

    for (i=0;i<MAX_ATTRS;i++)
    {
        if (strcmp(data->sysstatus_addr_attrs[i].aname, attr->dev_attr.attr.name) == 0 )
        {
            sysstatus_addr_attrs = &data->sysstatus_addr_attrs[i];
        }
    }

    if (sysstatus_addr_attrs==NULL )
    {
        pddf_err(SYSSTATUS, "%s is not supported attribute for this client\n",data->sysstatus_addr_attrs[i].aname);
        status = 0;

        return -EINVAL;
    }
    else
    {
        if (sysstatus_addr_attrs->devaddr==0xff)
        {
            return -EINVAL;
        }

        status = board_i2c_cpld_write_new(sysstatus_addr_attrs->devaddr, sysstatus_addr_attrs->devname, sysstatus_addr_attrs->offset, val & 0xff);
        if (status < 0)
        {
            return status;
        }

        status = board_i2c_cpld_write_new(sysstatus_addr_attrs->devaddr, sysstatus_addr_attrs->devname, sysstatus_addr_attrs->offset + 1, (val >> 8) & 0x1f);
        if (status < 0)
        {
            return status;
        }

        return count;
    }
}

ssize_t show_sysstatus_wdt_delay(struct device *dev, struct device_attribute *da, char *buf)
{

    struct sensor_device_attribute *attr = to_sensor_dev_attr(da);

    SYSSTATUS_DATA *data = &sysstatus_data;
    struct SYSSTATUS_ADDR_ATTR *sysstatus_addr_attrs = NULL;
    int i, status_hi, status_lo, delay;

    if(attr==NULL)
    {
        return -EINVAL;
    }

    for (i=0;i<MAX_ATTRS;i++)
    {
        if (strcmp(data->sysstatus_addr_attrs[i].aname, attr->dev_attr.attr.name) == 0 )
        {
            sysstatus_addr_attrs = &data->sysstatus_addr_attrs[i];

        }
    }

    if (sysstatus_addr_attrs==NULL )
    {
        pddf_err(SYSSTATUS, "%s is not supported attribute for this client\n",data->sysstatus_addr_attrs[i].aname);
        return -EINVAL;
    }
    else
    {
        if (sysstatus_addr_attrs->devaddr==0xff)
        {
            return -EINVAL;
        }

        status_hi = board_i2c_cpld_read_new(sysstatus_addr_attrs->devaddr, sysstatus_addr_attrs->devname, sysstatus_addr_attrs->offset);
        if (status_hi < 0) {
            return status_hi;
        } else {
            status_hi = status_hi & 0xff;
        }

        status_lo = board_i2c_cpld_read_new(sysstatus_addr_attrs->devaddr, sysstatus_addr_attrs->devname, sysstatus_addr_attrs->offset + 1);
        if (status_lo < 0) {
            return status_lo;
        } else {
            status_lo = status_lo & 0xff;
        }

        delay = (status_hi | (status_lo << 8)) & 0x03FF;

        return sprintf(buf, "%d\n", delay);
    }
}

ssize_t store_sysstatus_wdt_delay(struct device *dev, struct device_attribute *da, const char *buf, size_t count)
{
    int ret = 0;
    u16 val = 0;

    struct sensor_device_attribute *attr = to_sensor_dev_attr(da);

    SYSSTATUS_DATA *data = &sysstatus_data;
    struct SYSSTATUS_ADDR_ATTR *sysstatus_addr_attrs = NULL;
    int i, status;

    if(attr==NULL)
    {
        return -EINVAL;
    }

    ret = kstrtou16(buf, 0, &val);
	if (ret < 0)
    {
        pddf_err(SYSSTATUS, "Not support input value!\n");
        return ret;
    }

    for (i=0;i<MAX_ATTRS;i++)
    {
        if (strcmp(data->sysstatus_addr_attrs[i].aname, attr->dev_attr.attr.name) == 0 )
        {
            sysstatus_addr_attrs = &data->sysstatus_addr_attrs[i];
        }
    }

    if (sysstatus_addr_attrs==NULL )
    {
        pddf_err(SYSSTATUS, "%s is not supported attribute for this client\n",data->sysstatus_addr_attrs[i].aname);
        status = 0;

        return -EINVAL;
    }
    else
    {
        if (sysstatus_addr_attrs->devaddr==0xff)
        {
            return -EINVAL;
        }

        status = board_i2c_cpld_write_new(sysstatus_addr_attrs->devaddr, sysstatus_addr_attrs->devname, sysstatus_addr_attrs->offset, val & 0xff);
        if (status < 0)
        {
            return status;
        }

        status = board_i2c_cpld_write_new(sysstatus_addr_attrs->devaddr, sysstatus_addr_attrs->devname, sysstatus_addr_attrs->offset + 1, (val >> 8) & 0x3);
        if (status < 0)
        {
            return status;
        }

        return count;
    }
}

ssize_t show_plug_record_cache(struct device *dev, struct device_attribute *da, char *buf)
{

    struct sensor_device_attribute *attr = to_sensor_dev_attr(da);

    SYSSTATUS_DATA *data = &sysstatus_data;
    struct SYSSTATUS_ADDR_ATTR *sysstatus_addr_attrs = NULL;
    int i, j, status, index, offset, value;

    if(attr==NULL)
    {
        return -EINVAL;
    }

    for (i=0;i<MAX_ATTRS;i++)
    {
        if (strcmp(data->sysstatus_addr_attrs[i].aname, attr->dev_attr.attr.name) == 0 )
        {
            sysstatus_addr_attrs = &data->sysstatus_addr_attrs[i];
        }
    }

    if (sysstatus_addr_attrs==NULL )
    {
        pddf_err(SYSSTATUS, "%s is not supported attribute for this client\n",data->sysstatus_addr_attrs[i].aname);
        status = 0;
        return -EINVAL;
    }
    else
    {
        if (sysstatus_addr_attrs->devaddr==0xff)
        {
            return -EINVAL;
        }
        status = board_i2c_cpld_read_new(sysstatus_addr_attrs->devaddr, sysstatus_addr_attrs->devname, sysstatus_addr_attrs->offset);
    }

    if (sysstatus_addr_attrs->mask == 0xff) {
        return sprintf(buf, "%d\n", (status&sysstatus_addr_attrs->mask));
    }
    else
    {
        /* sysstatus_addr_attrs->len as port_index for user set in config file */
        /* sysstatus_addr_attrs->mask as port bit offset mask for user set in config file */
        index = sysstatus_addr_attrs->len - 1;
        if (index > PORT_MAX_NUM - 1) {
            pddf_err(SYSSTATUS, "Plug record index out of range index = %d\n",index);
            return -EINVAL;
        }
        /* Store plug_record byte value to find out port_index from start to end */
        mutex_lock(&plug_record_cache_lock);
        for (i=0; i<8; i++) {
            if ((status >> i) & 0x1) {
                /* Calculate the offset in plug_record_cache[MAX], index is Port index, mask is Bit offset in each Byte */
                for (j=0;j<MAX_ATTRS;j++) {
                    if ((strcmp(data->sysstatus_addr_attrs[j].devname, sysstatus_addr_attrs->devname) == 0) && \
                         (data->sysstatus_addr_attrs[j].offset == sysstatus_addr_attrs->offset) && \
                         (data->sysstatus_addr_attrs[j].mask == i)) {
                             offset = data->sysstatus_addr_attrs[j].len - 1;
                             if (offset > PORT_MAX_NUM - 1) {
                                pddf_err(SYSSTATUS, "Plug record index out of range index = %d\n", offset);
                                mutex_unlock(&plug_record_cache_lock);
                                return -EINVAL;
                            }
                            plug_record_cache[offset] = 1;
                            pddf_dbg(SYSSTATUS, "index = %d, offset = %d, sysstatus_addr_attrs->mask=%d plug_record_cache[offset]=%d\n", index, offset, sysstatus_addr_attrs->mask,
                    plug_record_cache[offset]);
                        }
                }
            }
        }

        value = plug_record_cache[index];

        /* After read byte to clear bit[x] */
        if (value) {
            plug_record_cache[index] = 0;
        }
        mutex_unlock(&plug_record_cache_lock);

        return sprintf(buf, "%d\n", value);
    }
}

static ssize_t do_attr_operation(struct device *dev, struct device_attribute *da, const char *buf, size_t count)
{
    PDDF_ATTR *ptr = (PDDF_ATTR *)da;
    SYSSTATUS_DATA *pdata = (SYSSTATUS_DATA *)(ptr->addr);

    if (pdata->len >= (MAX_ATTRS - 1)) {
        pr_err("PDDF SYSSTATUS pdata->len over max attrs, len=%d\n", pdata->len);
        return -EINVAL;
    }
    pdata->sysstatus_addr_attrs[pdata->len] = pdata->sysstatus_addr_attr;
    pdata->len++;
    pddf_dbg(SYSSTATUS, "%s: Populating the data for %s\n", __FUNCTION__, pdata->sysstatus_addr_attr.aname);
    memset(&pdata->sysstatus_addr_attr, 0, sizeof(pdata->sysstatus_addr_attr));


    return count;
}




int __init sysstatus_data_init(void)
{
    struct kobject *device_kobj;
    int ret = 0;


    pddf_dbg(SYSSTATUS, "PDDF SYSSTATUS MODULE.. init\n");

    mutex_init(&plug_record_cache_lock);

    device_kobj = get_device_i2c_kobj();
    if(!device_kobj)
        return -ENOMEM;

    sysstatus_addr_kobj = kobject_create_and_add("sysstatus", device_kobj);
    if(!sysstatus_addr_kobj)
        return -ENOMEM;

    sysstatus_data_kobj = kobject_create_and_add("sysstatus_data", sysstatus_addr_kobj);
    if(!sysstatus_data_kobj)
        return -ENOMEM;


    ret = sysfs_create_group(sysstatus_addr_kobj, &pddf_sysstatus_addr_group);
    if (ret)
    {
        kobject_put(sysstatus_addr_kobj);
        return ret;
    }

    ret = sysfs_create_group(sysstatus_data_kobj, &pddf_sysstatus_data_group);
    if (ret)
    {
        sysfs_remove_group(sysstatus_addr_kobj, &pddf_sysstatus_addr_group);
        kobject_put(sysstatus_data_kobj);
        kobject_put(sysstatus_addr_kobj);
        return ret;
    }

    pddf_info(SYSSTATUS, "PDDF SYSSTATUS MODULE.. init done\n");

    return ret;
}

void __exit sysstatus_data_exit(void)
{
    pddf_dbg(SYSSTATUS, "PDDF SYSSTATUS  MODULE.. exit\n");
    sysfs_remove_group(sysstatus_data_kobj, &pddf_sysstatus_data_group);
    sysfs_remove_group(sysstatus_addr_kobj, &pddf_sysstatus_addr_group);
    kobject_put(sysstatus_data_kobj);
    kobject_put(sysstatus_addr_kobj);
    pddf_dbg(SYSSTATUS, "%s: Removed the kobjects for 'SYSSTATUS'\n",__FUNCTION__);
    return;
}

module_init(sysstatus_data_init);
module_exit(sysstatus_data_exit);

MODULE_AUTHOR("Broadcom");
MODULE_DESCRIPTION("SYSSTATUS platform data");
MODULE_LICENSE("GPL");
