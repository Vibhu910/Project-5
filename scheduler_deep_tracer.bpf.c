/* bpftool-generated vmlinux.h contains standalone forward decls that trigger
 * -Wmissing-declarations; suppress only for this include.
 */
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-declarations"
#endif
#include "vmlinux.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>

/*
 * cfs_rq CO-RE: On many Ubuntu/cloud kernels, libbpf fails to resolve
 * relocations for cfs_rq fields (e.g. min_vruntime) even when vmlinux.h
 * compiles — BTF may omit or mismatch scheduler internals. Single-field
 * "flavor" structs made load fail with:
 *   failed to resolve CO-RE relocation ... struct cfs_rq___co_min.min_vruntime
 * We therefore do not BPF_CORE_READ cfs_rq members; cfs_* and rq_* stats
 * stay zero. sched_entity fields above (vruntime, load, etc.) still work.
 * Regenerate vmlinux.h on the running machine: make clean && make
 */

#define TASK_COMM_LEN 16
#define FILENAME_LEN  64

#define EVT_SWITCH      1
#define EVT_WAKEUP      2
#define EVT_WAKEUP_NEW  3
#define EVT_MIGRATE     4
#define EVT_FORK        5
#define EVT_EXIT        6
#define EVT_EXEC        7
#define EVT_WAIT        8
#define EVT_WQ_ENQUEUE  9
#define EVT_WQ_SUMMARY  10

struct sched_event {
    __u64  timestamp_ns;
    __u32  event_type;
    __s32  cpu;

    __u32  pid;
    __u32  tgid;
    char   comm[TASK_COMM_LEN];
    __s32  prio;
    __s32  static_prio;
    __s32  normal_prio;

    __u64  vruntime;
    __u64  exec_start;
    __u64  sum_exec_runtime;
    __u64  prev_sum_exec_runtime;
    __s64  vlag;
    __u64  slice;

    __u64  load_weight;
    __u32  load_inv_weight;

    __u64  cfs_min_vruntime;
    __u64  cfs_avg_vruntime;
    __u32  cfs_nr_running;
    __u32  cfs_h_nr_running;
    __u64  cfs_exec_clock;
    __u64  cfs_load_weight;

    __u8   on_rq;
    __u64  last_update_rq_clock;

    __u64  wait_queue_head_addr;
    __u32  wait_queue_flags;
    __u8   wait_queue_type;
    __u32  wait_queue_size;

    __u32  prev_pid;
    __u32  prev_tgid;
    char   prev_comm[TASK_COMM_LEN];
    __s32  prev_prio;
    __u64  prev_vruntime;
    __u64  prev_sum_exec;
    __u64  prev_load_weight;

    __s32  orig_cpu;
    __s32  dest_cpu;

    __u32  child_pid;
    char   filename[FILENAME_LEN];

    __u32  task_state;

    __u8   preempt;
    __u32  policy;
    __s32  nice;
    __u32  prev_task_state;
    __u32  rq_nr_running;
    __u64  rq_clock;
    __s32  wake_cpu;
};

/* Ringbuf size in bytes: must be power of 2 and page-aligned (kernel EINVAL). */
struct {
    __uint(type,        BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 1U << 24); /* 16 MiB */
} events SEC(".maps");

struct wait_queue_info {
    __u64  wait_queue_head_addr;
    __u8   wait_queue_type;
    __u32  wait_queue_flags;
    __u64  timestamp_ns;
    __u32  pid;
    __u32  tgid;
    char   comm[TASK_COMM_LEN];
};
struct {
    __uint(type,        BPF_MAP_TYPE_HASH);
    __uint(max_entries, 4096);
    __type(key,   __u32);
    __type(value, struct wait_queue_info);
} wait_queue_map SEC(".maps");

struct {
    __uint(type,        BPF_MAP_TYPE_HASH);
    __uint(max_entries, 4096);
    __type(key,   __u64);
    __type(value, __u32);
} wait_queue_size_map SEC(".maps");

