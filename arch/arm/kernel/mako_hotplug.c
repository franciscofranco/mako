/*
 * Copyright (c) 2013, Francisco Franco <franciscofranco.1990@gmail.com>. 
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
 * Simple no bullshit hot[un]plug driver for SMP
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/cpu.h>
#include <linux/workqueue.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/earlysuspend.h>
#include <linux/cpufreq.h>
#include <linux/delay.h>
#include <linux/hotplug.h>
 
#include <mach/cpufreq.h>

#define DEFAULT_FIRST_LEVEL 60
#define DEFAULT_THIRD_LEVEL 30
#define DEFAULT_SUSPEND_FREQ 702000
#define DEFAULT_CORES_ON_TOUCH 2
#define DEFAULT_COUNTER 20
#define BOOST_THRESHOLD 5000

//#define DEBUG

struct cpu_load_data {
	u64 prev_cpu_idle;
	u64 prev_cpu_wall;
};

static DEFINE_PER_CPU(struct cpu_load_data, cpuload);

static unsigned int default_first_level = DEFAULT_FIRST_LEVEL;
static unsigned int default_third_level = DEFAULT_THIRD_LEVEL;
static unsigned int cores_on_touch = DEFAULT_CORES_ON_TOUCH;
static unsigned int suspend_frequency = DEFAULT_SUSPEND_FREQ;
static unsigned long now;
static bool core_boost[4];
static short first_counter = 0;
static short third_counter = 0;

static struct workqueue_struct *wq;
static struct workqueue_struct *pm_wq;
static struct delayed_work decide_hotplug;
static struct work_struct resume;
static struct work_struct suspend;

unsigned int get_cur_max(unsigned int cpu);

static inline int get_cpu_load(unsigned int cpu)
{
	struct cpu_load_data *pcpu = &per_cpu(cpuload, cpu);
	struct cpufreq_policy policy;
	u64 cur_wall_time, cur_idle_time;
	unsigned int idle_time, wall_time;
	unsigned int cur_load;
	unsigned int cur_max, max_freq, cur_freq;

	cpufreq_get_policy(&policy, cpu);
	
	cur_idle_time = get_cpu_idle_time(cpu, &cur_wall_time, gpu_idle ? 0 : 1);

	wall_time = (unsigned int) (cur_wall_time - pcpu->prev_cpu_wall);
	pcpu->prev_cpu_wall = cur_wall_time;

	idle_time = (unsigned int) (cur_idle_time - pcpu->prev_cpu_idle);
	pcpu->prev_cpu_idle = cur_idle_time;

	if (unlikely(!wall_time || wall_time < idle_time))
		return 0;

	/* get the correct max frequency and current freqency */
	cur_max = get_cur_max(policy.cpu);

	if (cur_max >= policy.max)
	{
		max_freq = policy.max;
		cur_freq = policy.cur;
	}
	else
	{
		max_freq = cur_max;
		cur_freq = policy.cur > cur_max ? cur_max : policy.cur;
	}

	cur_load = 100 * (wall_time - idle_time) / wall_time;

	return (cur_load * cur_freq) / max_freq;
}

static void __ref online_core(unsigned short cpus_num)
{
	unsigned int cpu;
	
	if (cpus_num > 3)
		return;
	
	for_each_possible_cpu(cpu) 
	{
		if (!cpu_online(cpu)) 
		{
			cpu_up(cpu);
			break;
		}
	}
	
	if (cpus_num < cores_on_touch)
		core_boost[cpu] = true;
	
	first_counter = 0;
	third_counter = -DEFAULT_COUNTER;
	
	return;
}

static void __ref offline_core(unsigned int cpu)
{   
	if (!cpu)
		return;

	core_boost[cpu] = false;	
	cpu_down(cpu);
	
	first_counter = 0;
	third_counter = 0;
	
	return;
}

unsigned int scale_first_level(void)
{
	if (!dynamic_scaling)
		return default_first_level;
		
	if (gpu_idle)
	{
		if (default_first_level + 20 <= 90)
			return default_first_level + 20;
		else
			return 90;
	}
	else
		return default_first_level;
}

