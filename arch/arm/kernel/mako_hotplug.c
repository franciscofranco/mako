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
 * Simple no bullshit hot[in]plug driver for SMP
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/cpu.h>
#include <linux/workqueue.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/earlysuspend.h>
#include <linux/rq_stats.h>
#include <linux/cpufreq.h>
#include <linux/delay.h>
#include <linux/hotplug.h>

#include <mach/cpufreq.h>

#define SEC_THRESHOLD 2000
#define HISTORY_SIZE 10
#define DEFAULT_FIRST_LEVEL 90
#define DEFAULT_SECOND_LEVEL 50
#define DEFAULT_THIRD_LEVEL 30
#define DEFAULT_FOURTH_LEVEL 10
#define DEFAULT_SUSPEND_FREQ 702000
#define DEFAULT_CORES_ON_TOUCH 2

struct cpu_stats
{
    unsigned long time_stamp;
    unsigned int online_cpus;
    unsigned int total_cpus;
    unsigned int default_first_level;
    unsigned int default_second_level;
    unsigned int default_third_level;
    unsigned int default_fourth_level;
    unsigned int suspend_frequency;
    unsigned int cores_on_touch;
};

static struct cpu_stats stats;
static struct workqueue_struct *wq;
static struct delayed_work decide_hotplug;

bool is_touched = false;
unsigned long touch_off_time = 0;
unsigned int load_history[HISTORY_SIZE] = {0};
unsigned int counter = 0;

void is_touching(bool touch, unsigned long time_off)
{
    is_touched = touch;
    touch_off_time = time_off;
}

static void scale_interactive_tunables(unsigned int above_hispeed_delay,
    unsigned int go_hispeed_load, unsigned int timer_rate, 
    unsigned int min_sample_time)
{
    scale_above_hispeed_delay(above_hispeed_delay);
    scale_go_hispeed_load(go_hispeed_load);
    scale_timer_rate(timer_rate);
    scale_min_sample_time(min_sample_time);
}

static void first_level_work_check(unsigned long now)
{
    unsigned int cpu = nr_cpu_ids;
    
    /* lets bail if all cores are online */
    if (stats.online_cpus == stats.total_cpus)
        return;

    for_each_possible_cpu(cpu)
    {
        if (cpu && likely(!cpu_online(cpu)))
        {
            cpu_up(cpu);
            pr_info("Hotplug: cpu%d is up - high load\n", cpu);
        }
    }

    stats.time_stamp = now;
}

static void second_level_work_check(unsigned long now)
{
    unsigned int cpu = nr_cpu_ids;

    /* lets bail if all cores are online */
    if (stats.online_cpus == stats.total_cpus)
        return;

    for_each_possible_cpu(cpu)
    {
        if (cpu && likely(!cpu_online(cpu)))
        {
            cpu_up(cpu);
            pr_info("Hotplug: cpu%d is up - medium load\n", cpu);
            break;
        }
    }

    if (num_online_cpus() == 3) 
        scale_interactive_tunables(0, 80, 10, 80);

    stats.time_stamp = now;
}

static void third_level_work_check(unsigned int load, unsigned long now)
{
    unsigned int cpu = nr_cpu_ids;

    unsigned int third_level = stats.default_third_level * stats.online_cpus;
    unsigned int fourth_level = stats.default_fourth_level * stats.online_cpus;

    if (load <= fourth_level)
    {   
        for_each_online_cpu(cpu)
        {
            if (cpu)
            {
                cpu_down(cpu);
                pr_info("Hotplug: cpu%d is down - low load\n", cpu);
            }
        }
    }

    else if (load <= third_level)
    {
        for_each_online_cpu(cpu)
        {
            if (cpu)
            {
                cpu_down(cpu);
                pr_info("Hotplug: cpu%d is down - low load\n", cpu);
                break;
            }
        }        
    }

    if (likely(num_online_cpus() < 3))
        scale_interactive_tunables(15, 99, 30, 40);

    stats.time_stamp = now;
}

