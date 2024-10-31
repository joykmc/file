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
 * Description of various APIs related to FAN component
 */

#include <linux/module.h>
#include <linux/jiffies.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/sysfs.h>
#include <linux/slab.h>
#include <linux/dmi.h>
#include "pddf_fan_defs.h"
#include "pddf_fan_driver.h"
#include "pddf_client_defs.h"

/*#define FAN_DEBUG*/
#ifdef FAN_DEBUG
#define fan_dbg(...) printk(__VA_ARGS__)
#else
#define fan_dbg(...)
#endif

#define MAX_PWM 255

extern void *get_device_table(char *name);

int fan_debounce[] = {1,1,1,1,1,1}

void get_fan_duplicate_sysfs(int idx, char *str)
{
	switch (idx)
	{
		default:
			break;
	}

	return;
}


int fan_update_hw(struct device *dev, struct fan_attr_info *info, FAN_DATA_ATTR *udata)
{
	int status = 0;
    struct i2c_client *client = to_i2c_client(dev);
	FAN_SYSFS_ATTR_DATA *sysfs_attr_data = NULL;


    mutex_lock(&info->update_lock);

	sysfs_attr_data = udata->access_data;
	if (sysfs_attr_data->pre_set != NULL)
	{
		status = (sysfs_attr_data->pre_set)(client, udata, info);
		if (status!=0)
			dev_warn(&client->dev, "%s: pre_set function fails for %s attribute. ret %d\n", __FUNCTION__, udata->aname, status);
	}
	if (sysfs_attr_data->do_set != NULL)
	{
		status = (sysfs_attr_data->do_set)(client, udata, info);
		if (status!=0)
			dev_warn(&client->dev, "%s: do_set function fails for %s attribute. ret %d\n", __FUNCTION__, udata->aname, status);

	}
	if (sysfs_attr_data->post_set != NULL)
	{
		status = (sysfs_attr_data->post_set)(client, udata, info);
		if (status!=0)
			dev_warn(&client->dev, "%s: post_set function fails for %s attribute. ret %d\n", __FUNCTION__, udata->aname, status);
	}

    mutex_unlock(&info->update_lock);

    return 0;
}

int fan_update_attr(struct device *dev, struct fan_attr_info *info, FAN_DATA_ATTR *udata)
{
	int status = 0;
    struct i2c_client *client = to_i2c_client(dev);
	FAN_SYSFS_ATTR_DATA *sysfs_attr_data = NULL;


    mutex_lock(&info->update_lock);

    if (time_after(jiffies, info->last_updated + HZ + HZ / 2) || !info->valid)
	{
        dev_dbg(&client->dev, "Starting pddf_fan update\n");
        info->valid = 0;

		sysfs_attr_data = udata->access_data;
		if (sysfs_attr_data->pre_get != NULL)
		{
			status = (sysfs_attr_data->pre_get)(client, udata, info);
			if (status!=0)
				dev_warn(&client->dev, "%s: pre_get function fails for %s attribute. ret %d\n", __FUNCTION__, udata->aname, status);
		}
		if (sysfs_attr_data->do_get != NULL)
		{
			status = (sysfs_attr_data->do_get)(client, udata, info);
			if (status!=0)
				dev_warn(&client->dev, "%s: do_get function fails for %s attribute. ret %d\n", __FUNCTION__, udata->aname, status);

		}
		if (sysfs_attr_data->post_get != NULL)
		{
			status = (sysfs_attr_data->post_get)(client, udata, info);
			if (status!=0)
				dev_warn(&client->dev, "%s: post_get function fails for %s attribute.ret %d\n", __FUNCTION__, udata->aname, status);
		}


        info->last_updated = jiffies;
        info->valid = 1;
    }

    mutex_unlock(&info->update_lock);

    return 0;
}