/* Populates a sched_event with CFS sched_entity and cfs_rq fields from a task_struct. */
static __always_inline void fill_se(struct sched_event *e,
                                    struct task_struct *t)
{
    e->pid         = BPF_CORE_READ(t, pid);
    e->tgid        = BPF_CORE_READ(t, tgid);
    e->prio        = BPF_CORE_READ(t, prio);
    e->static_prio = BPF_CORE_READ(t, static_prio);
    e->normal_prio = BPF_CORE_READ(t, normal_prio);
    e->policy      = BPF_CORE_READ(t, policy);
    e->nice        = (__s32)BPF_CORE_READ(t, static_prio) - 120;
    e->wake_cpu    = -1;
    bpf_core_read_str(e->comm, sizeof(e->comm), &t->comm);

    e->vruntime              = BPF_CORE_READ(t, se.vruntime);
    e->exec_start            = BPF_CORE_READ(t, se.exec_start);
    e->sum_exec_runtime      = BPF_CORE_READ(t, se.sum_exec_runtime);
    e->prev_sum_exec_runtime = BPF_CORE_READ(t, se.prev_sum_exec_runtime);
    e->load_weight           = BPF_CORE_READ(t, se.load.weight);
    e->load_inv_weight       = BPF_CORE_READ(t, se.load.inv_weight);

    e->vlag  = BPF_CORE_READ(t, se.vlag);
    e->slice = BPF_CORE_READ(t, se.slice);

    e->on_rq = BPF_CORE_READ(t, se.on_rq);
    
    e->last_update_rq_clock = 0;

    e->task_state = BPF_CORE_READ(t, __state);
}

/* Populates the prev_* fields in a sched_event from a task_struct. */
static __always_inline void fill_prev_se(struct sched_event *e,
                                         struct task_struct *t)
{
    e->prev_pid          = BPF_CORE_READ(t, pid);
    e->prev_tgid         = BPF_CORE_READ(t, tgid);
    e->prev_prio         = BPF_CORE_READ(t, prio);
    e->prev_vruntime     = BPF_CORE_READ(t, se.vruntime);
    e->prev_sum_exec     = BPF_CORE_READ(t, se.sum_exec_runtime);
    e->prev_load_weight  = BPF_CORE_READ(t, se.load.weight);
    e->prev_task_state   = BPF_CORE_READ(t, __state);
    bpf_core_read_str(e->prev_comm, sizeof(e->prev_comm), &t->comm);
}

/* rq stats were via cfs_rq->rq; omitted when cfs_rq CO-RE is unreliable (see hdr). */
static __always_inline void fill_rq(struct sched_event *e,
                                    struct task_struct *t)
{
    (void)t;
    e->rq_nr_running = 0;
    e->rq_clock = 0;
}

/* Copies current task's pid/tgid/comm into a wait_queue_info. */
static __always_inline void fill_wq_info_task(struct wait_queue_info *wq_info)
{
    struct task_struct *t = (struct task_struct *)bpf_get_current_task();
    if (t) {
        wq_info->pid  = BPF_CORE_READ(t, pid);
        wq_info->tgid = BPF_CORE_READ(t, tgid);
        bpf_core_read_str(wq_info->comm, sizeof(wq_info->comm), &t->comm);
    }
}

/* Increments or decrements the per-queue task count in wait_queue_size_map. */
static __always_inline void update_wq_size(__u64 wq_addr, int delta)
{
    if (wq_addr == 0) return;
    
    __u32 *count = bpf_map_lookup_elem(&wait_queue_size_map, &wq_addr);
    if (count) {
        __u32 new_count = *count + delta;
        if (new_count == 0) {
            bpf_map_delete_elem(&wait_queue_size_map, &wq_addr);
        } else {
            *count = new_count;
        }
    } else if (delta > 0) {
        __u32 new_count = delta;
        bpf_map_update_elem(&wait_queue_size_map, &wq_addr, &new_count, BPF_ANY);
    }
}

