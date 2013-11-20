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

#define LOW_FREQ 8
#define FAST_COUNTER 4
#define SLOW_COUNTER 8
#define HISTORY_SIZE 10

unsigned int temp_threshold = 65;
module_param(temp_threshold, int, 0755);

static struct msm_thermal_data msm_thermal_info;

static struct workqueue_struct *wq;
static struct delayed_work check_temp_work;

static int limit_idx;
static int limit_idx_low;
static int limit_idx_high;
static int default_limit_idx_high;
static struct cpufreq_frequency_table *table;

unsigned short get_threshold(void)
{
	return temp_threshold;
}

static int msm_thermal_get_freq_table(void)
{
	int ret = 0;
	int i = 0;

	table = cpufreq_frequency_get_table(0);
	if (table == NULL) {
		pr_debug("%s: error reading cpufreq table\n", __func__);
		ret = -EINVAL;
		goto fail;
	}

	while (table[i].frequency != CPUFREQ_TABLE_END)
		i++;

	limit_idx_low = 0;
	default_limit_idx_high = limit_idx_high = limit_idx = i - 1;
	BUG_ON(limit_idx_high <= 0 || limit_idx_high <= limit_idx_low);
fail:
	return ret;
}

static void limit_cpu_freqs(unsigned int freq)
{
	int cpu;
	
	for_each_present_cpu(cpu)
	{
		msm_cpufreq_set_freq_limits(cpu, MSM_CPUFREQ_NO_LIMIT, freq);
	}
}

static unsigned short counting_range(long temp)
{
	if (temp >= temp_threshold + 10 || temp <= temp_threshold - 15)
		return FAST_COUNTER;
	else
		return SLOW_COUNTER;
}

static void check_temp(struct work_struct *work)
{
	struct tsens_device tsens_dev;
	static unsigned short counter, heat_counter, range;
	static int limit_init;
	static unsigned int polling;
	static long temp;
	static unsigned short history[HISTORY_SIZE];
	static unsigned short full_heat;
	bool heatwave;
	short av_heat;

	int ret = 0;
	
	tsens_dev.sensor_num = msm_thermal_info.sensor_id;
	tsens_get_temp(&tsens_dev, &temp);
		
	if (unlikely(!limit_init))
	{
		ret = msm_thermal_get_freq_table();
		if (ret)
			goto reschedule;
		else
			limit_init = 1;
	}
	
	full_heat -= history[heat_counter];
	history[heat_counter] = (short) temp;
	
	full_heat += (short) temp;

	if (unlikely(++heat_counter >= HISTORY_SIZE))
		heat_counter = 0;

	av_heat = full_heat / HISTORY_SIZE;
	
	if (unlikely(temp - av_heat >= 10))
		heatwave = true;
	else
		heatwave = false;
	
	if (unlikely(((temp >= temp_threshold + 20 || temp >= 90) 
			&& limit_idx > LOW_FREQ)) || heatwave)
	{
		limit_idx = LOW_FREQ;
		limit_cpu_freqs(table[limit_idx].frequency);
		polling = HZ/4;
	}
	else if (temp >= temp_threshold)
	{
		if (counter >= range)
		{
			if (limit_idx > LOW_FREQ)
			{	
				limit_idx--;
				limit_cpu_freqs(table[limit_idx].frequency);
			}
				
			range = counting_range(temp);
			counter = 0;
		}
		else
		{
			counter++;
		}
		
		polling = HZ/4;
	}
	else if (temp <= temp_threshold - 5 && limit_idx < limit_idx_high)
	{
		if (counter >= range)
		{		
			limit_idx++;
			limit_cpu_freqs(table[limit_idx].frequency);
			
			range = counting_range(temp);
			counter = 0;
		}
		else
		{
			counter++;
		}		
		
		polling = HZ/4;
	}
	else if (temp >= temp_threshold - 15)
	{
		if (counter >= range)
		{
			range = counting_range(temp);
			counter = 0;
		}
		else
		{
			counter++;
		}
		
		polling = HZ/4;
	}
	else
	{
		polling = HZ;
	}
	
/*	if (heatwave){pr_info("HEATWAVE");}
	pr_info("---------------------");
	pr_info("Temp:\t\t%ld",temp);
	pr_info("Av Temp:\t%d",av_heat);
	pr_info("Counter:\t%d",counter);
	pr_info("Range:\t%d",range);
	pr_info("CurFreq:\t%d",table[limit_idx].frequency);
	pr_info("---------------------");*/

reschedule:
	
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