ssize_t fan_show_speed_target(struct device *dev, struct device_attribute *da, char *buf)
{
    struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
    struct i2c_client *client = to_i2c_client(dev);
    struct fan_data *data = i2c_get_clientdata(client);
    FAN_PDATA *pdata = (FAN_PDATA *)(client->dev.platform_data);
    FAN_DATA_ATTR *pwm_usr_data = NULL, *status_usr_data = NULL;
    struct fan_attr_info *pwm_attr_info = NULL, \
            *status_attr_info = NULL;
    int i, status=0, fan_target, motor_number, ret;
    unsigned int tolerance, speed_1_min, speed_1_max, speed_2_max, speed_2_min;
    char *fan_id;
    char fan_attr[ATTR_NAME_LEN]="";
    char *attr_name, *end;
    int fan_attr_len = 0, pwm = 0;
    char *token, *curr_ptr;
    char curr[STATUS_INFO_LEN];

    for (i=0;i<data->num_attr;i++) {
        if (strcmp(attr->dev_attr.attr.name, pdata->fan_attrs[i].aname) == 0) {
			status_attr_info = &data->attr_info[i];
            status_usr_data = &pdata->fan_attrs[i];
        }
    }

    if (status_attr_info==NULL || status_usr_data==NULL) {
        pddf_err(FAN, "status_info  is not a supported attribute for this client\n");
		goto exit;
	}

    /* Find out the fan_id */
    fan_attr_len = strlen(attr->dev_attr.attr.name);
    attr_name = end = (char *)kzalloc(fan_attr_len+1, GFP_KERNEL);
    memcpy(attr_name, attr->dev_attr.attr.name, fan_attr_len+1);
    fan_id = strsep(&end, "_");
    /* fan_id is fan1/fan2/.../fan12 */
#ifdef __STDC_LIB_EXT1__
	memset_s(fan_attr, sizeof(fan_attr), 0 , ATTR_NAME_LEN);
#else
    memset(fan_attr, 0, ATTR_NAME_LEN);
#endif
    snprintf(fan_attr, ATTR_NAME_LEN, "%s_1_target", fan_id);

    /* Make sure fan motor number */
    if (strcmp(attr->dev_attr.attr.name, fan_attr) == 0) {
        motor_number = 1;
    } else {
        motor_number = 2;
    }

#ifdef __STDC_LIB_EXT1__
	memset_s(fan_attr, sizeof(fan_attr), 0 , ATTR_NAME_LEN);
#else
    memset(fan_attr, 0, ATTR_NAME_LEN);
#endif
    snprintf(fan_attr, ATTR_NAME_LEN, "%s_pwm", fan_id);
    for (i=0;i<data->num_attr;i++)
    {
        if (strcmp(fan_attr, pdata->fan_attrs[i].aname) == 0)
        {
            pwm_attr_info = &data->attr_info[i];
            pwm_usr_data = &pdata->fan_attrs[i];
            break;
        }
    }

    if (pwm_attr_info==NULL || pwm_usr_data==NULL)
    {
        pddf_err(FAN, "%s_pwm  is not a supported attribute for this client\n", fan_id);
        kfree(attr_name);
		goto exit;
	}

    kfree(attr_name);

    fan_update_attr(dev, pwm_attr_info, pwm_usr_data);

    pwm = pwm_attr_info->val.intval;

    /* Determine the fan speed threshold info */
    strncpy(curr, status_usr_data->status_info, sizeof(curr) - 1);
    curr[sizeof(curr) - 1] = '\0';
    curr_ptr = curr;

    /* Example: TOLERANCE:30, SPEED_1_MAX:12000, SPEED_1_MIN:1000*/
    while ((token = strsep(&curr_ptr, ",")) != NULL) {
        /* Skip leading spaces in each token */
        while (*token == ' ') {
            token++;
        }

        if (strncmp(token, "TOLERANCE:", 10) == 0) {
            char *value = token + 10;
            while (*value == ' ') {
                value++;
            }
            ret = kstrtouint(value, 0, &tolerance);
            if (ret) {
                pddf_err(FAN, "kstrtouint convert tolerance error, ret =%d\n", ret);
                return ret;
            }
        } else if (strncmp(token, "SPEED_1_MAX:", 12) == 0) {
            char *value = token + 12;
            while (*value == ' ') {
                value++;
            }
            ret = kstrtouint(value, 0, &speed_1_max);
            if (ret) {
                pddf_err(FAN, "kstrtouint convert speed_1_max error, ret =%d\n", ret);
                return ret;
            }
        } else if (strncmp(token, "SPEED_1_MIN:", 12) == 0) {
            char *value = token + 12;
            while (*value == ' ') {
                value++;
            }
            ret = kstrtouint(value, 0, &speed_1_min);
            if (ret) {
                pddf_err(FAN, "kstrtouint convert speed_1_min error, ret =%d\n", ret);
                return ret;
            }
        } else if (strncmp(token, "SPEED_2_MAX:", 12) == 0) {
            char *value = token + 12;
            while (*value == ' ') {
                value++;
            }
            ret = kstrtouint(value, 0, &speed_2_max);
            if (ret) {
                pddf_err(FAN, "kstrtouint convert speed_2_max error, ret =%d\n", ret);
                return ret;
            }
        } else if (strncmp(token, "SPEED_2_MIN:", 12) == 0) {
            char *value = token + 12;
            while (*value == ' ') {
                value++;
            }
            ret = kstrtouint(value, 0, &speed_2_min);
            if (ret) {
                pddf_err(FAN, "kstrtouint convert speed_2_min error, ret =%d\n", ret);
                return ret;
            }
        }
    }

    pddf_dbg(FAN, "%s: tolerance=%d, speed_1_max=%d, speed_1_min=%d, speed_2_max=%d, speed_2_min=%d\n", __FUNCTION__,
            tolerance, speed_1_max, speed_1_min, speed_2_max, speed_2_min);

    if (motor_number == 1) {
        fan_target = (speed_1_max - speed_1_min) * pwm / 100 + speed_1_min;
    } else {
        fan_target = (speed_2_max - speed_2_min) * pwm / 100 + speed_2_min;
    }

    return snprintf(buf, PAGE_SIZE, "%d\n", fan_target);

exit:
    return sprintf(buf, "%d\n", status);
}

ssize_t fan_show_debounce(struct device *dev, struct device_attribute *da, char *buf)
{
    struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
    struct i2c_client *client = to_i2c_client(dev);
    struct fan_data *data = i2c_get_clientdata(client);
    int i, fan_idx; // Start from 0
    char string[30];

    for(i=0;i<12;i++) {
        sprintf(string, "fan%d_debounce", i + 1);
        pddf_err(FAN, "sprintf debounce string=%s\n", string);
        if (strncmp(string, attr->dev_attr.attr.name, 6) == 0) {
            fan_idx = i;
            pddf_err(FAN, "debounce get fan index = %d\n", fan_idx);
            break;
        }
        if (i >= 12) {
            pddf_err(FAN, "fan debouce not found match fan_idx, check sysfs attr name\n");
        }
    }

    return fan_debounce[fan_idx];
}