/* bpf_for_each_map_elem callback: emits one EVT_WQ_SUMMARY event per waiting task. */
static long emit_wq_summary_callback(struct bpf_map *map, const void *key, void *value, void *ctx)
{
    __u32 *tgid_ptr = (__u32 *)key;
    struct wait_queue_info *wq_info = (struct wait_queue_info *)value;
    
    __u32 wq_size = 0;
    if (wq_info->wait_queue_head_addr != 0) {
        __u32 *count = bpf_map_lookup_elem(&wait_queue_size_map, &wq_info->wait_queue_head_addr);
        if (count) {
            wq_size = *count;
        }
    }
    
    struct sched_event *e = bpf_ringbuf_reserve(&events, sizeof(*e), 0);
    if (!e) return 0;
    __builtin_memset(e, 0, sizeof(*e));
    e->timestamp_ns = bpf_ktime_get_ns();
    e->event_type   = EVT_WQ_SUMMARY;
    e->cpu          = bpf_get_smp_processor_id();
    e->pid          = wq_info->pid;
    e->tgid         = wq_info->tgid;
    e->wait_queue_head_addr = wq_info->wait_queue_head_addr;
    e->wait_queue_type      = wq_info->wait_queue_type;
    e->wait_queue_flags     = wq_info->wait_queue_flags;
    e->wait_queue_size      = wq_size;
    __builtin_memcpy(e->comm, wq_info->comm, sizeof(e->comm));
    bpf_ringbuf_submit(e, 0);
    return 0;
}

/* Iterates wait_queue_map and emits one EVT_WQ_SUMMARY event per entry. */
static __always_inline void emit_all_wait_queues(void)
{
    bpf_for_each_map_elem(&wait_queue_map, emit_wq_summary_callback, NULL, 0);
}

/* Emits an EVT_WQ_ENQUEUE event whenever a wait queue entry is recorded. */
static __always_inline void emit_wq_enqueue(__u64 wq_addr, __u8 wq_type, __u32 wq_flags)
{
    struct sched_event *e = bpf_ringbuf_reserve(&events, sizeof(*e), 0);
    if (!e) return;
    __builtin_memset(e, 0, sizeof(*e));
    e->timestamp_ns = bpf_ktime_get_ns();
    e->event_type   = EVT_WQ_ENQUEUE;
    e->cpu          = bpf_get_smp_processor_id();
    e->wait_queue_head_addr = wq_addr;
    e->wait_queue_type      = wq_type;
    e->wait_queue_flags     = wq_flags;
    e->pid  = bpf_get_current_pid_tgid() >> 32;
    e->tgid = bpf_get_current_pid_tgid() & 0xFFFFFFFF;
    struct task_struct *t = (struct task_struct *)bpf_get_current_task();
    if (t)
        bpf_probe_read_kernel_str(e->comm, sizeof(e->comm), t->comm);
    bpf_ringbuf_submit(e, 0);
}

SEC("tp_btf/sched_switch")
int BPF_PROG(handle_sched_switch,
             bool preempt,
             struct task_struct *prev,
             struct task_struct *next)
{
    struct sched_event *e = bpf_ringbuf_reserve(&events, sizeof(*e), 0);
    if (!e) return 0;

    __builtin_memset(e, 0, sizeof(*e));
    e->timestamp_ns = bpf_ktime_get_ns();
    e->event_type   = EVT_SWITCH;
    e->cpu          = bpf_get_smp_processor_id();
    e->preempt      = preempt ? 1 : 0;

    fill_se(e, next);
    fill_rq(e, next);

    fill_prev_se(e, prev);

    bpf_ringbuf_submit(e, 0);
    return 0;
}

