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
#include <linux/rq_stats.h>

/* threshold for comparing time diffs is 2 seconds */
#define SEC_THRESHOLD 2000
#define HISTORY_SIZE 10
#define DEFAULT_FIRST_LEVEL 90
#define DEFAULT_SECOND_LEVEL 25
#define DEFAULT_THIRD_LEVEL 50
#define DEFAULT_SUSPEND_FREQ 702000

/*
 * TODO probably populate the struct with more relevant data
 */
struct cpu_stats
{
    /* variable to be accessed to filter spurious load spikes */
    unsigned long time_stamp;
    unsigned int online_cpus;
    unsigned int total_cpus;
    unsigned int default_first_level;
    unsigned int default_second_level;
    unsigned int default_third_level;
    unsigned int suspend_frequency;
};

static struct cpu_stats stats;

static struct workqueue_struct *wq;

static struct delayed_work decide_hotplug;

unsigned int load_history[HISTORY_SIZE] = {0};
unsigned int counter = 0;

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
                    pr_info("Hotplug: cpu%d is up - high load\n", cpu);
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
    
    if (stats.online_cpus < 2 || (now - stats.time_stamp) >= temp_diff)
    {
        for_each_possible_cpu(cpu)
        {
            if (cpu)
            {
                if (!cpu_online(cpu))
                {
                    cpu_up(cpu);
                    pr_info("Hotplug: cpu%d is up - medium load\n", cpu);
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
        for_each_online_cpu(cpu)
        {
            if (cpu)
            {
                cpu_down(cpu);
                pr_info("Hotplug: cpu%d is down - low load\n", cpu);
            }
        }
        
        stats.time_stamp = now;
    }
}

static void decide_hotplug_func(struct work_struct *work)
{
    unsigned long now;
    unsigned int i, j, first_level, second_level, third_level, load = 0;
    
    /* start feeding the current load to the history array so that we can
     make a little average. Works good for filtering low and/or high load
     spikes */
    load_history[counter] = report_load_at_max_freq();
        
    for (i = 0, j = counter; i < HISTORY_SIZE; i++, j--) {
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
    third_level = stats.default_third_level * stats.online_cpus;
    
    /*
    pr_info("LOAD: %d\n", load);
    pr_info("FIRST: %d\n", first_level);
    pr_info("SECOND: %d\n", second_level);
    pr_info("THIRD: %d\n", third_level);
    pr_info("COUNTER: %d\n", counter); 
    */
    
    if (load >= first_level)
    {
        first_level_work_check(SEC_THRESHOLD, now);
        queue_delayed_work_on(0, wq, &decide_hotplug, msecs_to_jiffies(HZ));
        return;
    }
    
    /* load is medium-high so online only one core at a time */
    else if (load >= second_level)
    {
        /* feed it 2 times the seconds threshold because when this is called
           there is a check inside that onlines cpu1 bypassing the time_diff
           but afterwards it takes at least 4 seconds as threshold before
           onlining another cpu. This eliminates unneeded onlining when we are
           for example swipping between home or app drawer and we only need
           cpu0 and cpu1 online for that, cpufreq takes care of the rest */
        
        second_level_work_check(SEC_THRESHOLD*2, now);
        queue_delayed_work_on(0, wq, &decide_hotplug, msecs_to_jiffies(HZ));
        return;
    }
    
    /* low load obliterate the cpus to death */
    else if (load <= third_level && stats.online_cpus > 1)
    {
        third_level_work_check(SEC_THRESHOLD, now);
    }
    
    queue_delayed_work_on(0, wq, &decide_hotplug, msecs_to_jiffies(HZ));
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
        for_each_online_cpu(cpu)
        {
            if (cpu)
            {
                cpu_down(cpu);
                pr_info("Early Suspend Hotplug: cpu%d is down\n", cpu);
            }
        }
	}
    
    /* cap max frequency to 702MHz by default */
    msm_cpufreq_set_freq_limits(0, MSM_CPUFREQ_NO_LIMIT, stats.suspend_frequency);
    pr_info("Cpulimit: Early suspend - limit cpu%d max frequency to: %dMHz\n",
            0, stats.suspend_frequency/1000);
    
    stats.online_cpus = num_online_cpus();
}

static void mako_hotplug_late_resume(struct early_suspend *handler)
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
    
    /* new time_stamp and online_cpu because all cpus were just onlined */
    stats.time_stamp = ktime_to_ms(ktime_get());
    stats.online_cpus = num_online_cpus();
    
    pr_info("Late Resume starting Hotplug work...\n");
    queue_delayed_work_on(0, wq, &decide_hotplug, HZ);
}

static struct early_suspend mako_hotplug_suspend =
{
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

void update_suspend_frequency(unsigned int freq)
{
    stats.suspend_frequency = freq;
}

inline unsigned int get_first_level(void)
{
    return stats.default_first_level;
}

inline unsigned int get_second_level(void)
{
    return stats.default_second_level;
}

inline unsigned int get_third_level(void)
{
    return stats.default_third_level;
}

inline unsigned int get_suspend_frequency(void)
{
    return stats.suspend_frequency;
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
    stats.suspend_frequency = DEFAULT_SUSPEND_FREQ;
    
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