static int extract_fan_index(char *fan_id)
{
    int fan_index = 0;
    char string[30];

    for(i=0; i<12; i++) {
        sprintf(string, "fan%d", i + 1);
        pddf_err(FAN, "sprintf debounce string=%s\n", string);
        if (strncmp(string, fan_id, strlen(string)) == 0) {
            pddf_err(FAN, "debounce get fan index = %d\n", i);
            return i;
        }
    }
    pddf_err(FAN, "extract fan_index not found match fan_idx\n");
    return -ENIVAL;
}

ssize_t fan_show_status(struct device *dev, struct device_attribute *da, char *buf)
{
    struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
    struct i2c_client *client = to_i2c_client(dev);
    struct fan_data *data = i2c_get_clientdata(client);
    FAN_PDATA *pdata = (FAN_PDATA *)(client->dev.platform_data);
    FAN_DATA_ATTR *pres_usr_data = NULL, *speed_1_usr_data = NULL, \
                 *speed_2_usr_data = NULL, *pwm_usr_data = NULL, \
                 *status_usr_data = NULL;
    struct fan_attr_info *pres_attr_info = NULL, *speed_1_attr_info = NULL, \
            *speed_2_attr_info = NULL, *pwm_attr_info = NULL, \
            *status_attr_info = NULL;
    int i, status=0, fanx_1_input, fanx_2_input,
        fan_target, fan_tolerance, fan_base, ret;
    unsigned int tolerance, speed_1_min, speed_1_max, speed_2_max, speed_2_min;
    char *fan_id;
    char fan_attr[ATTR_NAME_LEN]="";
    char *attr_name, *end;
    int fan_attr_len = 0, presence = 0, pwm = 0, fan_index = 0;
    char *token, *curr_ptr;
    char curr[STATUS_INFO_LEN];

    for (i=0;i<data->num_attr;i++) {
        if (strcmp(attr->dev_attr.attr.name, pdata->fan_attrs[i].aname) == 0) {
			status_attr_info = &data->attr_info[i];
            status_usr_data = &pdata->fan_attrs[i];
        }
    }

    if (status_attr_info==NULL || status_usr_data==NULL) {
        pddf_err(FAN, "status_info  is not a supported attribute for this client\n");
		goto exit;
	}

    /* Find out the fan_id */
    fan_attr_len = strlen(attr->dev_attr.attr.name);
    attr_name = end = (char *)kzalloc(fan_attr_len+1, GFP_KERNEL);
    memcpy(attr_name, attr->dev_attr.attr.name, fan_attr_len+1);
    fan_id = strsep(&end, "_");
    /* fan_id is fan1/fan2/.../fan12 */
#ifdef __STDC_LIB_EXT1__
	memset_s(fan_attr, sizeof(fan_attr), 0 , ATTR_NAME_LEN);
#else
    memset(fan_attr, 0, ATTR_NAME_LEN);
#endif
    snprintf(fan_attr, ATTR_NAME_LEN, "%s_present", fan_id);
    for (i=0;i<data->num_attr;i++)
    {
        if (strcmp(fan_attr, pdata->fan_attrs[i].aname) == 0)
        {
			pres_attr_info = &data->attr_info[i];
            pres_usr_data = &pdata->fan_attrs[i];
            break;
        }
    }
#ifdef __STDC_LIB_EXT1__
	memset_s(fan_attr, sizeof(fan_attr), 0 , ATTR_NAME_LEN);
#else
    memset(fan_attr, 0, ATTR_NAME_LEN);
#endif
    snprintf(fan_attr, ATTR_NAME_LEN, "%s_1_input", fan_id);
    for (i=0;i<data->num_attr;i++)
    {
        if (strcmp(fan_attr, pdata->fan_attrs[i].aname) == 0)
        {
            speed_1_attr_info = &data->attr_info[i];
            speed_1_usr_data = &pdata->fan_attrs[i];
            break;
        }
    }
#ifdef __STDC_LIB_EXT1__
	memset_s(fan_attr, sizeof(fan_attr), 0 , ATTR_NAME_LEN);
#else
    memset(fan_attr, 0, ATTR_NAME_LEN);
#endif
    snprintf(fan_attr, ATTR_NAME_LEN, "%s_2_input", fan_id);
    for (i=0;i<data->num_attr;i++)
    {
        if (strcmp(fan_attr, pdata->fan_attrs[i].aname) == 0)
        {
            speed_2_attr_info = &data->attr_info[i];
            speed_2_usr_data = &pdata->fan_attrs[i];
            break;
        }
    }

#ifdef __STDC_LIB_EXT1__
	memset_s(fan_attr, sizeof(fan_attr), 0 , ATTR_NAME_LEN);
#else
    memset(fan_attr, 0, ATTR_NAME_LEN);
#endif
    snprintf(fan_attr, ATTR_NAME_LEN, "%s_pwm", fan_id);
    for (i=0;i<data->num_attr;i++)
    {
        if (strcmp(fan_attr, pdata->fan_attrs[i].aname) == 0)
        {
            pwm_attr_info = &data->attr_info[i];
            pwm_usr_data = &pdata->fan_attrs[i];
            break;
        }
    }

    if (pres_attr_info==NULL || pres_usr_data==NULL || speed_1_attr_info ==NULL || speed_1_usr_data==NULL ||
        pwm_attr_info==NULL || pwm_usr_data==NULL)
    {
        pddf_err(FAN, "%s_present or %s_input is not a supported attribute for this client\n", fan_id, fan_id);
        kfree(attr_name);
		goto exit;
	}
    kfree(attr_name);

    fan_update_attr(dev, pres_attr_info, pres_usr_data);
    fan_update_attr(dev, speed_1_attr_info, speed_1_usr_data);
    fan_update_attr(dev, pwm_attr_info, pwm_usr_data);

	/*Decide the o/p based on attribute type */
    presence = pres_attr_info->val.intval;
    pwm = pwm_attr_info->val.intval;
    fanx_1_input = speed_1_attr_info->val.intval;

    if (presence == 0)
        return sprintf(buf, "0\n");

    /* Determine whether th fan is present */
    strncpy(curr, status_usr_data->status_info, sizeof(curr) - 1);
    curr[sizeof(curr) - 1] = '\0';
    curr_ptr = curr;

    /* Example: TOLERANCE:30, SPEED_1_MAX:12000, SPEED_1_MIN:1000*/
    while ((token = strsep(&curr_ptr, ",")) != NULL) {
        /* Skip leading spaces in each token */
        while (*token == ' ') {
            token++;
        }

        if (strncmp(token, "TOLERANCE:", 10) == 0) {
            char *value = token + 10;
            while (*value == ' ') {
                value++;
            }
            ret = kstrtouint(value, 0, &tolerance);
            if (ret) {
                pddf_err(FAN, "kstrtouint convert tolerance error, ret =%d\n", ret);
                return ret;
            }
        } else if (strncmp(token, "SPEED_1_MAX:", 12) == 0) {
            char *value = token + 12;
            while (*value == ' ') {
                value++;
            }
            ret = kstrtouint(value, 0, &speed_1_max);
            if (ret) {
                pddf_err(FAN, "kstrtouint convert speed_1_max error, ret =%d\n", ret);
                return ret;
            }
        } else if (strncmp(token, "SPEED_1_MIN:", 12) == 0) {
            char *value = token + 12;
            while (*value == ' ') {
                value++;
            }
            ret = kstrtouint(value, 0, &speed_1_min);
            if (ret) {
                pddf_err(FAN, "kstrtouint convert speed_1_min error, ret =%d\n", ret);
                return ret;
            }
        } else if (strncmp(token, "SPEED_2_MAX:", 12) == 0) {
            char *value = token + 12;
            while (*value == ' ') {
                value++;
            }
            ret = kstrtouint(value, 0, &speed_2_max);
            if (ret) {
                pddf_err(FAN, "kstrtouint convert speed_2_max error, ret =%d\n", ret);
                return ret;
            }
        } else if (strncmp(token, "SPEED_2_MIN:", 12) == 0) {
            char *value = token + 12;
            while (*value == ' ') {
                value++;
            }
            ret = kstrtouint(value, 0, &speed_2_min);
            if (ret) {
                pddf_err(FAN, "kstrtouint convert speed_2_min error, ret =%d\n", ret);
                return ret;
            }
        }
    }

    pddf_dbg(FAN, "%s: tolerance=%d, speed_1_max=%d, speed_1_min=%d, speed_2_max=%d, speed_2_min=%d\n", __FUNCTION__,
            tolerance, speed_1_max, speed_1_min, speed_2_max, speed_2_min);


    fan_index = extract_fan_index(fan_id);
    pddf_err(FAN, "fan_show status fan_idnex = %d, fan_debounce=%d\n", fan_index, fan_debounce[fan_index]);

    fan_target = (speed_1_max - speed_1_min) * pwm / 100 + speed_1_min;

    fan_tolerance = fan_target * tolerance / 100;

    fan_base = fanx_1_input - fan_target;

    pddf_dbg(FAN, "present =%d, pwm = %d, fanx_1_input=%d, fan_target=%d, fan_tolerance=%d\n",
                presence, pwm, fanx_1_input, fan_target, fan_tolerance);

    /*fan_debouce: fan control thermal policy daemmon filter the status of fan[idx], avoid shocks*/
    if (((fan_base > fan_tolerance || fan_base < -fan_tolerance)) && (fan_debounce[fan_index] == 2))
        return sprintf(buf, "2\n");

    if (speed_2_attr_info != NULL && speed_2_usr_data != NULL) {
        fan_update_attr(dev, speed_2_attr_info, speed_2_usr_data);
        fanx_2_input = speed_2_attr_info->val.intval;
        fan_target = (speed_2_max - speed_2_min) * pwm / 100 + speed_2_min;
        fan_tolerance = fan_target * tolerance / 100;
        fan_base = fanx_2_input - fan_target;
        pddf_dbg(FAN, "fanx_2_input=%d, fan_target=%d, fan_tolerance=%d, fan_base=%d\n",
                fanx_2_input, fan_target, fan_tolerance, fan_base);
        if (((fan_base > fan_tolerance || fan_base < -fan_tolerance)) && (fan_debounce[fan_index] == 2))
            return sprintf(buf, "2\n");
    }


    return sprintf(buf, "1\n");
exit:
    return sprintf(buf, "%d\n", status);
}