SEC("tp_btf/sched_wakeup")
int BPF_PROG(handle_wakeup, struct task_struct *p)
{
    struct sched_event *e = bpf_ringbuf_reserve(&events, sizeof(*e), 0);
    if (!e) return 0;

    __builtin_memset(e, 0, sizeof(*e));
    e->timestamp_ns = bpf_ktime_get_ns();
    e->event_type   = EVT_WAKEUP;
    e->cpu          = bpf_get_smp_processor_id();

    fill_se(e, p);
    fill_rq(e, p);
    e->wake_cpu     = BPF_CORE_READ(p, wake_cpu);

    bpf_ringbuf_submit(e, 0);
    return 0;
}

SEC("tp_btf/sched_wakeup_new")
int BPF_PROG(handle_wakeup_new, struct task_struct *p)
{
    struct sched_event *e = bpf_ringbuf_reserve(&events, sizeof(*e), 0);
    if (!e) return 0;

    __builtin_memset(e, 0, sizeof(*e));
    e->timestamp_ns = bpf_ktime_get_ns();
    e->event_type   = EVT_WAKEUP_NEW;
    e->cpu          = bpf_get_smp_processor_id();

    fill_se(e, p);
    fill_rq(e, p);
    e->wake_cpu     = BPF_CORE_READ(p, wake_cpu);

    bpf_ringbuf_submit(e, 0);
    return 0;
}

SEC("tp_btf/sched_migrate_task")
int BPF_PROG(handle_migrate,
             struct task_struct *p,
             int dest_cpu)
{
    struct sched_event *e = bpf_ringbuf_reserve(&events, sizeof(*e), 0);
    if (!e) return 0;

    __builtin_memset(e, 0, sizeof(*e));
    e->timestamp_ns = bpf_ktime_get_ns();
    e->event_type   = EVT_MIGRATE;
    e->cpu          = bpf_get_smp_processor_id();
    e->orig_cpu     = bpf_get_smp_processor_id();
    e->dest_cpu     = dest_cpu;

    fill_se(e, p);
    fill_rq(e, p);

    bpf_ringbuf_submit(e, 0);
    return 0;
}

SEC("tp_btf/sched_process_fork")
int BPF_PROG(handle_fork,
             struct task_struct *parent,
             struct task_struct *child)
{
    struct sched_event *e = bpf_ringbuf_reserve(&events, sizeof(*e), 0);
    if (!e) return 0;

    __builtin_memset(e, 0, sizeof(*e));
    e->timestamp_ns = bpf_ktime_get_ns();
    e->event_type   = EVT_FORK;
    e->cpu          = bpf_get_smp_processor_id();

    fill_se(e, child);
    fill_rq(e, child);
    fill_prev_se(e, parent);
    e->child_pid = BPF_CORE_READ(child, pid);

    bpf_ringbuf_submit(e, 0);
    return 0;
}

SEC("tp_btf/sched_process_exit")
int BPF_PROG(handle_exit, struct task_struct *p)
{
    struct sched_event *e = bpf_ringbuf_reserve(&events, sizeof(*e), 0);
    if (!e) return 0;

    __builtin_memset(e, 0, sizeof(*e));
    e->timestamp_ns = bpf_ktime_get_ns();
    e->event_type   = EVT_EXIT;
    e->cpu          = bpf_get_smp_processor_id();

    fill_se(e, p);
    fill_rq(e, p);

    __u32 tgid = BPF_CORE_READ(p, tgid);
    struct wait_queue_info *wq_info = bpf_map_lookup_elem(&wait_queue_map, &tgid);
    if (wq_info) {
        update_wq_size(wq_info->wait_queue_head_addr, -1);
        bpf_map_delete_elem(&wait_queue_map, &tgid);
    }

    bpf_ringbuf_submit(e, 0);
    return 0;
}

