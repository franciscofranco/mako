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

#define MAKO_HOTPLUG_CONTROL_VERSION 1

unsigned int first_level = 90;
unsigned int second_level = 25;
unsigned int third_level = 50;

unsigned int suspend_frequency = 702000;

extern void update_first_level(unsigned int level);
extern void update_second_level(unsigned int level);
extern void update_third_level(unsigned int level);

extern void update_suspend_freq(unsigned int freq);

/*
 * Sysfs get/set entries
 */

static ssize_t first_level_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    return sprintf(buf, "%u\n", first_level);
}

static ssize_t first_level_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
    unsigned int new_val;
    
	sscanf(buf, "%u", &new_val);
    
    if (new_val != first_level && new_val >= 0 && new_val <= 100)
    {
        update_first_level(new_val);
        first_level = new_val;
    }
    
    return size;
}

static ssize_t second_level_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    return sprintf(buf, "%u\n", second_level);
}

static ssize_t second_level_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
    unsigned int new_val;
    
	sscanf(buf, "%u", &new_val);
    
    if (new_val != second_level && new_val >= 0 && new_val <= 100)
    {
        update_second_level(new_val);
        second_level = new_val;
    }
    
    return size;
}

static ssize_t third_level_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    return sprintf(buf, "%u\n", third_level);
}

static ssize_t third_level_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
    unsigned int new_val;
    
	sscanf(buf, "%u", &new_val);
    
    if (new_val != third_level && new_val >= 0 && new_val <= 100)
    {
        update_third_level(new_val);
        third_level = new_val;
    }
    
    return size;
}

static ssize_t suspend_frequency_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    return sprintf(buf, "%u\n", suspend_frequency);
}

static ssize_t suspend_frequency_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
    unsigned int new_val;
    
	sscanf(buf, "%u", &new_val);
    
    if (new_val != suspend_frequency && new_val >= 0 && new_val <= 1512000)
    {
        update_suspend_freq(new_val);
        suspend_frequency = new_val;
    }
    
    return size;
}

static ssize_t mako_hotplug_control_version(struct device *dev, struct device_attribute* attr, char *buf)
{
    return sprintf(buf, "%d\n", MAKO_HOTPLUG_CONTROL_VERSION);
}

static DEVICE_ATTR(first_level, 0777, first_level_show, first_level_store);
static DEVICE_ATTR(second_level, 0777, second_level_show, second_level_store);
static DEVICE_ATTR(third_level, 0777, third_level_show, third_level_store);
static DEVICE_ATTR(suspend_frequency, 0777, suspend_frequency_show, suspend_frequency_store);

static DEVICE_ATTR(version, 0777 , mako_hotplug_control_version, NULL);

static struct attribute *mako_hotplug_control_attributes[] =
{
	&dev_attr_first_level.attr,
    &dev_attr_second_level.attr,
    &dev_attr_third_level.attr,
    &dev_attr_suspend_frequency.attr,
	&dev_attr_version.attr,
	NULL
};

static struct attribute_group mako_hotplug_control_group =
{
	.attrs  = mako_hotplug_control_attributes,
};

static struct miscdevice mako_hotplug_control_device =
{
	.minor = MISC_DYNAMIC_MINOR,
	.name = "mako_hotplug_control",
};

static int __init mako_hotplug_control_init(void)
{
    int ret;
    
    pr_info("%s misc_register(%s)\n", __FUNCTION__, mako_hotplug_control_device.name);
    
    ret = misc_register(&mako_hotplug_control_device);
    
    if (ret)
    {
	    pr_err("%s misc_register(%s) fail\n", __FUNCTION__, mako_hotplug_control_device.name);
	    return 1;
	}
    
    if (sysfs_create_group(&mako_hotplug_control_device.this_device->kobj, &mako_hotplug_control_group) < 0)
    {
	    pr_err("%s sysfs_create_group fail\n", __FUNCTION__);
	    pr_err("Failed to create sysfs group for device (%s)!\n", mako_hotplug_control_device.name);
	}
    
    return 0;
}
late_initcall(mako_hotplug_control_init);