ssize_t fan_show_default(struct device *dev, struct device_attribute *da, char *buf)
{
    struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
    struct i2c_client *client = to_i2c_client(dev);
    struct fan_data *data = i2c_get_clientdata(client);
    FAN_PDATA *pdata = (FAN_PDATA *)(client->dev.platform_data);
    FAN_DATA_ATTR *usr_data = NULL;
    struct fan_attr_info *attr_info = NULL;
    int i, status=0;
	char new_str[ATTR_NAME_LEN] = "";
	FAN_SYSFS_ATTR_DATA *ptr = NULL;

    for (i=0;i<data->num_attr;i++) {
		ptr = (FAN_SYSFS_ATTR_DATA *)pdata->fan_attrs[i].access_data;
		get_fan_duplicate_sysfs(ptr->index , new_str);
        if (strcmp(attr->dev_attr.attr.name, pdata->fan_attrs[i].aname) == 0 || \
				 strcmp(attr->dev_attr.attr.name, new_str) == 0) {
			attr_info = &data->attr_info[i];
            usr_data = &pdata->fan_attrs[i];
			strcpy(new_str, "");
        }
    }

    if (attr_info==NULL || usr_data==NULL) {
        dev_err(&client->dev, "%s is not supported attribute for this client\n", usr_data->aname);
		goto exit;
	}

    fan_update_attr(dev, attr_info, usr_data);

	/*Decide the o/p based on attribute type */
	switch(attr->index)
	{
		case FAN1_PRESENT:
		case FAN2_PRESENT:
		case FAN3_PRESENT:
		case FAN4_PRESENT:
		case FAN5_PRESENT:
		case FAN6_PRESENT:
		case FAN7_PRESENT:
		case FAN8_PRESENT:
		case FAN9_PRESENT:
		case FAN10_PRESENT:
		case FAN11_PRESENT:
		case FAN12_PRESENT:
		case FAN1_DIRECTION:
		case FAN2_DIRECTION:
		case FAN3_DIRECTION:
		case FAN4_DIRECTION:
		case FAN5_DIRECTION:
		case FAN6_DIRECTION:
		case FAN7_DIRECTION:
		case FAN8_DIRECTION:
		case FAN9_DIRECTION:
		case FAN10_DIRECTION:
		case FAN11_DIRECTION:
		case FAN12_DIRECTION:
		case FAN1_1_INPUT:
		case FAN1_2_INPUT:
		case FAN2_1_INPUT:
		case FAN2_2_INPUT:
		case FAN3_1_INPUT:
		case FAN3_2_INPUT:
		case FAN4_1_INPUT:
		case FAN4_2_INPUT:
		case FAN5_1_INPUT:
		case FAN5_2_INPUT:
		case FAN6_1_INPUT:
		case FAN6_2_INPUT:
		case FAN7_1_INPUT:
		case FAN7_2_INPUT:
		case FAN8_1_INPUT:
		case FAN8_2_INPUT:
		case FAN9_1_INPUT:
		case FAN9_2_INPUT:
		case FAN10_1_INPUT:
		case FAN10_2_INPUT:
		case FAN11_1_INPUT:
		case FAN11_2_INPUT:
		case FAN12_1_INPUT:
		case FAN12_2_INPUT:
		case FAN1_PWM:
		case FAN2_PWM:
		case FAN3_PWM:
		case FAN4_PWM:
		case FAN5_PWM:
		case FAN6_PWM:
		case FAN7_PWM:
		case FAN8_PWM:
		case FAN9_PWM:
		case FAN10_PWM:
		case FAN11_PWM:
		case FAN12_PWM:
		case FAN1_FAULT:
		case FAN2_FAULT:
		case FAN3_FAULT:
		case FAN4_FAULT:
		case FAN5_FAULT:
		case FAN6_FAULT:
		case FAN7_FAULT:
		case FAN8_FAULT:
		case FAN9_FAULT:
		case FAN10_FAULT:
		case FAN11_FAULT:
		case FAN12_FAULT:
		case FAN1_FRU_WP:
		case FAN2_FRU_WP:
		case FAN3_FRU_WP:
		case FAN4_FRU_WP:
		case FAN5_FRU_WP:
		case FAN6_FRU_WP:
		case FAN7_FRU_WP:
		case FAN8_FRU_WP:
		case FAN9_FRU_WP:
		case FAN10_FRU_WP:
		case FAN11_FRU_WP:
		case FAN12_FRU_WP:
		case FAN1_PLUG_RECORD:
		case FAN2_PLUG_RECORD:
		case FAN3_PLUG_RECORD:
		case FAN4_PLUG_RECORD:
		case FAN5_PLUG_RECORD:
		case FAN6_PLUG_RECORD:
		case FAN7_PLUG_RECORD:
		case FAN8_PLUG_RECORD:
			status = attr_info->val.intval;
			break;
		default:
			dev_err(&client->dev, "%s: Unable to find the attribute index for %s\n",
					 __FUNCTION__, usr_data->aname);
			status = 0;
	}

exit:
    return sprintf(buf, "%d\n", status);
}


