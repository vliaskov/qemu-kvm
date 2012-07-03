#ifndef QEMU_CPUS_H
#define QEMU_CPUS_H

/* cpus.c */
void qemu_init_cpu_loop(void);
void resume_all_vcpus(void);
void pause_all_vcpus(void);
void cpu_stop_current(void);
void replug_vcpu(void *p);
void unplug_vcpu(void *p);

void cpu_synchronize_all_states(void);
void cpu_synchronize_all_post_reset(void);
void cpu_synchronize_all_post_init(void);

/* vl.c */
extern int smp_cores;
extern int smp_threads;
void set_numa_modes(void);
void set_cpu_log(const char *optarg);
void set_cpu_log_filename(const char *optarg);
void list_cpus(FILE *f, fprintf_function cpu_fprintf, const char *optarg);

typedef enum {
    CPU_REMOVESUCCESS_NOTIFY = 0,
    CPU_REMOVEFAIL_NOTIFY = 1,
    CPU_ADDSUCCESS_NOTIFY = 2,
    CPU_ADDFAIL_NOTIFY = 3
} cpu_hp_result_code;

struct cpu_hp_result {
    int64_t cpu;
    cpu_hp_result_code ret;
    QLIST_ENTRY (cpu_hp_result) next;
};

void cpu_ost_notify(int cpu_index, uint32_t event);

#endif
