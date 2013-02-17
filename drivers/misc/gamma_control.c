/*
 * Copyright 2013 Francisco Franco
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/device.h>
#include <linux/miscdevice.h>

#define GAMMACONTROL_VERSION 2

unsigned int greys_val = 114;
unsigned int mids_val = 21;
unsigned int blacks_val = 118;
unsigned int contrast_val = 0;
unsigned int brightness_val = 0;
unsigned int saturation_val = 80;
unsigned int whites_val = 48;

extern void update_vals(int array_pos);

inline int get_whites(void)
{
	return whites_val;
}

inline int get_mids(void)
{
	return mids_val;
}

inline int get_blacks(void)
{
	return blacks_val;
}

inline int get_contrast(void)
{
	return contrast_val;
}

inline int get_brightness(void)
{
	return brightness_val;
}

inline int get_saturation(void)
{
	return saturation_val;
}

inline int get_greys(void)
{
	return greys_val;
}

static ssize_t greys_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    return sprintf(buf, "%d\n", greys_val);
}

static ssize_t greys_store(struct device * dev, struct device_attribute * attr, const char * buf, size_t size)
{
    int new_val;

	sscanf(buf, "%d", &new_val);

	if (new_val != greys_val) {
		if (new_val < 0)
			new_val = 0;
		else if (new_val > 255)
			new_val = 255;
			
		pr_info("New mids: %d\n", new_val);
		greys_val = new_val;
		update_vals(1);
	}

    return size;
}

static ssize_t mids_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    return sprintf(buf, "%d\n", mids_val);
}

static ssize_t mids_store(struct device * dev, struct device_attribute * attr, const char * buf, size_t size)
{
    int new_val;

	sscanf(buf, "%d", &new_val);

	if (new_val != mids_val) {
		if (new_val < 0)
			new_val = 0;
		else if (new_val > 255)
			new_val = 255;
		pr_info("New mids: %d\n", new_val);
		mids_val = new_val;
		update_vals(2);
	}

    return size;
}

static ssize_t blacks_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    return sprintf(buf, "%d\n", blacks_val);
}

static ssize_t blacks_store(struct device * dev, struct device_attribute * attr, const char * buf, size_t size)
{
    int new_val;

	sscanf(buf, "%d", &new_val);

	if (new_val != blacks_val) {
		if (new_val < 0)
			new_val = 0;
		else if (new_val > 255)
			new_val = 255;
		pr_info("New mids: %d\n", new_val);
		blacks_val = new_val;
		update_vals(3);
	}

    return size;
}

static ssize_t contrast_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    return sprintf(buf, "%d\n", contrast_val);
}

static ssize_t contrast_store(struct device * dev, struct device_attribute * attr, const char * buf, size_t size)
{
    int new_val;

	sscanf(buf, "%d", &new_val);

	if (new_val != contrast_val) {
		if (new_val < 0)
			new_val = 0;
		else if (new_val > 255)
			new_val = 255;
		pr_info("New mids: %d\n", new_val);
		contrast_val = new_val;
		update_vals(5);
	}

    return size;
}

static ssize_t brightness_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    return sprintf(buf, "%d\n", brightness_val);
}

static ssize_t brightness_store(struct device * dev, struct device_attribute * attr, const char * buf, size_t size)
{
    int new_val;

	sscanf(buf, "%d", &new_val);

	if (new_val != brightness_val) {
		if (new_val < 0)
			new_val = 0;
		else if (new_val > 255)
			new_val = 255;
		pr_info("New mids: %d\n", new_val);
		brightness_val = new_val;
		update_vals(6);
	}

    return size;
}

static ssize_t saturation_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    return sprintf(buf, "%d\n", saturation_val);
}

static ssize_t saturation_store(struct device * dev, struct device_attribute * attr, const char * buf, size_t size)
{
    int new_val;

	sscanf(buf, "%d", &new_val);

	if (new_val != saturation_val) {
		if (new_val < 0)
			new_val = 0;
		else if (new_val > 255)
			new_val = 255;
		pr_info("New mids: %d\n", new_val);
		saturation_val = new_val;
		update_vals(7);
	}

    return size;
}

static ssize_t whites_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    return sprintf(buf, "%d\n", whites_val);
}

static ssize_t whites_store(struct device * dev, struct device_attribute * attr, const char * buf, size_t size)
{
    int new_val;

	sscanf(buf, "%d", &new_val);

	if (new_val != whites_val) {
		pr_info("New whites: %d\n", new_val);
		whites_val = new_val;
		update_vals(8);
	}

    return size;
}

static ssize_t gammacontrol_version(struct device * dev, struct device_attribute * attr, char * buf)
{
    return sprintf(buf, "%u\n", GAMMACONTROL_VERSION);
}

static DEVICE_ATTR(whites, 0777, whites_show, whites_store);
static DEVICE_ATTR(mids, 0777, mids_show, mids_store);
static DEVICE_ATTR(blacks, 0777, blacks_show, blacks_store);
static DEVICE_ATTR(contrast, 0777, contrast_show, contrast_store);
static DEVICE_ATTR(brightness, 0777, brightness_show, brightness_store);
static DEVICE_ATTR(saturation, 0777, saturation_show, saturation_store);
static DEVICE_ATTR(greys, 0777, greys_show, greys_store);
static DEVICE_ATTR(version, 0777 , gammacontrol_version, NULL);

static struct attribute *gammacontrol_attributes[] = 
    {
	&dev_attr_whites.attr,
	&dev_attr_mids.attr,
	&dev_attr_blacks.attr,
	&dev_attr_contrast.attr,
	&dev_attr_brightness.attr,
	&dev_attr_saturation.attr,
	&dev_attr_greys.attr,
	&dev_attr_version.attr,
	NULL
    };

static struct attribute_group gammacontrol_group = 
    {
	.attrs  = gammacontrol_attributes,
    };

static struct miscdevice gammacontrol_device = 
    {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "gammacontrol",
    };

static int __init gammacontrol_init(void)
{
    int ret;

    pr_info("%s misc_register(%s)\n", __FUNCTION__, gammacontrol_device.name);

    ret = misc_register(&gammacontrol_device);

    if (ret) {
	    pr_err("%s misc_register(%s) fail\n", __FUNCTION__, gammacontrol_device.name);
	    return 1;
	}

    if (sysfs_create_group(&gammacontrol_device.this_device->kobj, &gammacontrol_group) < 0) {
	    pr_err("%s sysfs_create_group fail\n", __FUNCTION__);
	    pr_err("Failed to create sysfs group for device (%s)!\n", gammacontrol_device.name);
	}

    return 0;
}
late_initcall(gammacontrol_init);