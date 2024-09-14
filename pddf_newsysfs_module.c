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
 * A pddf kernel module to create i2C client for optics
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
#include "pddf_newsysfs_defs.h"

static ssize_t do_attr_operation(struct device *dev, struct device_attribute *da, const char *buf, size_t count);
static ssize_t do_device_operation(struct device *dev, struct device_attribute *da, const char *buf, size_t count);
extern void* get_device_table(char *name);
extern void delete_device_table(char *name);

SYSFS_DATA sysfs_data = {0};

/* SYSFS CACHE CLIENT DATA */
PDDF_DATA_ATTR(attr_name, S_IWUSR|S_IRUGO, show_pddf_data, store_pddf_data, PDDF_CHAR, 32, (void*)&sysfs_data.sysfs_attr.aname, NULL);
PDDF_DATA_ATTR(attr_mode, S_IWUSR|S_IRUGO, show_pddf_data, store_pddf_data, PDDF_CHAR, 32, (void*)&sysfs_data.sysfs_attr.mode, NULL);
PDDF_DATA_ATTR(attr_value, S_IWUSR|S_IRUGO, show_pddf_data, store_pddf_data, PDDF_CHAR, 32, (void*)&sysfs_data.sysfs_attr.value, NULL);
PDDF_DATA_ATTR(attr_ops, S_IWUSR, NULL, do_attr_operation, PDDF_CHAR, 8, (void*)&sysfs_data, NULL);
PDDF_DATA_ATTR(dev_ops, S_IWUSR, NULL, do_device_operation, PDDF_CHAR, 8, (void*)&sysfs_data, (void*)&pddf_data);


static struct attribute *sysfs_attributes[] = {
    &attr_attr_name.dev_attr.attr,
    &attr_attr_value.dev_attr.attr,
    &attr_attr_mode.dev_attr.attr,
    &attr_attr_ops.dev_attr.attr,
    &attr_dev_ops.dev_attr.attr,
    NULL
};

static const struct attribute_group pddf_newsysfs_client_data_group = {
    .attrs = sysfs_attributes,
};

void remove_trailing_newline(char *str)
{
    size_t len = strlen(str);

    if (len > 0 &&(str[len - 1] == '\n' || str[len - 1] == '\r')) {
        str[len - 1] = '\0';
    }
}

static ssize_t do_attr_operation(struct device *dev, struct device_attribute *da, const char *buf, size_t count)
{
    PDDF_ATTR *ptr = (PDDF_ATTR *)da;
    SYSFS_DATA *pdata = (SYSFS_DATA *)(ptr->addr);

    pdata->sysfs_attrs[pdata->len] = pdata->sysfs_attr;
    pdata->idx++;
    pdata->len++;
    memset(&pdata->sysfs_attr, 0, sizeof(pdata->sysfs_attr));

    return count;
}

#define VAL_NUM_MAX 3000
#define VAL_LEN_MAX 32

static char val_char[VAL_NUM_MAX][VAL_LEN_MAX];

ssize_t show_sysstatus_value(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct sensor_device_attribute *sda = to_sensor_dev_attr(attr);

    if (sda->index >= VAL_NUM_MAX)
        return -EINVAL;

    snprintf(buf, PAGE_SIZE, "%s\n", val_char[sda->index]);
    return strlen(buf);
}

