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
#include <linux/hotplug.h>

/*
 * Sysfs get/set entries
 */

static ssize_t first_level_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    return sprintf(buf, "%u\n", get_first_level());
}

static ssize_t first_level_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
    unsigned int new_val;
    
	sscanf(buf, "%u", &new_val);
    
    if (new_val != get_first_level() && new_val >= 0 && new_val <= 100)
    {
        update_first_level(new_val);
    }
    
    return size;
}

static DEVICE_ATTR(first_level, 0664, first_level_show, first_level_store);

static struct attribute *mako_hotplug_control_attributes[] =
{
	&dev_attr_first_level.attr,
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
