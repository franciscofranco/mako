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

#define GAMMACONTROL_VERSION 3

/*
 * Update function callback into the display driver
 * @type: if its RED, GREEN or BLUE
 * @array_pos: index of the array to be changed with the new value
 * @val: value that is going to be writen into the array and then
 * 		 pushed to the display diver
 */
extern void update_vals(int type, int array_pos, int val);

/*
 * Whites for RED, GREEN and BLUE
 */
unsigned int red_whites_val = 32;
unsigned int green_whites_val = 32;
unsigned int blue_whites_val = 32;

/*
 * Grays for RED, GREEN and BLUE
 */
unsigned int red_greys_val = 64;
unsigned int green_greys_val = 64;
unsigned int blue_greys_val = 64;

/*
 * Mids for RED, GREEN and BLUE
 */
unsigned int red_mids_val = 68;
unsigned int green_mids_val = 68;
unsigned int blue_mids_val = 68;

/*
 * Blacks for RED, GREEN and BLUE
 */
unsigned int red_blacks_val = 118;
unsigned int green_blacks_val = 118;
unsigned int blue_blacks_val = 118;

/*
 * These values are common to the RGB spectrum in this implementation
 */
unsigned int contrast_val = 25;
unsigned int brightness_val = 4;
unsigned int saturation_val = 66;

/*
 * Sysfs get/set entries
 */

static ssize_t red_whites_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    return sprintf(buf, "%d\n", red_whites_val);
}

static ssize_t red_whites_store(struct device * dev, struct device_attribute * attr, const char * buf, size_t size)
{
    int new_val;

	sscanf(buf, "%d", &new_val);

	if (new_val != red_whites_val) {
		pr_info("New RED whites: %d\n", new_val);
		red_whites_val = new_val;
		update_vals(1, 8, red_whites_val);
	}

    return size;
}

static ssize_t green_whites_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    return sprintf(buf, "%d\n", green_whites_val);
}

static ssize_t green_whites_store(struct device * dev, struct device_attribute * attr, const char * buf, size_t size)
{
    int new_val;

	sscanf(buf, "%d", &new_val);

	if (new_val != green_whites_val) {
		pr_info("New GREEN whites: %d\n", new_val);
		green_whites_val = new_val;
		update_vals(2, 8, green_whites_val);
	}

    return size;
}

static ssize_t blue_whites_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    return sprintf(buf, "%d\n", blue_whites_val);
}

static ssize_t blue_whites_store(struct device * dev, struct device_attribute * attr, const char * buf, size_t size)
{
    int new_val;

	sscanf(buf, "%d", &new_val);

	if (new_val != blue_whites_val) {
		pr_info("New BLUE whites: %d\n", new_val);
		blue_whites_val = new_val;
		update_vals(3, 8, blue_whites_val);
	}

    return size;
}

static ssize_t red_greys_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    return sprintf(buf, "%d\n", red_greys_val);
}

static ssize_t red_greys_store(struct device * dev, struct device_attribute * attr, const char * buf, size_t size)
{
    int new_val;

	sscanf(buf, "%d", &new_val);

	if (new_val != red_greys_val) {
		if (new_val < 0)
			new_val = 0;
		else if (new_val > 255)
			new_val = 255;
			
		pr_info("New RED grays: %d\n", new_val);
		red_greys_val = new_val;
		update_vals(1, 1, red_greys_val);
	}

    return size;
}

static ssize_t green_greys_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    return sprintf(buf, "%d\n", green_greys_val);
}

static ssize_t green_greys_store(struct device * dev, struct device_attribute * attr, const char * buf, size_t size)
{
    int new_val;

	sscanf(buf, "%d", &new_val);

	if (new_val != green_greys_val) {
		if (new_val < 0)
			new_val = 0;
		else if (new_val > 255)
			new_val = 255;
			
		pr_info("New GREEN grays: %d\n", new_val);
		green_greys_val = new_val;
		update_vals(2, 1, green_greys_val);
	}

    return size;
}

static ssize_t blue_greys_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    return sprintf(buf, "%d\n", blue_greys_val);
}

static ssize_t blue_greys_store(struct device * dev, struct device_attribute * attr, const char * buf, size_t size)
{
    int new_val;

	sscanf(buf, "%d", &new_val);

	if (new_val != blue_greys_val) {
		if (new_val < 0)
			new_val = 0;
		else if (new_val > 255)
			new_val = 255;
			
		pr_info("New RED grays: %d\n", new_val);
		blue_greys_val = new_val;
		update_vals(3, 1, blue_greys_val);
	}

    return size;
}

static ssize_t red_mids_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    return sprintf(buf, "%d\n", red_mids_val);
}

static ssize_t red_mids_store(struct device * dev, struct device_attribute * attr, const char * buf, size_t size)
{
    int new_val;

	sscanf(buf, "%d", &new_val);

	if (new_val != red_mids_val) {
		if (new_val < 0)
			new_val = 0;
		else if (new_val > 255)
			new_val = 255;
		pr_info("New RED mids: %d\n", new_val);
		red_mids_val = new_val;
		update_vals(1, 2, red_mids_val);
	}

    return size;
}

static ssize_t green_mids_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    return sprintf(buf, "%d\n", green_mids_val);
}

static ssize_t green_mids_store(struct device * dev, struct device_attribute * attr, const char * buf, size_t size)
{
    int new_val;

	sscanf(buf, "%d", &new_val);

	if (new_val != green_mids_val) {
		if (new_val < 0)
			new_val = 0;
		else if (new_val > 255)
			new_val = 255;
		pr_info("New GREEN mids: %d\n", new_val);
		green_mids_val = new_val;
		update_vals(2, 2, green_mids_val);
	}

    return size;
}

