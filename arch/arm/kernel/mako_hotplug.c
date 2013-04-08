/*
 *  Copyright (c) 2013, Francisco Franco <franciscofranco.1990@gmail.com>. 
 *  All rights reserved.
 *
 *  Simple no bullshit hot[in]plug driver for SMP
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/cpu.h>
#include <linux/workqueue.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/earlysuspend.h>
#include <mach/cpufreq.h>

/* threshold for comparing time diffs is 2 seconds */
#define SEC_THRESHOLD 2000

#define DEFAULT_FIRST_LEVEL 90
unsigned int default_first_level;

#define DEFAULT_SECOND_LEVEL 25
unsigned int default_second_level;

#define DEFAULT_THIRD_LEVEL 50
unsigned int default_third_level;

#define DEFAULT_SUSPEND_FREQ 702000
unsigned int suspend_freq;

/* this comes from msm_rq_stats */
unsigned int report_load_at_max_freq(void);

/*
 * TODO probably populate the struct with more relevant data
 */
struct cpu_stats
{
    /* variable to be accessed to filter spurious load spikes */
    unsigned long time_stamp;

    unsigned int online_cpus;

    unsigned int total_cpus;
};

static struct cpu_stats stats;

static struct workqueue_struct *wq;

static struct delayed_work decide_hotplug;

static void first_level_work_check(unsigned long temp_diff, unsigned long now)
{
    unsigned int cpu = nr_cpu_ids;
    
    if ((now - stats.time_stamp) >= temp_diff)
    {
        for_each_possible_cpu(cpu)
        {
            if (cpu)
            {
                if (!cpu_online(cpu))
                {
                    cpu_up(cpu);
                    pr_info("Hotplug: cpu%d is up\n", cpu);
                }
            }
        }
        
        /*
         * new current time for comparison in the next load check
         * we don't want too many hot[in]plugs in small time span
         */
        stats.time_stamp = now;
    }
}

static void second_level_work_check(unsigned long temp_diff, unsigned long now)
{
    unsigned int cpu = nr_cpu_ids;
    
    if ((now - stats.time_stamp) >= temp_diff)
    {
        for_each_possible_cpu(cpu)
        {
            if (cpu)
            {
                if (!cpu_online(cpu))
                {
                    cpu_up(cpu);
                    pr_info("Hotplug: cpu%d is up\n", cpu);
                    break;
                }
            }
        }
        
        stats.time_stamp = now;
    }
}

static void third_level_work_check(unsigned long temp_diff, unsigned long now)
{
    unsigned int cpu = nr_cpu_ids;
    
    if ((now - stats.time_stamp) >= temp_diff)
    {
        for_each_possible_cpu(cpu)
        {
            if (cpu)
            {
                if (cpu_online(cpu))
                {
                    cpu_down(cpu);
                    pr_info("Hotplug: cpu%d is down\n", cpu);
                }
            }
        }
        
        stats.time_stamp = now;
    }
}

static void __cpuinit decide_hotplug_func(struct work_struct *work)
{
    unsigned long now, temp_diff, sampling_timer;
    unsigned int load, first_level, second_level, third_level;
        
    /* load polled in this sampling time */
    load = report_load_at_max_freq();
    
    /* time of this sampling time */
    now = ktime_to_ms(ktime_get());
    
    /* the load thresholds scale with the number of online cpus */
    first_level = default_first_level * stats.online_cpus;
    second_level = default_second_level * stats.online_cpus;
    third_level = default_third_level * stats.online_cpus;
    
    /* init temp_diff for the allowance of hotplug or not */
    temp_diff = SEC_THRESHOLD/stats.online_cpus;
    
    /* jiffies count for how often the decision work is called */
    sampling_timer = HZ/stats.online_cpus;
        
    if (load >= first_level && stats.online_cpus < stats.total_cpus)
    {
        first_level_work_check(temp_diff, now);
        queue_delayed_work_on(0, wq, &decide_hotplug, sampling_timer);
        return;
    }
    
    /* load is medium-high so online only one core at a time */
    else if (load >= second_level && stats.online_cpus < stats.total_cpus)
    {
        second_level_work_check(temp_diff, now);
        queue_delayed_work_on(0, wq, &decide_hotplug, sampling_timer);
        return;
    }
    
    /* low load obliterate the cpus to death */
    else if (load <= third_level && stats.online_cpus > 1)
    {
        third_level_work_check(temp_diff, now);
    }
        
    stats.online_cpus = num_online_cpus();

    queue_delayed_work_on(0, wq, &decide_hotplug, sampling_timer);
}