SEC("tp_btf/sched_process_exec")
int BPF_PROG(handle_exec,
             struct task_struct *p,
             pid_t old_pid,
             struct linux_binprm *bprm)
{
    struct sched_event *e = bpf_ringbuf_reserve(&events, sizeof(*e), 0);
    if (!e) return 0;

    __builtin_memset(e, 0, sizeof(*e));
    e->timestamp_ns = bpf_ktime_get_ns();
    e->event_type   = EVT_EXEC;
    e->cpu          = bpf_get_smp_processor_id();

    fill_se(e, p);
    fill_rq(e, p);

    if (bprm)
        bpf_core_read_str(e->filename, sizeof(e->filename),
                          &bprm->filename);

    bpf_ringbuf_submit(e, 0);
    return 0;
}

SEC("kprobe/dequeue_task_fair")
int BPF_KPROBE(handle_dequeue, struct rq *rq, struct task_struct *p, int flags)
{
    unsigned int state = BPF_CORE_READ(p, __state);
    if (state == 0) return 0;

    struct sched_event *e = bpf_ringbuf_reserve(&events, sizeof(*e), 0);
    if (!e) return 0;

    __builtin_memset(e, 0, sizeof(*e));
    e->timestamp_ns = bpf_ktime_get_ns();
    e->event_type   = EVT_WAIT;
    e->cpu          = bpf_get_smp_processor_id();

    fill_se(e, p);
    e->rq_nr_running = BPF_CORE_READ(rq, nr_running);
    e->rq_clock      = BPF_CORE_READ(rq, clock);

    __u32 tgid = BPF_CORE_READ(p, tgid);
    struct wait_queue_info *wq_info = bpf_map_lookup_elem(&wait_queue_map, &tgid);
    if (wq_info) {
        e->wait_queue_head_addr = wq_info->wait_queue_head_addr;
        e->wait_queue_type      = wq_info->wait_queue_type;
        e->wait_queue_flags     = wq_info->wait_queue_flags;
    } else {
        e->wait_queue_type = 5;
    }

    bpf_ringbuf_submit(e, 0);
    emit_all_wait_queues();
    
    if (wq_info) {
        update_wq_size(wq_info->wait_queue_head_addr, -1);
        bpf_map_delete_elem(&wait_queue_map, &tgid);
    }
    return 0;
}

SEC("kprobe/prepare_to_wait")
int BPF_KPROBE(handle_prepare_to_wait,
               struct wait_queue_head *wq,
               struct wait_queue_entry *wq_entry,
               int state)
{
    __u32 tgid = bpf_get_current_pid_tgid() & 0xFFFFFFFF;
    
    struct wait_queue_info *existing = bpf_map_lookup_elem(&wait_queue_map, &tgid);
    int is_new = (existing == NULL);
    
    struct wait_queue_info wq_info = {};
    wq_info.wait_queue_head_addr = (__u64)wq;
    wq_info.wait_queue_type = 0;
    wq_info.wait_queue_flags = 0;
    if (wq_entry)
        wq_info.wait_queue_flags = (__u32)BPF_CORE_READ(wq_entry, flags);
    wq_info.timestamp_ns = bpf_ktime_get_ns();
    fill_wq_info_task(&wq_info);
    
    bpf_map_update_elem(&wait_queue_map, &tgid, &wq_info, BPF_ANY);
    if (is_new) {
        update_wq_size(wq_info.wait_queue_head_addr, 1);
    }
    emit_wq_enqueue(wq_info.wait_queue_head_addr, wq_info.wait_queue_type, wq_info.wait_queue_flags);
    return 0;
}