ssize_t fan_store_default(struct device *dev, struct device_attribute *da, const char *buf, size_t count)
{
    struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
    struct i2c_client *client = to_i2c_client(dev);
    struct fan_data *data = i2c_get_clientdata(client);
    FAN_PDATA *pdata = (FAN_PDATA *)(client->dev.platform_data);
    FAN_DATA_ATTR *usr_data = NULL;
    struct fan_attr_info *attr_info = NULL;
    int i, ret ;
	uint32_t val;

    for (i=0;i<data->num_attr;i++) {
		if (strcmp(data->attr_info[i].name, attr->dev_attr.attr.name) == 0 && \
				 strcmp(pdata->fan_attrs[i].aname, attr->dev_attr.attr.name) == 0) {
            attr_info = &data->attr_info[i];
            usr_data = &pdata->fan_attrs[i];
        }
    }

    if (attr_info==NULL || usr_data==NULL) {
		dev_err(&client->dev, "%s is not supported attribute for this client\n",
				 attr->dev_attr.attr.name);
		goto exit;
	}

	switch(attr->index)
	{
		case FAN1_PWM:
		case FAN2_PWM:
		case FAN3_PWM:
		case FAN4_PWM:
		case FAN5_PWM:
		case FAN6_PWM:
		case FAN7_PWM:
		case FAN8_PWM:
		case FAN9_PWM:
		case FAN10_PWM:
		case FAN11_PWM:
		case FAN12_PWM:
		case FAN1_FRU_WP:
		case FAN2_FRU_WP:
		case FAN3_FRU_WP:
		case FAN4_FRU_WP:
		case FAN5_FRU_WP:
		case FAN6_FRU_WP:
		case FAN7_FRU_WP:
		case FAN8_FRU_WP:
		case FAN9_FRU_WP:
		case FAN10_FRU_WP:
		case FAN11_FRU_WP:
		case FAN12_FRU_WP:
		case FAN1_PLUG_RECORD:
		case FAN2_PLUG_RECORD:
		case FAN3_PLUG_RECORD:
		case FAN4_PLUG_RECORD:
		case FAN5_PLUG_RECORD:
		case FAN6_PLUG_RECORD:
		case FAN7_PLUG_RECORD:
		case FAN8_PLUG_RECORD:
			ret = kstrtoint(buf, 0, &val);
			if (ret) {
				dev_err(&client->dev, "%s: Unable to convert string into value for %s\n",
						 __FUNCTION__, usr_data->aname);
				return ret;
			}
			/*Update the value of attr_info here, and use it to update the HW values*/
			attr_info->val.intval = val;
			break;
		default:
			dev_err(&client->dev, "%s: Unable to find the attr index for %s\n",
					 __FUNCTION__, usr_data->aname);
			goto exit;
	}

	dev_dbg(&client->dev, "%s: pwm to be set is %d\n", __FUNCTION__, val);
	fan_update_hw(dev, attr_info, usr_data);

exit:
	return count;
}

