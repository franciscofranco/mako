/* Copyright (c) 2012-2013, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/msm_tsens.h>
#include <linux/workqueue.h>
#include <linux/cpu.h>
#include <linux/cpufreq.h>
#include <linux/msm_tsens.h>
#include <linux/msm_thermal.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <mach/cpufreq.h>

#define THROTTLE_FREQUENCY 1026000

struct cpus {
    bool throttling;
};

struct cpus cpu_stats;

/*
 * Poll for temperature changes every 2 seconds.
 * It will scale based on the device temperature.
 */
unsigned int polling = HZ*2;

unsigned int temp_threshold = 70;
module_param(temp_threshold, int, 0755);

static struct msm_thermal_data msm_thermal_info;

static struct workqueue_struct *wq;
static struct delayed_work check_temp_work;

unsigned short get_threshold(void)
{
	return temp_threshold;
}

static void limit_cpu_freqs(unsigned int freq)
{
    int cpu;
    
    for_each_present_cpu(cpu)
    {
        msm_cpufreq_set_freq_limits(cpu, MSM_CPUFREQ_NO_LIMIT, freq);
        pr_info("Thermal Throttling activated: CPU%d limited to %d\n",
                cpu, freq);
    }
    
    cpu_stats.throttling = true;
}

static void unlimit_cpu_freqs(void)
{
    int cpu;
    
    for_each_present_cpu(cpu)
    {
        msm_cpufreq_set_freq_limits(cpu, MSM_CPUFREQ_NO_LIMIT,
                                    MSM_CPUFREQ_NO_LIMIT);
        pr_info("Thermal Throttling deactivated: CPU%d unlocked\n", cpu);
    }
    
    cpu_stats.throttling = false;
}

static void check_temp(struct work_struct *work)
{
	struct tsens_device tsens_dev;
	long temp = 0;
    
	tsens_dev.sensor_num = msm_thermal_info.sensor_id;
	tsens_get_temp(&tsens_dev, &temp);
    
	/* temperature is high, lets throttle even more and
     poll faster (every .25s) */
	if (temp >= temp_threshold)
	{
        if (!cpu_stats.throttling)
        {
            limit_cpu_freqs(THROTTLE_FREQUENCY);
            polling = HZ/4;
        }
	}
    
	/* the device is in safe temperature, polling is normal (every second) */
	else if (temp < (temp_threshold - 5))
	{
        if (cpu_stats.throttling)
        {
            unlimit_cpu_freqs();
            polling = HZ*2;
        }
	}
    
	queue_delayed_work(wq, &check_temp_work, polling);
}

int __devinit msm_thermal_init(struct msm_thermal_data *pdata)
{
	int ret = 0;
    
	BUG_ON(!pdata);
	BUG_ON(pdata->sensor_id >= TSENS_MAX_SENSORS);
	memcpy(&msm_thermal_info, pdata, sizeof(struct msm_thermal_data));
    
	wq = alloc_workqueue("msm_thermal_workqueue", WQ_HIGHPRI, 0);
    
    if (!wq)
        return -ENOMEM;
    
	INIT_DELAYED_WORK(&check_temp_work, check_temp);
	queue_delayed_work(wq, &check_temp_work, HZ*30);
    
	return ret;
}

static int __devinit msm_thermal_dev_probe(struct platform_device *pdev)
{
	int ret = 0;
	char *key = NULL;
	struct device_node *node = pdev->dev.of_node;
	struct msm_thermal_data data;
    
	memset(&data, 0, sizeof(struct msm_thermal_data));
	key = "qcom,sensor-id";
	ret = of_property_read_u32(node, key, &data.sensor_id);
	if (ret)
		goto fail;
	WARN_ON(data.sensor_id >= TSENS_MAX_SENSORS);
    
fail:
	if (ret)
		pr_err("%s: Failed reading node=%s, key=%s\n",
		       __func__, node->full_name, key);
	else
		ret = msm_thermal_init(&data);
    
	return ret;
}

static struct of_device_id msm_thermal_match_table[] = {
	{.compatible = "qcom,msm-thermal"},
	{},
};

static struct platform_driver msm_thermal_device_driver = {
	.probe = msm_thermal_dev_probe,
	.driver = {
		.name = "msm-thermal",
		.owner = THIS_MODULE,
		.of_match_table = msm_thermal_match_table,
	},
};

int __init msm_thermal_device_init(void)
{
	return platform_driver_register(&msm_thermal_device_driver);
}