static ssize_t blue_mids_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    return sprintf(buf, "%d\n", blue_mids_val);
}

static ssize_t blue_mids_store(struct device * dev, struct device_attribute * attr, const char * buf, size_t size)
{
    int new_val;

	sscanf(buf, "%d", &new_val);

	if (new_val != blue_mids_val) {
		if (new_val < 0)
			new_val = 0;
		else if (new_val > 255)
			new_val = 255;
		pr_info("New BLUE mids: %d\n", new_val);
		blue_mids_val = new_val;
		update_vals(3, 2, blue_mids_val);
	}

    return size;
}

static ssize_t red_blacks_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    return sprintf(buf, "%d\n", red_blacks_val);
}

static ssize_t red_blacks_store(struct device * dev, struct device_attribute * attr, const char * buf, size_t size)
{
    int new_val;

	sscanf(buf, "%d", &new_val);

	if (new_val != red_blacks_val) {
		if (new_val < 0)
			new_val = 0;
		else if (new_val > 255)
			new_val = 255;
		pr_info("New RED blacks: %d\n", new_val);
		red_blacks_val = new_val;
		update_vals(1, 3, red_blacks_val);
	}

    return size;
}

static ssize_t green_blacks_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    return sprintf(buf, "%d\n", green_blacks_val);
}

static ssize_t green_blacks_store(struct device * dev, struct device_attribute * attr, const char * buf, size_t size)
{
    int new_val;

	sscanf(buf, "%d", &new_val);

	if (new_val != green_blacks_val) {
		if (new_val < 0)
			new_val = 0;
		else if (new_val > 255)
			new_val = 255;
		pr_info("New GREEN blacks: %d\n", new_val);
		green_blacks_val = new_val;
		update_vals(2, 3, green_blacks_val);
	}

    return size;
}

static ssize_t blue_blacks_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    return sprintf(buf, "%d\n", blue_blacks_val);
}

static ssize_t blue_blacks_store(struct device * dev, struct device_attribute * attr, const char * buf, size_t size)
{
    int new_val;

	sscanf(buf, "%d", &new_val);

	if (new_val != blue_blacks_val) {
		if (new_val < 0)
			new_val = 0;
		else if (new_val > 255)
			new_val = 255;
		pr_info("New BLUE blacks: %d\n", new_val);
		blue_blacks_val = new_val;
		update_vals(3, 3, blue_blacks_val);
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
		else if (new_val > 31)
			new_val = 31;
		pr_info("New contrast: %d\n", new_val);
		contrast_val = new_val;
		update_vals(5, 0, contrast_val);
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
		else if (new_val > 31)
			new_val = 31;
		pr_info("New brightness: %d\n", new_val);
		brightness_val = new_val;
		update_vals(6, 0, brightness_val);
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
		pr_info("New saturation: %d\n", new_val);
		saturation_val = new_val;
		update_vals(7, 0, saturation_val);
	}

    return size;
}

static ssize_t gammacontrol_version(struct device * dev, struct device_attribute * attr, char * buf)
{
    return sprintf(buf, "%u\n", GAMMACONTROL_VERSION);
}

static DEVICE_ATTR(red_whites, 0664, red_whites_show, red_whites_store);
static DEVICE_ATTR(green_whites, 0664, green_whites_show, green_whites_store);
static DEVICE_ATTR(blue_whites, 0664, blue_whites_show, blue_whites_store);

static DEVICE_ATTR(red_greys, 0664, red_greys_show, red_greys_store);
static DEVICE_ATTR(green_greys, 0664, green_greys_show, green_greys_store);
static DEVICE_ATTR(blue_greys, 0664, blue_greys_show, blue_greys_store);

static DEVICE_ATTR(red_mids, 0664, red_mids_show, red_mids_store);
static DEVICE_ATTR(green_mids, 0664, green_mids_show, green_mids_store);
static DEVICE_ATTR(blue_mids, 0664, blue_mids_show, blue_mids_store);

static DEVICE_ATTR(red_blacks, 0664, red_blacks_show, red_blacks_store);
static DEVICE_ATTR(green_blacks, 0664, green_blacks_show, green_blacks_store);
static DEVICE_ATTR(blue_blacks, 0664, blue_blacks_show, blue_blacks_store);

static DEVICE_ATTR(contrast, 0664, contrast_show, contrast_store);
static DEVICE_ATTR(brightness, 0664, brightness_show, brightness_store);
static DEVICE_ATTR(saturation, 0664, saturation_show, saturation_store);

static DEVICE_ATTR(version, 0664 , gammacontrol_version, NULL);

static struct attribute *gammacontrol_attributes[] = 
{
	&dev_attr_red_whites.attr,
	&dev_attr_green_whites.attr,
	&dev_attr_blue_whites.attr,
	&dev_attr_red_greys.attr,
	&dev_attr_green_greys.attr,
	&dev_attr_blue_greys.attr,
	&dev_attr_red_mids.attr,
	&dev_attr_green_mids.attr,
	&dev_attr_blue_mids.attr,
	&dev_attr_red_blacks.attr,
	&dev_attr_green_blacks.attr,
	&dev_attr_blue_blacks.attr,
	&dev_attr_contrast.attr,
	&dev_attr_brightness.attr,
	&dev_attr_saturation.attr,
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