SEC("kprobe/prepare_to_wait_exclusive")
int BPF_KPROBE(handle_prepare_to_wait_exclusive,
               struct wait_queue_head *wq,
               struct wait_queue_entry *wq_entry,
               int state)
{
    __u32 tgid = bpf_get_current_pid_tgid() & 0xFFFFFFFF;
    struct wait_queue_info *existing = bpf_map_lookup_elem(&wait_queue_map, &tgid);
    int is_new = (existing == NULL);
    
    struct wait_queue_info wq_info = {};
    wq_info.wait_queue_head_addr = (__u64)wq;
    wq_info.wait_queue_type = 0;
    wq_info.wait_queue_flags = 0x1;
    if (wq_entry)
        wq_info.wait_queue_flags = (__u32)BPF_CORE_READ(wq_entry, flags);
    wq_info.timestamp_ns = bpf_ktime_get_ns();
    fill_wq_info_task(&wq_info);
    bpf_map_update_elem(&wait_queue_map, &tgid, &wq_info, BPF_ANY);
    if (is_new) {
        update_wq_size(wq_info.wait_queue_head_addr, 1);
    }
    emit_wq_enqueue(wq_info.wait_queue_head_addr, wq_info.wait_queue_type, wq_info.wait_queue_flags);
    return 0;
}

SEC("kprobe/__mutex_lock_slowpath")
int BPF_KPROBE(handle_mutex_lock_slowpath, void *lock)
{
    __u32 tgid = bpf_get_current_pid_tgid() & 0xFFFFFFFF;
    struct wait_queue_info *wq_info = bpf_map_lookup_elem(&wait_queue_map, &tgid);
    if (wq_info) {
        wq_info->wait_queue_type = 2;
        fill_wq_info_task(wq_info);
        bpf_map_update_elem(&wait_queue_map, &tgid, wq_info, BPF_ANY);
        emit_wq_enqueue(wq_info->wait_queue_head_addr, wq_info->wait_queue_type, wq_info->wait_queue_flags);
    } else {
        struct wait_queue_info wq_info_new = {};
        wq_info_new.wait_queue_head_addr = (__u64)lock;
        wq_info_new.wait_queue_type = 2;
        wq_info_new.wait_queue_flags = 0;
        wq_info_new.timestamp_ns = bpf_ktime_get_ns();
        fill_wq_info_task(&wq_info_new);
        bpf_map_update_elem(&wait_queue_map, &tgid, &wq_info_new, BPF_ANY);
        update_wq_size(wq_info_new.wait_queue_head_addr, 1);
        emit_wq_enqueue(wq_info_new.wait_queue_head_addr, wq_info_new.wait_queue_type, wq_info_new.wait_queue_flags);
    }
    return 0;
}

SEC("kprobe/__wait_on_bit")
int BPF_KPROBE(handle_wait_on_bit,
               void *word,
               int bit,
               unsigned mode,
               unsigned timeout)
{
    __u32 tgid = bpf_get_current_pid_tgid() & 0xFFFFFFFF;
    struct wait_queue_info *wq_info = bpf_map_lookup_elem(&wait_queue_map, &tgid);
    if (wq_info) {
        wq_info->wait_queue_type = 1;
        fill_wq_info_task(wq_info);
        bpf_map_update_elem(&wait_queue_map, &tgid, wq_info, BPF_ANY);
        emit_wq_enqueue(wq_info->wait_queue_head_addr, wq_info->wait_queue_type, wq_info->wait_queue_flags);
    } else {
        struct wait_queue_info wq_info_new = {};
        wq_info_new.wait_queue_head_addr = (__u64)word;
        wq_info_new.wait_queue_type = 1;
        wq_info_new.wait_queue_flags = 0;
        wq_info_new.timestamp_ns = bpf_ktime_get_ns();
        fill_wq_info_task(&wq_info_new);
        bpf_map_update_elem(&wait_queue_map, &tgid, &wq_info_new, BPF_ANY);
        update_wq_size(wq_info_new.wait_queue_head_addr, 1);
        emit_wq_enqueue(wq_info_new.wait_queue_head_addr, wq_info_new.wait_queue_type, wq_info_new.wait_queue_flags);
    }
    return 0;
}