int fan_cpld_client_read(FAN_DATA_ATTR *udata)
{
    int status = -1;
    int status_hi, status_lo;

    if (udata!=NULL)
    {
        if (udata->len==1)
        {
            status = board_i2c_cpld_read_new(udata->devaddr, udata->devname, udata->offset);
            pddf_dbg(FAN, "udata->devaddr=0x%x, udata->devname=%s, udata->offset=0x%x, status=0x%x\n", \
                    udata->devaddr, udata->devname, udata->offset, status);
        }
        else
        {
            /* Get the I2C client for the CPLD */
            struct i2c_client *client_ptr=NULL;
            client_ptr = (struct i2c_client *)get_device_table(udata->devname);
            if (client_ptr)
            {
                if (udata->len==2)
                {
                    status_lo = i2c_smbus_read_byte_data(client_ptr, udata->offset);
                    if (status_lo < 0) {
                        return status_lo;
                    }

                    status_hi = i2c_smbus_read_byte_data(client_ptr, udata->offset + 1);
                    if (status_hi < 0) {
                        return status_hi;
                    }

                    status = (status_hi << 8) | status_lo;
                }
                else
                    pddf_err(FAN, "PDDF_FAN: Doesn't support block CPLD read yet");
            }
            else
                pddf_err(FAN,"Unable to get the client handle for %s\n", udata->devname);
        }

    }

    return status;
}
#if (FAN_DEBUG == 0x1)
extern int fan_dbg_arr[];
#endif
int sonic_i2c_get_fan_present_default(void *client, FAN_DATA_ATTR *udata, void *info)
{
    int status = 0;
    int val = 0;
    u8 order;
    char idx[3] = {0};
    char *pidx = idx;
    struct fan_attr_info *painfo = (struct fan_attr_info *)info;

    pr_debug("%s:%d udata->devtype=%s, udata->aname=%s, addr=0x%x\n", __func__, __LINE__,
             udata->devtype, udata->aname, udata->offset);

    if (strcmp(udata->devtype, "cpld") == 0) {
        val = fan_cpld_client_read(udata);
    } else {
        val = i2c_smbus_read_byte_data((struct i2c_client *)client, udata->offset);
    }

    strncpy(idx, udata->aname+3, 1);
    idx[1] = '\0';

    if (kstrtou8(pidx, 0, &order) < 0) {
        pr_err("%s:%d idx=%s\n", __func__, __LINE__, pidx);
        return -EINVAL;
    }

	if (val < 0)
		status = val;
	else {
#if (FAN_DEBUG == 0x1)
		painfo->val.intval = ((val & udata->mask) == udata->cmpval) & *(fan_dbg_arr+order-1);
#else
        painfo->val.intval = ((val & udata->mask) == udata->cmpval);
#endif
    }

#if (FAN_DEBUG == 0x1)
    pr_debug("%s:%d val=%d, idx=%s, *(fan_dbg_arr+idx)=%d, order=%d, "
             "((val & udata->mask) == udata->cmpval)=%d\n",
             __func__, __LINE__, val, idx, *(fan_dbg_arr+order-1), order-1,
             ((val & udata->mask) == udata->cmpval));
#endif

    return status;
}

int sonic_i2c_get_fan_rpm_default(void *client, FAN_DATA_ATTR *udata, void *info)
{
    int status = 0;
	int val = 0;
    struct fan_attr_info *painfo = (struct fan_attr_info *)info;

    if (strcmp(udata->devtype, "cpld") == 0)
    {
        val = fan_cpld_client_read(udata);
    }
    else
    {
        if (udata->len == 1)
        {
            val = i2c_smbus_read_byte_data((struct i2c_client *)client, udata->offset);
        }
        else if (udata->len ==2)
        {
            val = i2c_smbus_read_word_swapped((struct i2c_client *)client, udata->offset);

        }
    }

	if (val < 0)
		status = val;
	else
	{
		if ((udata->is_divisor) && (val != 0))
			painfo->val.intval = udata->mult / val;
		else
			painfo->val.intval = udata->mult * val;
	}

	return status;
}