static void mako_hotplug_early_suspend(struct early_suspend *handler)
{
    unsigned int cpu = nr_cpu_ids;
	 
    /* cancel the hotplug work when the screen is off and flush the WQ */
    flush_workqueue(wq);
    cancel_delayed_work_sync(&decide_hotplug);
    pr_info("Early Suspend stopping Hotplug work...");
    
    if (num_online_cpus() > 1)
    {
        for_each_possible_cpu(cpu)
        {
            if (cpu)
            {
                if (cpu_online(cpu))
                {
                    cpu_down(cpu);
                    pr_info("Early Suspend Hotplug: cpu%d is down\n", cpu);
                }
            }
        }
	}
    
    /* cap max frequency to 702MHz by default */
    msm_cpufreq_set_freq_limits(0, MSM_CPUFREQ_NO_LIMIT, suspend_freq);
    pr_info("Cpulimit: Early suspend - limit cpu%d max frequency to: %dMHz\n",
            0, suspend_freq/1000);
    
    stats.online_cpus = num_online_cpus();
}

static void __cpuinit mako_hotplug_late_resume(struct early_suspend *handler)
{
    unsigned int cpu = nr_cpu_ids;
    
    /* online all cores when the screen goes online */
    for_each_possible_cpu(cpu)
    {
        if (cpu)
        {
            if (!cpu_online(cpu))
            {
                cpu_up(cpu);
                pr_info("Late Resume Hotplug: cpu%d is up\n", cpu);
            }
        }
    }
    
    /* restore default 1,5GHz max frequency */
    msm_cpufreq_set_freq_limits(0, MSM_CPUFREQ_NO_LIMIT, MSM_CPUFREQ_NO_LIMIT);
    pr_info("Cpulimit: Late resume - restore cpu%d max frequency.\n", 0);
    
    /* new time_stamp because all cpus were just onlined */
    stats.time_stamp = ktime_to_ms(ktime_get());
    
    pr_info("Late Resume starting Hotplug work...\n");
    queue_delayed_work_on(0, wq, &decide_hotplug, HZ);
}

static struct early_suspend mako_hotplug_suspend =
{
	.suspend = mako_hotplug_early_suspend,
	.resume = mako_hotplug_late_resume,
};

/* these come from the sysfs driver that exports the thresholds to userspace */
void update_first_level(unsigned int level)
{
    default_first_level = level;
}

void update_second_level(unsigned int level)
{
    default_second_level = level;
}

void update_third_level(unsigned int level)
{
    default_third_level = level;
}

void update_suspend_freq(unsigned int freq)
{
    suspend_freq = freq;
}
/* end sysfs functions from external driver */

int __init mako_hotplug_init(void)
{
	pr_info("Mako Hotplug driver started.\n");
    
    /* init everything here */
    stats.time_stamp = 0;
    stats.online_cpus = num_online_cpus();
    stats.total_cpus = num_present_cpus();
    default_first_level = DEFAULT_FIRST_LEVEL;
    default_second_level = DEFAULT_SECOND_LEVEL;
    default_third_level = DEFAULT_THIRD_LEVEL;
    suspend_freq = DEFAULT_SUSPEND_FREQ;
    
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