static ssize_t store_sysstatus_value(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    struct sensor_device_attribute *sda = to_sensor_dev_attr(attr);
    char buffer[256];

    if (sda->index >= VAL_NUM_MAX)
        return -EINVAL;

    strncpy(buffer, buf, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';
    remove_trailing_newline(buffer);
    snprintf(val_char[sda->index], count + 1, "%s", buffer);

    return count;
}

struct kobject *newsysfs_kobj;
struct kobject *varsysfs_kobj;

static int create_sysfs_node(const char *node_name, int idx, umode_t mode)
{
    struct sensor_device_attribute *attr;

    int ret;

    attr = kzalloc(sizeof(struct sensor_device_attribute), GFP_KERNEL);
    if (!attr) {
        pddf_err(NEWSYSFS, "PDDF_ERROR: %s: kzalloc fail\n", __FUNCTION__);
        return -ENOMEM;
    }

    attr->dev_attr.attr.name = node_name;
    attr->dev_attr.attr.mode = mode;
    attr->dev_attr.show = show_sysstatus_value;
    attr->dev_attr.store = store_sysstatus_value;
    attr->index = idx;

    ret = sysfs_create_file(varsysfs_kobj, &attr->dev_attr.attr);
    if (ret) {
        kfree(attr);
        pddf_err(NEWSYSFS,"PDDF_ERROR: %s: create file fail\n", __FUNCTION__);
        return -ENOMEM;
    }

    return 0;
}

static int delete_sysfs_node(const char *node_name, umode_t mode)
{
    struct sensor_device_attribute *attr;

    attr = kzalloc(sizeof(struct sensor_device_attribute), GFP_KERNEL);
    if (!attr) {
        pr_err("PDDF_ERROR: %s: kzalloc fail\n", __FUNCTION__);
        return -ENOMEM;
    }

    attr->dev_attr.attr.name = node_name;
    attr->dev_attr.attr.mode = mode;
    attr->dev_attr.show = show_sysstatus_value;
    attr->dev_attr.store = store_sysstatus_value;

    sysfs_remove_file(varsysfs_kobj, &attr->dev_attr.attr);

    kfree(attr);

    return 0;
}

static int has_duplicate_name(char *str, int idx)
{
    int i =0;
    /*Compare if there are any duplicates before arry[idx]*/
    for (i = 0; i < idx; i++) {
        pddf_dbg(NEWSYSFS, "str=%s, i=%d, sysfs_data.sysfs_attrs[i].aname=%s\n", str, i, sysfs_data.sysfs_attrs[i].aname);
        if (strcmp(str, sysfs_data.sysfs_attrs[i].aname) == 0) {
            pddf_err(NEWSYSFS, "duplicate name True, str=%s, sysfs_data.sysfs_attrs[i].aname=%s\n", str, sysfs_data.sysfs_attrs[i].aname);
            return 1;
        }
    }
    return 0;
}

/*PDDF_DATA_ATTR(dev_ops, S_IWUSR, NULL, do_device_operation, PDDF_CHAR, 8, (void*)&pddf_attr, (void*)NULL);*/
static ssize_t do_device_operation(struct device *dev, struct device_attribute *da, const char *buf, size_t count)
{
    int i = 0, ret;
    PDDF_ATTR *ptr = (PDDF_ATTR *)da;
    SYSFS_DATA *pdata = (SYSFS_DATA *)(ptr->addr);
    SYSFS_PDATA *varcache_platform_data = NULL;
    umode_t mode_rw;

    /* Populate the platform data for NEWSYSFS */
    if (strncmp(buf, "add", strlen(buf)-1)==0)
    {
        /*num is all sysfs sum number, idx is clean to 0 after every add dev_ops*/
        int num = pdata->len;
        int idx = pdata->idx;
        /* Allocate the varcache_platform_data */
        varcache_platform_data = (SYSFS_PDATA *)kzalloc(sizeof(SYSFS_PDATA), GFP_KERNEL);
        varcache_platform_data->sysfs_attrs = (SYSFS_ATTR *)kzalloc(num*sizeof(SYSFS_ATTR), GFP_KERNEL);


        varcache_platform_data->idx = pdata->idx;
        varcache_platform_data->len = pdata->len;

        for (i = num - idx; i < num; i++) {

            if (i >= VAL_NUM_MAX) {
                pddf_err(NEWSYSFS, "sysfs index = %d out of max range...\n", num);
                goto clear_data;
            }

            varcache_platform_data->sysfs_attrs[i] = pdata->sysfs_attrs[i];

            if (strcmp(varcache_platform_data->sysfs_attrs[i].mode, "rw")==0)
                mode_rw = 0644;
            else
                mode_rw = 0444;

            memcpy(val_char[i], varcache_platform_data->sysfs_attrs[i].value, VAL_LEN_MAX);

            pddf_dbg(NEWSYSFS, "name=%s, value=%s, sysfs_index=%d, mode=%o\n", varcache_platform_data->sysfs_attrs[i].aname,
                        varcache_platform_data->sysfs_attrs[i].value, i, mode_rw);

            /*Check if there are duplicate name to skip create_sysfs_file, avoid call trace*/
            if (has_duplicate_name(varcache_platform_data->sysfs_attrs[i].aname, i)) {
                pddf_err(NEWSYSFS, "Found duplicate filename = %s, skip create sysfs file\n", varcache_platform_data->sysfs_attrs[i].aname);
                continue;
            }


            ret = create_sysfs_node(varcache_platform_data->sysfs_attrs[i].aname, i, mode_rw);
            if (ret) {
                pddf_err(NEWSYSFS, "create sysfs node fail\n");
                goto clear_data;
            }
        }
    }
    else if (strncmp(buf, "delete", strlen(buf)-1)==0)
    {
        int num = pdata->len;
        int idx = pdata->idx;
        /* Allocate the varcache_platform_data */
        varcache_platform_data = (SYSFS_PDATA *)kzalloc(sizeof(SYSFS_PDATA), GFP_KERNEL);
        varcache_platform_data->sysfs_attrs = (SYSFS_ATTR *)kzalloc(num*sizeof(SYSFS_ATTR), GFP_KERNEL);


        varcache_platform_data->idx = pdata->idx;
        varcache_platform_data->len = pdata->len;

        for (i = num - idx; i < num; i++) {

            if (i >= VAL_NUM_MAX) {
                pddf_err(NEWSYSFS, "sysfs index = %d out of max range...\n", num);
                goto clear_data;
            }

            varcache_platform_data->sysfs_attrs[i] = pdata->sysfs_attrs[i];

            if (strcmp(varcache_platform_data->sysfs_attrs[i].mode, "rw")==0)
                mode_rw = 0644;
            else
                mode_rw = 0444;

            pddf_dbg(NEWSYSFS, "del name=%s, value=%s, mode=%o\n", varcache_platform_data->sysfs_attrs[i].aname,
                        varcache_platform_data->sysfs_attrs[i].value,  mode_rw);

            ret = delete_sysfs_node(varcache_platform_data->sysfs_attrs[i].aname, mode_rw);
            if (ret) {
                pddf_err(NEWSYSFS, "delete sysfs node fail\n");
                kfree(varcache_platform_data);
                return ret;
            }
        }
        pdata->len = pdata->len - pdata->idx - pdata->idx;
    }
    else
    {
        pddf_err(NEWSYSFS, "PDDF_ERROR: %s: Invalid value for dev_ops %s", __FUNCTION__, buf);
    }

    goto clear_data;

clear_data:
    //memset(pdata, 0, sizeof(SYSFS_DATA));
    pdata->idx = 0;
    if (varcache_platform_data != NULL)
        kfree(varcache_platform_data);
    return count;
}


int __init pddf_data_init(void)
{
    struct kobject *device_kobj;
    int ret = 0;

    pddf_info(NEWSYSFS, "CACHE PDDF MODULE.. init\n");

    device_kobj = get_device_i2c_kobj();
    if(!device_kobj)
        return -ENOMEM;

    newsysfs_kobj = kobject_create_and_add("newsysfs", device_kobj);
    if(!newsysfs_kobj)
        return -ENOMEM;

    pddf_info(NEWSYSFS, "CREATED NEWSYSFS GROUP\n");

    varsysfs_kobj = kobject_create_and_add("sysfs", newsysfs_kobj);
    if(!varsysfs_kobj)
        return -ENOMEM;

    pddf_info(NEWSYSFS, "CREATED SYSFS GROUP\n");

    ret = sysfs_create_group(newsysfs_kobj, &pddf_newsysfs_client_data_group);
    if (ret)
    {
        sysfs_remove_group(newsysfs_kobj, &pddf_newsysfs_client_data_group);
        kobject_put(newsysfs_kobj);
        return ret;
    }
    pddf_info(NEWSYSFS, "CREATED PDDF DATA SYSFS GROUP\n");

    return ret;
}

void __exit pddf_data_exit(void)
{

    pddf_info(NEWSYSFS, "VARCACHE PDDF MODULE.. exit\n");
    sysfs_remove_group(newsysfs_kobj, &pddf_newsysfs_client_data_group);
    kobject_put(varsysfs_kobj);
    kobject_put(newsysfs_kobj);

    pddf_info(NEWSYSFS, KERN_ERR "%s: Removed the kobjects for 'varcache' and 'varsysfs'\n",__FUNCTION__);

    return;
}

module_init(pddf_data_init);
module_exit(pddf_data_exit);

MODULE_AUTHOR("IEI");
MODULE_DESCRIPTION("node for s3ip sysfs platform data");
MODULE_LICENSE("GPL");
