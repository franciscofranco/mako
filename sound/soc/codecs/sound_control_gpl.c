/*
 * Author: Paul Reioux aka Faux123 <reioux@gmail.com>
 *
 * WCD93xx sound control module
 * Copyright 2013 Paul Reioux
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/kallsyms.h>

#include <sound/control.h>
#include <sound/soc.h>

extern struct snd_kcontrol_new *gpl_faux_snd_controls_ptr;

#define SOUND_CONTROL_MAJOR_VERSION	2
#define SOUND_CONTROL_MINOR_VERSION	0

#define CAMCORDER_MIC_OFFSET    20
#define HANDSET_MIC_OFFSET      21
#define SPEAKER_OFFSET          10
#define HEADPHONE_L_OFFSET      8
#define HEADPHONE_R_OFFSET      9

static ssize_t cam_mic_gain_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	struct soc_mixer_control *l_mixer_ptr;

	l_mixer_ptr =
		(struct soc_mixer_control *)gpl_faux_snd_controls_ptr[CAMCORDER_MIC_OFFSET].
			private_value;

	return sprintf(buf, "%d", l_mixer_ptr->max);
}

static ssize_t cam_mic_gain_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	int l_max;
	int l_delta;
	struct soc_mixer_control *l_mixer_ptr;

	l_mixer_ptr =
		(struct soc_mixer_control *)gpl_faux_snd_controls_ptr[CAMCORDER_MIC_OFFSET].
			private_value;

	sscanf(buf, "%d", &l_max);
 
	// limit the max gain
	l_delta = l_max - l_mixer_ptr->platform_max;
	l_mixer_ptr->platform_max = l_max;
	l_mixer_ptr->max = l_max;
	l_mixer_ptr->min += l_delta;

	return (count);
}

static ssize_t mic_gain_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	struct soc_mixer_control *l_mixer_ptr;

	l_mixer_ptr =
		(struct soc_mixer_control *)gpl_faux_snd_controls_ptr[HANDSET_MIC_OFFSET].
			private_value;

	return sprintf(buf, "%d", l_mixer_ptr->max);
}

static ssize_t mic_gain_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	int l_max;
	int l_delta;
	struct soc_mixer_control *l_mixer_ptr;

	l_mixer_ptr =
		(struct soc_mixer_control *)gpl_faux_snd_controls_ptr[HANDSET_MIC_OFFSET].
			private_value;

	sscanf(buf, "%d", &l_max);

	l_delta = l_max - l_mixer_ptr->platform_max;
	l_mixer_ptr->platform_max = l_max;
	l_mixer_ptr->max = l_max;
	l_mixer_ptr->min += l_delta;

	return (count);
}

static ssize_t speaker_gain_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	struct soc_mixer_control *l_mixer_ptr;

	l_mixer_ptr =
		(struct soc_mixer_control *)gpl_faux_snd_controls_ptr[SPEAKER_OFFSET].
			private_value;

	return sprintf(buf, "%d", l_mixer_ptr->max);
}

static ssize_t speaker_gain_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	int l_max;
	int l_delta;
	struct soc_mixer_control *l_mixer_ptr;

	l_mixer_ptr =
		(struct soc_mixer_control *)gpl_faux_snd_controls_ptr[SPEAKER_OFFSET].
			private_value;

	sscanf(buf, "%d", &l_max);

	l_delta = l_max - l_mixer_ptr->platform_max;
	l_mixer_ptr->platform_max = l_max;
	l_mixer_ptr->max = l_max;
	l_mixer_ptr->min += l_delta;

	return (count);
}

static ssize_t headphone_gain_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	struct soc_mixer_control *l_mixer_ptr, *r_mixer_ptr;

	l_mixer_ptr =
		(struct soc_mixer_control *)gpl_faux_snd_controls_ptr[HEADPHONE_L_OFFSET].
			private_value;
	r_mixer_ptr =
		(struct soc_mixer_control *)gpl_faux_snd_controls_ptr[HEADPHONE_R_OFFSET].
			private_value;

	return sprintf(buf, "%d %d",
			l_mixer_ptr->max,
			r_mixer_ptr->max);
}

static ssize_t headphone_gain_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	int l_max, r_max;
	int l_delta, r_delta;
	struct soc_mixer_control *l_mixer_ptr, *r_mixer_ptr;

	l_mixer_ptr =
		(struct soc_mixer_control *)gpl_faux_snd_controls_ptr[HEADPHONE_L_OFFSET].
			private_value;
	r_mixer_ptr =
		(struct soc_mixer_control *)gpl_faux_snd_controls_ptr[HEADPHONE_R_OFFSET].
			private_value;

	sscanf(buf, "%d %d", &l_max, &r_max);

	l_delta = l_max - l_mixer_ptr->platform_max;
	l_mixer_ptr->platform_max = l_max;
	l_mixer_ptr->max = l_max;
	l_mixer_ptr->min += l_delta;

	r_delta = r_max - r_mixer_ptr->platform_max;
	r_mixer_ptr->platform_max = r_max;
	r_mixer_ptr->max = r_max;
	r_mixer_ptr->min += r_delta;
 
	return count;
}

static ssize_t sound_control_version_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "version: %u.%u\n",
			SOUND_CONTROL_MAJOR_VERSION,
			SOUND_CONTROL_MINOR_VERSION);
}

static struct kobj_attribute cam_mic_gain_attribute =
	__ATTR(gpl_cam_mic_gain,
		0666,
		cam_mic_gain_show,
		cam_mic_gain_store);

static struct kobj_attribute mic_gain_attribute =
	__ATTR(gpl_mic_gain,
		0666,
		mic_gain_show,
		mic_gain_store);

static struct kobj_attribute speaker_gain_attribute =
	__ATTR(gpl_speaker_gain,
		0666,
		speaker_gain_show,
		speaker_gain_store);

static struct kobj_attribute headphone_gain_attribute = 
	__ATTR(gpl_headphone_gain,
		0666,
		headphone_gain_show,
		headphone_gain_store);

static struct kobj_attribute sound_control_version_attribute = 
	__ATTR(gpl_sound_control_version,
		0444,
		sound_control_version_show, NULL);

static struct attribute *sound_control_attrs[] =
	{
		&cam_mic_gain_attribute.attr,
		&mic_gain_attribute.attr,
		&speaker_gain_attribute.attr,
		&headphone_gain_attribute.attr,
		&sound_control_version_attribute.attr,
		NULL,
	};

static struct attribute_group sound_control_attr_group =
	{
		.attrs = sound_control_attrs,
	};

static struct kobject *sound_control_kobj;

static int sound_control_init(void)
{
	int sysfs_result;

	if (gpl_faux_snd_controls_ptr == NULL) {
		pr_err("%s sound_controls_ptr is NULL!\n", __FUNCTION__);
		return -1;
	}

	sound_control_kobj =
		kobject_create_and_add("sound_control", kernel_kobj);

	if (!sound_control_kobj) {
		pr_err("%s sound_control_kobj create failed!\n",
			__FUNCTION__);
		return -ENOMEM;
        }

	sysfs_result = sysfs_create_group(sound_control_kobj,
			&sound_control_attr_group);

	if (sysfs_result) {
		pr_info("%s sysfs create failed!\n", __FUNCTION__);
		kobject_put(sound_control_kobj);
	}
	return sysfs_result;
}

static void sound_control_exit(void)
{
	if (sound_control_kobj != NULL)
		kobject_put(sound_control_kobj);
}

module_init(sound_control_init);
module_exit(sound_control_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Paul Reioux <reioux@gmail.com>");
MODULE_DESCRIPTION("Sound Control Module GPL Edition");