unsigned int scale_third_level(void)
{
	if (!dynamic_scaling)
		return default_third_level;
		
	if (gpu_idle)
	{
		if (default_third_level + 20 <= 60)
			return default_third_level + 20;
		else
			return 60;
	}
	else
		return default_third_level;
}

void __ref touchboost_func(void)
{	
	unsigned int i, core, cpus_num, boost_freq;
	struct cpufreq_policy policy;
	
	cpus_num = num_online_cpus();
	boost_freq = get_input_boost_freq();
	
	if (cpus_num < cores_on_touch)
	{
		for(i = cpus_num; i < cores_on_touch; i++)
		{
			online_core(cpus_num);
		}
	}
	core = 0;
	
	for_each_possible_cpu(core)
	{
		if (core_boost[core])
		{
			cpufreq_get_policy(&policy, core);
			if (policy.cur < boost_freq)
				__cpufreq_driver_target(&policy, boost_freq, 
						CPUFREQ_RELATION_H);
		}
	}
}

static void __ref decide_hotplug_func(struct work_struct *work)
{
	unsigned int cpu, lowest_cpu = 0;
	unsigned int load, av_load = 0, lowest_cpu_load = 100;
	unsigned short online_cpus;
	unsigned short up_val, down_val;

#ifdef DEBUG
	short load_array[4] = {};
    int cpu_debug = 0;
	static unsigned long debug_time_stamp;
    struct cpufreq_policy policy;
#endif

	now = ktime_to_ms(ktime_get());
	online_cpus = num_online_cpus();

	for_each_online_cpu(cpu) 
	{
		load = get_cpu_load(cpu);
		
#ifdef DEBUG
		load_array[cpu] = load;
#endif		
		
		if (load < lowest_cpu_load && cpu && !(core_boost[cpu] && 
				now - time_stamp < BOOST_THRESHOLD))
		{
			lowest_cpu = cpu;
			lowest_cpu_load = load;
		}
		
		av_load += load;
	}

	av_load = av_load / online_cpus;
	
	if (gpu_idle)
	{
		up_val = 2;
		down_val = 3;
	}
	else
	{
		up_val = 4;
		down_val = 2;		
	}
	
	if (av_load >= scale_first_level())
	{
		if (first_counter < DEFAULT_COUNTER)
			first_counter += up_val;
		
		if (third_counter > 0)
			third_counter -= up_val;
			
		if (first_counter >= DEFAULT_COUNTER)
			online_core(online_cpus);	
	}
	else if (av_load <= scale_third_level())
	{
		if (third_counter < DEFAULT_COUNTER)
			third_counter += down_val;
		
		if (first_counter > 0)
			first_counter -= down_val;
			
		if (third_counter >= DEFAULT_COUNTER)
			offline_core(lowest_cpu);	
	}
	else
	{
		if (first_counter > 0)
			first_counter -= down_val;
		
		if (third_counter > 0)
			third_counter -= down_val; 
	}
	
#ifdef DEBUG
    if (debug_time_stamp < ktime_to_ms(ktime_get()) - 100)
    {
		cpu = 0;
		pr_info("----HOTPLUG DEBUG INFO----\n");
		pr_info("Cores on:\t%d", num_online_cpus());
		pr_info("Core0:\t%d", load_array[0]);
		pr_info("Core1:\t%d", load_array[1]);
		pr_info("Core2:\t%d", load_array[2]);
		pr_info("Core3:\t%d", load_array[3]);
		pr_info("Av Load:\t%d", av_load);
		pr_info("-------------------------");
		pr_info("Up count:\t%d\n",first_counter);
		pr_info("Dw count:\t%d\n",third_counter);
	
		if (gpu_idle)
			pr_info("Gpu Idle: true");
		else
			pr_info("Gpu Idle: false");
		if (is_touching)
			pr_info("Touch: true");
		else
			pr_info("Touch: false");
		
		for_each_possible_cpu(cpu_debug)
		{
			if (cpu_online(cpu_debug))
			{
				cpufreq_get_policy(&policy, cpu_debug);
				pr_info("cpu%d:\t%d MHz",cpu_debug,policy.cur/1000);
			}
			else
				pr_info("cpu%d:\toff",cpu_debug);
		}
		pr_info("First level: %d", scale_first_level());
		pr_info("Third level: %d", scale_third_level());
		pr_info("-----------------------------------------");
		debug_time_stamp = now;
	}
#endif

    queue_delayed_work(wq, &decide_hotplug, msecs_to_jiffies(50));
}