static void decide_hotplug_func(struct work_struct *work)
{
    unsigned long now;
    unsigned int i, j, first_level, second_level, load = 0;
    
    /* start feeding the current load to the history array so that we can
     make a little average. Works good for filtering low and/or high load
     spikes */
    load_history[counter] = report_load_at_max_freq();
        
    for (i = 0, j = counter; i < HISTORY_SIZE; i++, j--) 
    {
        load += load_history[j];

        if (j == 0)
            j = HISTORY_SIZE;
    }
    
    if (++counter == HISTORY_SIZE)
        counter = 0;

    load = load/HISTORY_SIZE;
    /* finish load routines */
        
    /* time of this sampling time */
    now = ktime_to_ms(ktime_get());
    
    stats.online_cpus = num_online_cpus();
    
    /* the load thresholds scale with the number of online cpus */
    first_level = stats.default_first_level * stats.online_cpus;
    second_level = stats.default_second_level * stats.online_cpus;
    
    /*
    pr_info("LOAD: %d\n", load);
    pr_info("FIRST: %d\n", first_level);
    pr_info("SECOND: %d\n", second_level);
    pr_info("THIRD: %d\n", third_level);
    pr_info("COUNTER: %d\n", counter); 
    pr_info("BOOL: %d\n", is_touched);
    */

    if (load >= first_level)
    {
        scale_interactive_tunables(0, 80, 10, 80);
        first_level_work_check(now);
        queue_delayed_work_on(0, wq, &decide_hotplug, msecs_to_jiffies(HZ));
        return;
    }

    /* if the touch driver detects a finger in the screen go down this path */
    else if (is_touched)
    {   
        if (now >= touch_off_time + SEC_THRESHOLD)
        {
            /* only call scale function if dynamic_scaling is true */
            if (likely(get_dynamic_scaling()))
                scale_min_sample_time(40);
            is_touched = false;
        }

        else if (stats.online_cpus < stats.cores_on_touch)
        {
            second_level_work_check(now);
            queue_delayed_work_on(0, wq, &decide_hotplug, msecs_to_jiffies(HZ));           
            return;       
        }
    }
    /* load is medium-high so online only one core at a time */
    else if (load >= second_level)
    {   
        second_level_work_check(now);
        queue_delayed_work_on(0, wq, &decide_hotplug, msecs_to_jiffies(HZ));
        return;
    }

    /* low load obliterate the cpus to death */
    else if (stats.online_cpus > 1 && (now - stats.time_stamp) >= SEC_THRESHOLD)
    {
        third_level_work_check(load, now);
    }
    
    queue_delayed_work_on(0, wq, &decide_hotplug, msecs_to_jiffies(HZ));
}

static void mako_hotplug_early_suspend(struct early_suspend *handler)
{	 
    /* cancel the hotplug work when the screen is off and flush the WQ */
    cancel_delayed_work_sync(&decide_hotplug);
    flush_workqueue(wq);
    pr_info("Early Suspend stopping Hotplug work...\n");
    
    third_level_work_check(0, ktime_to_ms(ktime_get()));
    
    /* cap max frequency to 702MHz by default */
    msm_cpufreq_set_freq_limits(0, MSM_CPUFREQ_NO_LIMIT, 
            stats.suspend_frequency);
    pr_info("Cpulimit: Early suspend - limit cpu%d max frequency to: %dMHz\n",
            0, stats.suspend_frequency/1000);
}

static void mako_hotplug_late_resume(struct early_suspend *handler)
{    
    /* online all cores when the screen goes online */
    first_level_work_check(ktime_to_ms(ktime_get()));

    /* restore max frequency */
    msm_cpufreq_set_freq_limits(0, MSM_CPUFREQ_NO_LIMIT, MSM_CPUFREQ_NO_LIMIT);
    pr_info("Cpulimit: Late resume - restore cpu%d max frequency.\n", 0);
    
    pr_info("Late Resume starting Hotplug work...\n");
    queue_delayed_work_on(0, wq, &decide_hotplug, HZ);
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
    stats.default_first_level = level;
}

void update_second_level(unsigned int level)
{
    stats.default_second_level = level;
}

void update_third_level(unsigned int level)
{
    stats.default_third_level = level;
}

void update_fourth_level(unsigned int level)
{
    stats.default_fourth_level = level;
}

void update_suspend_frequency(unsigned int freq)
{
    stats.suspend_frequency = freq;
}

void update_cores_on_touch(unsigned int num)
{
    stats.cores_on_touch = num;
}

unsigned int get_first_level()
{
    return stats.default_first_level;
}

unsigned int get_second_level()
{
    return stats.default_second_level;
}

unsigned int get_third_level()
{
    return stats.default_third_level;
}

unsigned int get_fourth_level()
{
    return stats.default_fourth_level;
}

unsigned int get_suspend_frequency()
{
    return stats.suspend_frequency;
}

unsigned int get_cores_on_touch()
{
    return stats.cores_on_touch;
}
/* end sysfs functions from external driver */

int __init mako_hotplug_init(void)
{
	pr_info("Mako Hotplug driver started.\n");
    
    /* init everything here */
    stats.time_stamp = 0;
    stats.online_cpus = num_online_cpus();
    stats.total_cpus = num_present_cpus();
    stats.default_first_level = DEFAULT_FIRST_LEVEL;
    stats.default_second_level = DEFAULT_SECOND_LEVEL;
    stats.default_third_level = DEFAULT_THIRD_LEVEL;
    stats.default_fourth_level = DEFAULT_FOURTH_LEVEL;
    stats.suspend_frequency = DEFAULT_SUSPEND_FREQ;
    stats.cores_on_touch = DEFAULT_CORES_ON_TOUCH;
    
    wq = alloc_workqueue("mako_hotplug_workqueue",
                         WQ_UNBOUND | WQ_RESCUER | WQ_FREEZABLE, 1);
    
    if (!wq)
        return -ENOMEM;
    
    INIT_DELAYED_WORK(&decide_hotplug, decide_hotplug_func);
    queue_delayed_work_on(0, wq, &decide_hotplug, HZ*25);
    
    register_early_suspend(&mako_hotplug_suspend);
    
    return 0;
}
late_initcall(mako_hotplug_init);