SEC("kprobe/wait_on_page_bit")
int BPF_KPROBE(handle_wait_on_page_bit, void *page, int bit_nr)
{
    __u32 tgid = bpf_get_current_pid_tgid() & 0xFFFFFFFF;
    struct wait_queue_info *wq_info = bpf_map_lookup_elem(&wait_queue_map, &tgid);
    if (wq_info) {
        wq_info->wait_queue_type = 1;
        fill_wq_info_task(wq_info);
        bpf_map_update_elem(&wait_queue_map, &tgid, wq_info, BPF_ANY);
        emit_wq_enqueue(wq_info->wait_queue_head_addr, wq_info->wait_queue_type, wq_info->wait_queue_flags);
    } else {
        struct wait_queue_info wq_info_new = {};
        wq_info_new.wait_queue_head_addr = (__u64)page;
        wq_info_new.wait_queue_type = 1;
        wq_info_new.wait_queue_flags = 0;
        wq_info_new.timestamp_ns = bpf_ktime_get_ns();
        fill_wq_info_task(&wq_info_new);
        bpf_map_update_elem(&wait_queue_map, &tgid, &wq_info_new, BPF_ANY);
        update_wq_size(wq_info_new.wait_queue_head_addr, 1);
        emit_wq_enqueue(wq_info_new.wait_queue_head_addr, wq_info_new.wait_queue_type, wq_info_new.wait_queue_flags);
    }
    return 0;
}

SEC("kprobe/poll_schedule_timeout")
int BPF_KPROBE(handle_poll_schedule_timeout, void *wait_address, int state, long timeout)
{
    __u32 tgid = bpf_get_current_pid_tgid() & 0xFFFFFFFF;
    struct wait_queue_info wq_info_new = {};
    wq_info_new.wait_queue_head_addr = (__u64)wait_address;
    wq_info_new.wait_queue_type = 4;
    wq_info_new.wait_queue_flags = 0;
    wq_info_new.timestamp_ns = bpf_ktime_get_ns();
    fill_wq_info_task(&wq_info_new);
    bpf_map_update_elem(&wait_queue_map, &tgid, &wq_info_new, BPF_ANY);
    update_wq_size(wq_info_new.wait_queue_head_addr, 1);
    emit_wq_enqueue(wq_info_new.wait_queue_head_addr, wq_info_new.wait_queue_type, wq_info_new.wait_queue_flags);
    return 0;
}

SEC("kprobe/schedule_timeout")
int BPF_KPROBE(handle_schedule_timeout, long timeout)
{
    __u32 tgid = bpf_get_current_pid_tgid() & 0xFFFFFFFF;
    struct wait_queue_info *wq_info = bpf_map_lookup_elem(&wait_queue_map, &tgid);
    if (wq_info) {
        wq_info->wait_queue_type = 4;
        fill_wq_info_task(wq_info);
        bpf_map_update_elem(&wait_queue_map, &tgid, wq_info, BPF_ANY);
        emit_wq_enqueue(wq_info->wait_queue_head_addr, wq_info->wait_queue_type, wq_info->wait_queue_flags);
    } else {
        struct wait_queue_info wq_info_new = {};
        wq_info_new.wait_queue_head_addr = (__u64)bpf_get_current_task();
        wq_info_new.wait_queue_type = 4;
        wq_info_new.wait_queue_flags = 0;
        wq_info_new.timestamp_ns = bpf_ktime_get_ns();
        fill_wq_info_task(&wq_info_new);
        bpf_map_update_elem(&wait_queue_map, &tgid, &wq_info_new, BPF_ANY);
        update_wq_size(wq_info_new.wait_queue_head_addr, 1);
        emit_wq_enqueue(wq_info_new.wait_queue_head_addr, wq_info_new.wait_queue_type, wq_info_new.wait_queue_flags);
    }
    return 0;
}

char LICENSE[] SEC("license") = "GPL";