static void suspend_func(struct work_struct *work)
{
	int cpu;

    /* cancel the hotplug work when the screen is off and flush the WQ */
	flush_workqueue(wq);
    cancel_delayed_work_sync(&decide_hotplug);
	cancel_work_sync(&resume);

    pr_info("Early Suspend stopping Hotplug work...\n");
    
	for_each_possible_cpu(cpu) 
	{
		if (cpu)
		{
			core_boost[cpu] = false;
			cpu_down(cpu);
		}
		
	}

	is_touching = false;
	first_counter = 0;
	third_counter = 0;

	/* cap max frequency to 702MHz by default */
	msm_cpufreq_set_freq_limits(0, MSM_CPUFREQ_NO_LIMIT, 
			suspend_frequency);
}

static void __ref resume_func(struct work_struct *work)
{
	cancel_work_sync(&suspend);

	/* restore max frequency */
    msm_cpufreq_set_freq_limits(0, MSM_CPUFREQ_NO_LIMIT, MSM_CPUFREQ_NO_LIMIT);
    pr_info("Cpulimit: Late resume - restore cpu%d max frequency.\n", 0);

	/* touchboost */
	is_touching = true;
    idle_counter = -10;
    gpu_idle = false;

	freq_boosted_time = time_stamp = ktime_to_ms(ktime_get());
	is_touching = true;
	
	touchboost_func();
    
    pr_info("Late Resume starting Hotplug work...\n");
    queue_delayed_work(wq, &decide_hotplug, HZ);	
}

static void mako_hotplug_early_suspend(struct early_suspend *handler)
{	 
    queue_work(pm_wq, &suspend);
}

static void mako_hotplug_late_resume(struct early_suspend *handler)
{  
	queue_work(pm_wq, &resume);
}

static struct early_suspend mako_hotplug_suspend =
{
    .level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1,
	.suspend = mako_hotplug_early_suspend,
	.resume = mako_hotplug_late_resume,
};

/* sysfs functions for external driver */

void update_first_level(unsigned int level)
{
	default_first_level = level;
}

void update_third_level(unsigned int level)
{
	default_third_level = level;
}

void update_suspend_frequency(unsigned int freq)
{
	suspend_frequency = freq;
}

void update_cores_on_touch(unsigned int num)
{
	cores_on_touch = num;
}

unsigned int get_first_level()
{
	return default_first_level;
}

unsigned int get_third_level()
{
	return default_third_level;
}

unsigned int get_suspend_frequency()
{
	return suspend_frequency;
}

unsigned int get_cores_on_touch()
{
	return cores_on_touch;
}

bool get_core_boost(unsigned int cpu)
{
	return core_boost[cpu];
}

/* end sysfs functions from external driver */

int __init mako_hotplug_init(void)
{
	pr_info("Mako Hotplug driver started.\n");

    wq = alloc_ordered_workqueue("mako_hotplug_workqueue", 0);
    
    if (!wq)
        return -ENOMEM;

	pm_wq = alloc_workqueue("pm_workqueue", 0, 1);
    
    if (!pm_wq)
        return -ENOMEM;

    INIT_DELAYED_WORK(&decide_hotplug, decide_hotplug_func);
	INIT_WORK(&resume, resume_func);
	INIT_WORK(&suspend, suspend_func);
    queue_delayed_work(wq, &decide_hotplug, HZ*25);
    
    register_early_suspend(&mako_hotplug_suspend);
    
    return 0;
}
late_initcall(mako_hotplug_init);