int sonic_i2c_get_fan_direction_default(void *client, FAN_DATA_ATTR *udata, void *info)
{
    int status = 0;
	uint32_t val = 0;
    struct fan_attr_info *painfo = (struct fan_attr_info *)info;

    if (strcmp(udata->devtype, "cpld") == 0)
    {
        val = fan_cpld_client_read(udata);
    }
    else
    {
	    val = i2c_smbus_read_byte_data((struct i2c_client *)client, udata->offset);
    }
	painfo->val.intval = ((val & udata->mask) == udata->cmpval);

    return status;
}


int sonic_i2c_set_fan_pwm_default(struct i2c_client *client, FAN_DATA_ATTR *udata, void *info)
{
    int status = 0;
	uint32_t val = 0;
    struct fan_attr_info *painfo = (struct fan_attr_info *)info;

	val = painfo->val.intval & udata->mask;

	if (val > 100)
	{
	  return -EINVAL;
	}

	val = val * MAX_PWM / 100;

    if (strcmp(udata->devtype, "cpld") == 0)
    {
        if (udata->len==1)
        {
            status = board_i2c_cpld_write_new(udata->devaddr, udata->devname, udata->offset, (uint8_t)val);
            pddf_dbg(FAN, "udata->devaddr=0x%x, udata->devname=%s, udata->offset=0x%x, (uint8_t)val=%d\n", \
                    udata->devaddr, udata->devname, udata->offset, (uint8_t)val);
        }
        else
        {
            /* Get the I2C client for the CPLD */
            struct i2c_client *client_ptr=NULL;
            client_ptr = (struct i2c_client *)get_device_table(udata->devname);
            if (client_ptr)
            {
                if (udata->len==2)
                {
                    uint8_t val_lsb = val & 0xFF;
                    uint8_t val_hsb = (val >> 8) & 0xFF;
                    /* TODO: Check this logic for LE and BE */
                    i2c_smbus_write_byte_data(client, udata->offset, val_lsb);
                    i2c_smbus_write_byte_data(client, udata->offset+1, val_hsb);
                }
                else
                    pddf_err(FAN, "PDDF_FAN: Doesn't support block CPLD write yet");
            }
            else
                pddf_err(FAN, "Unable to get the client handle for %s\n", udata->devname);
        }

    }
    else
    {
        if (udata->len == 1)
            i2c_smbus_write_byte_data(client, udata->offset, val);
        else if (udata->len == 2)
        {
            uint8_t val_lsb = val & 0xFF;
            uint8_t val_hsb = (val >> 8) & 0xFF;
            /* TODO: Check this logic for LE and BE */
            i2c_smbus_write_byte_data(client, udata->offset, val_lsb);
            i2c_smbus_write_byte_data(client, udata->offset+1, val_hsb);
        }
        else
        {
            pddf_err(FAN, "%s: pwm should be of len 1/2 bytes. Not setting the pwm as the length is %d\n", __FUNCTION__, udata->len);
        }
    }

	return status;
}

int sonic_i2c_set_fan_fru_wp_default(struct i2c_client *client, FAN_DATA_ATTR *udata, void *info)
{
    int status = 0;
	uint32_t val = 0;
	struct fan_attr_info *painfo = (struct fan_attr_info *)info;

    /* Convert bit 0 and 1 by udata->cmpval */
	if (udata->cmpval)
		val = painfo->val.intval & udata->mask;
	else
		val = !(painfo->val.intval & udata->mask);

    if (strcmp(udata->devtype, "cpld") == 0)
    {
        if (udata->len==1)
        {
            status = board_i2c_cpld_write_new(udata->devaddr, udata->devname, udata->offset, (uint8_t)val);
            pddf_dbg(FAN, "udata->devaddr=0x%x, udata->devname=%s, udata->offset=0x%x, (uint8_t)val=%d\n", \
                    udata->devaddr, udata->devname, udata->offset, (uint8_t)val);

        }
        else
        {
            /* Get the I2C client for the CPLD */
            struct i2c_client *client_ptr=NULL;
            client_ptr = (struct i2c_client *)get_device_table(udata->devname);
            if (client_ptr)
            {
                if (udata->len==2)
                {
                    uint8_t val_lsb = val & 0xFF;
                    uint8_t val_hsb = (val >> 8) & 0xFF;
                    /* TODO: Check this logic for LE and BE */
                    i2c_smbus_write_byte_data(client, udata->offset, val_lsb);
                    i2c_smbus_write_byte_data(client, udata->offset+1, val_hsb);
                }
                else
                    pddf_err(FAN, "PDDF_FAN: Doesn't support block CPLD write yet");
            }
            else
               pddf_err(FAN, "Unable to get the client handle for %s\n", udata->devname);
        }

    }
    else
    {
        if (udata->len == 1)
            i2c_smbus_write_byte_data(client, udata->offset, val);
        else if (udata->len == 2)
        {
            uint8_t val_lsb = val & 0xFF;
            uint8_t val_hsb = (val >> 8) & 0xFF;
            /* TODO: Check this logic for LE and BE */
            i2c_smbus_write_byte_data(client, udata->offset, val_lsb);
            i2c_smbus_write_byte_data(client, udata->offset+1, val_hsb);
        }
        else
        {
            pddf_err(FAN, "%s: pwm should be of len 1/2 bytes. Not setting the pwm as the length is %d\n", __FUNCTION__, udata->len);
        }
    }

	return status;
}

int sonic_i2c_set_fan_plug_record_default(struct i2c_client *client, FAN_DATA_ATTR *udata, void *info)
{
    int status = 0;
	uint32_t val = 0;
    struct fan_attr_info *painfo = (struct fan_attr_info *)info;

	val = painfo->val.intval & udata->mask;

    if (strcmp(udata->devtype, "cpld") == 0) {
        if (udata->len==1) {
            status = board_i2c_cpld_write_new(udata->devaddr, udata->devname, udata->offset, (uint8_t)val);
            pddf_dbg(FAN, "udata->devaddr=0x%x, udata->devname=%s, udata->offset=0x%x, (uint8_t)val=%d\n", \
                    udata->devaddr, udata->devname, udata->offset, (uint8_t)val);
        } else {
            /* Get the I2C client for the CPLD */
            struct i2c_client *client_ptr=NULL;
            client_ptr = (struct i2c_client *)get_device_table(udata->devname);
            if (client_ptr) {
                if (udata->len==2) {
                    uint8_t val_lsb = val & 0xFF;
                    uint8_t val_hsb = (val >> 8) & 0xFF;
                    /* TODO: Check this logic for LE and BE */
                    i2c_smbus_write_byte_data(client, udata->offset, val_lsb);
                    i2c_smbus_write_byte_data(client, udata->offset+1, val_hsb);
                } else {
                    pddf_err(FAN, "PDDF_FAN: Doesn't support block CPLD write yet");
                }
            } else {
               pddf_err(FAN, "Unable to get the client handle for %s\n", udata->devname);
            }
        }

    } else {
        if (udata->len == 1) {
            i2c_smbus_write_byte_data(client, udata->offset, val);
        } else if (udata->len == 2) {
            uint8_t val_lsb = val & 0xFF;
            uint8_t val_hsb = (val >> 8) & 0xFF;
            /* TODO: Check this logic for LE and BE */
            i2c_smbus_write_byte_data(client, udata->offset, val_lsb);
            i2c_smbus_write_byte_data(client, udata->offset+1, val_hsb);
        } else {
            pddf_err(FAN, "%s: pwm should be of len 1/2 bytes. Not setting the pwm as the length is %d\n", __FUNCTION__, udata->len);
        }
    }

	return status;
}

int sonic_i2c_get_fan_pwm_default(void *client, FAN_DATA_ATTR *udata, void *info)
{
    int status = 0;
	int val = 0;
    struct fan_attr_info *painfo = (struct fan_attr_info *)info;

    if (strcmp(udata->devtype, "cpld") == 0)
    {
        val = fan_cpld_client_read(udata);
    }
    else
    {
        if (udata->len == 1)
        {
            val = i2c_smbus_read_byte_data((struct i2c_client *)client, udata->offset);
        }
        else if (udata->len ==2)
        {
            val = i2c_smbus_read_word_swapped((struct i2c_client *)client, udata->offset);

        }
    }

	if (val < 0)
		status = val;
	else
	{
		val = val & udata->mask;
		val = val * 100 / MAX_PWM;
		painfo->val.intval = val;
	}
    return status;
}

int sonic_i2c_get_fan_fru_wp_default(void *client, FAN_DATA_ATTR *udata, void *info)
{
    int status = 0;
	int val = 0;
    struct fan_attr_info *painfo = (struct fan_attr_info *)info;

    if (strcmp(udata->devtype, "cpld") == 0)
    {
        val = fan_cpld_client_read(udata);
    }
    else
    {
        if (udata->len == 1)
        {
            val = i2c_smbus_read_byte_data((struct i2c_client *)client, udata->offset);
        }
        else if (udata->len ==2)
        {
            val = i2c_smbus_read_word_swapped((struct i2c_client *)client, udata->offset);

        }
    }

	if (val < 0)
		status = val;
	else
	{
		painfo->val.intval = ((val & udata->mask) == udata->cmpval);
	}
    return status;
}

int sonic_i2c_get_fan_plug_record_default(void *client, FAN_DATA_ATTR *udata, void *info)
{
	int status = 0;
	int val = 0;
	struct fan_attr_info *painfo = (struct fan_attr_info *)info;

	if(strcmp(udata->devtype, "cpld") == 0) {
		val = fan_cpld_client_read(udata);
	} else {
		if (udata->len == 1) {
			val = i2c_smbus_read_byte_data((struct i2c_client *)client, udata->offset);
		} else if (udata->len ==2) {
			val = i2c_smbus_read_word_swapped((struct i2c_client *)client, udata->offset);
		}
	}

	if(val < 0) {
		status = val;
	} else {
		painfo->val.intval = !((val & udata->mask) == udata->cmpval);
	}
	return status;
}

int sonic_i2c_get_fan_fault_default(void *client, FAN_DATA_ATTR *udata, void *info)
{
    int status = 0;
	int val = 0;
    struct fan_attr_info *painfo = (struct fan_attr_info *)info;

	/*Assuming fan fault to be denoted by 1 byte only*/
    if (strcmp(udata->devtype, "cpld") == 0)
    {
        val = fan_cpld_client_read(udata);
    }
    else
    {
	    val = i2c_smbus_read_byte_data((struct i2c_client *)client, udata->offset);
    }

	if (val < 0)
		status = val;
	else
		painfo->val.intval = ((val & udata->mask) == udata->cmpval);
    return status;
}


int pddf_fan_post_probe_default(struct i2c_client *client, const struct i2c_device_id *dev_id)
{

	/*Dummy func for now - check the respective platform modules*/
    return 0;
}
