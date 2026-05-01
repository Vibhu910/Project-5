#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <bpf/libbpf.h>
#include "scheduler_deep_tracer.skel.h"

#ifndef ENOENT
#define ENOENT 2
#endif

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
    unsigned long long timestamp_ns;
    unsigned int       event_type;
    int                cpu;

    unsigned int  pid;
    unsigned int  tgid;
    char          comm[TASK_COMM_LEN];
    int           prio;
    int           static_prio;
    int           normal_prio;

    unsigned long long vruntime;
    unsigned long long exec_start;
    unsigned long long sum_exec_runtime;
    unsigned long long prev_sum_exec_runtime;
    long long          vlag;
    unsigned long long slice;

    unsigned long long load_weight;
    unsigned int       load_inv_weight;

    unsigned long long cfs_min_vruntime;
    unsigned long long cfs_avg_vruntime;
    unsigned int       cfs_nr_running;
    unsigned int       cfs_h_nr_running;
    unsigned long long cfs_exec_clock;
    unsigned long long cfs_load_weight;

    unsigned char      on_rq;
    unsigned long long last_update_rq_clock;

    unsigned long long wait_queue_head_addr;
    unsigned int       wait_queue_flags;
    unsigned char      wait_queue_type;
    unsigned int       wait_queue_size;

    unsigned int       prev_pid;
    unsigned int       prev_tgid;
    char               prev_comm[TASK_COMM_LEN];
    int                prev_prio;
    unsigned long long prev_vruntime;
    unsigned long long prev_sum_exec;
    unsigned long long prev_load_weight;

    int  orig_cpu;
    int  dest_cpu;

    unsigned int child_pid;
    char         filename[FILENAME_LEN];

    unsigned int task_state;

    unsigned char preempt;
    unsigned int policy;
    int          nice;
    unsigned int prev_task_state;
    unsigned int rq_nr_running;
    unsigned long long rq_clock;
    int          wake_cpu;
};

#define MAX_WQ_SUMMARY_BUFFER 256
static struct sched_event wq_summary_buffer[MAX_WQ_SUMMARY_BUFFER];
static int wq_summary_count = 0;
static int expecting_wq_summary = 0;
static int last_wait_queue_was_tracked = 0;

/* Returns a fixed-width string label for the given event type. */
static const char *event_name(unsigned int t) {
    switch (t) {
        case EVT_SWITCH:     return "SCHED_SWITCH ";
        case EVT_WAKEUP:     return "WAKEUP       ";
        case EVT_WAKEUP_NEW: return "WAKEUP_NEW   ";
        case EVT_MIGRATE:    return "MIGRATE      ";
        case EVT_FORK:       return "FORK         ";
        case EVT_EXIT:       return "EXIT/ZOMBIE  ";
        case EVT_EXEC:       return "EXEC         ";
        case EVT_WAIT:       return "WAIT_QUEUE   ";
        case EVT_WQ_ENQUEUE: return "WQ_ENQUEUE   ";
        case EVT_WQ_SUMMARY: return "WQ_SUMMARY   ";
        default:             return "UNKNOWN      ";
    }
}

/* Returns the scheduling policy name for the given kernel SCHED_* value. */
static const char *policy_str(unsigned int p) {
    switch (p) {
        case 0: return "NORMAL";
        case 1: return "FIFO";
        case 2: return "RR";
        case 3: return "BATCH";
        case 4: return "ISO";
        case 5: return "IDLE";
        case 6: return "DEADLINE";
        default: return "?";
    }
}

/* Decodes a task_state bitmask into a human-readable string. */
static const char *state_str(unsigned int s) {
    if (s == 0)      return "RUNNING";
    if (s & 0x0001)  return "INTERRUPTIBLE_SLEEP";
    if (s & 0x0002)  return "UNINTERRUPTIBLE_SLEEP";
    if (s & 0x0010)  return "STOPPED";
    if (s & 0x0040)  return "DEAD";
    if (s & 0x0080)  return "ZOMBIE";
    return "OTHER";
}

/* Returns the wait queue type name for the given numeric type. */
static const char *wait_queue_type_str(unsigned char t) {
    switch (t) {
        case 0: return "UNKNOWN";
        case 1: return "I/O";
        case 2: return "MUTEX";
        case 3: return "SEMAPHORE";
        case 4: return "SLEEP";
        case 5: return "OTHER";
        default: return "UNKNOWN";
    }
}

/* Formats wait_queue_entry flags into a human-readable string. */
static void wait_queue_flags_str(char *buf, size_t sz, unsigned int flags) {
    if (flags == 0) {
        snprintf(buf, sz, "0");
        return;
    }
    if (flags & 0x1)
        snprintf(buf, sz, "EXCLUSIVE 0x%x", flags);
    else
        snprintf(buf, sz, "0x%x", flags);
}

static int header_printed = 0;

/* Prints the column header row for the tabular event output. */
static void print_header(void) {
    if (header_printed) return;
    printf("\n");
    printf("%-12s %-3s %-12s %-6s %-6s %-16s %-5s %-6s %-15s %-10s %-10s %-10s %-8s %-6s %-15s %-6s %-15s %-8s %-10s %-15s %-12s %-20s\n",
           "EVENT", "CPU", "TIME(s)", "PID", "TGID", "COMM", "PRIO", "STATIC", "VRUNTIME(ns)", "VLAG", "SLICE", "SUM_EXEC", "LOAD_WT", "ON_RQ", "STATE",
           "CFS_NR", "CFS_MIN_VR", "CFS_LD_WT", "PREV_PID", "PREV_VR", "WQ_TYPE", "WQ_ADDR");
    printf("%-12s %-3s %-12s %-6s %-6s %-16s %-5s %-6s %-15s %-10s %-8s %-6s %-15s %-6s %-15s %-8s %-10s %-15s %-12s %-20s\n",
           "-----------", "---", "------------", "------", "------", "----------------", "-----", "------",
           "---------------", "----------", "--------", "------", "---------------", "------", "---------------", "--------", "----------", "---------------", "------------", "--------------------");
    header_printed = 1;
}

/* Prints a single sched_event as a tabular row with per-type detail lines. */
static void print_event_row(const struct sched_event *e) {
    unsigned long long ts_s  = e->timestamp_ns / 1000000000ULL;
    unsigned long long ts_us = (e->timestamp_ns % 1000000000ULL) / 1000;
    char time_str[32];
    snprintf(time_str, sizeof(time_str), "%llu.%06llu", ts_s, ts_us);
    
    char vruntime_str[32];
    snprintf(vruntime_str, sizeof(vruntime_str), "vruntime=%llu", e->vruntime);

    char vlag_str[32];
    snprintf(vlag_str, sizeof(vlag_str), "vlag=%lld", e->vlag);
    
    char slice_str[32];
    snprintf(slice_str, sizeof(slice_str), "slice=%lld", e->slice);
    
    char sum_exec_str[32];
    snprintf(sum_exec_str, sizeof(sum_exec_str), "%.3fms", e->sum_exec_runtime / 1e6);
    
    char cfs_min_vr_str[32];
    if (e->cfs_min_vruntime > 1000000000ULL) {
        snprintf(cfs_min_vr_str, sizeof(cfs_min_vr_str), "%.2e", (double)e->cfs_min_vruntime);
    } else {
        snprintf(cfs_min_vr_str, sizeof(cfs_min_vr_str), "%llu", e->cfs_min_vruntime);
    }
    
    char prev_vr_str[32];
    if (e->prev_pid != 0) {
        if (e->prev_vruntime > 1000000000ULL) {
            snprintf(prev_vr_str, sizeof(prev_vr_str), "%llu", e->prev_vruntime);
            //snprintf(prev_vr_str, sizeof(prev_vr_str), "%.2e", (double)e->prev_vruntime);
        } else {
            //snprintf(prev_vr_str, sizeof(prev_vr_str), "%llu", e->prev_vruntime);
            snprintf(prev_vr_str, sizeof(prev_vr_str), "%llu", e->prev_vruntime);
        }
    } else {
        strcpy(prev_vr_str, "-");
    }
    
    char wq_addr_str[32];
    if (e->wait_queue_head_addr != 0) {
        snprintf(wq_addr_str, sizeof(wq_addr_str), "0x%016llx", e->wait_queue_head_addr);
    } else {
        strcpy(wq_addr_str, "-");
    }
    
    char state_short[16];
    const char *state_full = state_str(e->task_state);
    strncpy(state_short, state_full, sizeof(state_short) - 1);
    state_short[sizeof(state_short) - 1] = '\0';
    
    const char *wq_type_str = (e->wait_queue_type != 0) ? wait_queue_type_str(e->wait_queue_type) : "-";
    
    printf("%-12s %-3d %-12s %-6u %-6u %-16.16s %-5d %-6d %-15s %-10s %-10s %-10s %-8llu %-6u %-15.15s %-6u %-15s %-8llu %-10u %-15s %-12s %-20s\n",
           event_name(e->event_type),
           e->cpu,
           time_str,
           e->pid,
           e->tgid,
           e->comm,
           e->prio,
           e->static_prio,
           vruntime_str,
	   vlag_str,
	   slice_str,
           sum_exec_str,
           e->load_weight,
           (unsigned int)e->on_rq,
           state_short,
           e->cfs_nr_running,
           cfs_min_vr_str,
           e->cfs_load_weight,
           e->prev_pid,
           prev_vr_str,
           wq_type_str,
           wq_addr_str);
    
    switch (e->event_type) {
        case EVT_SWITCH:
            if (e->prev_pid != 0) {
                printf("    -> prev: pid=%u %s vruntime=%s %s  prev_state=%s\n",
                       e->prev_pid, e->prev_comm, prev_vr_str,
                       e->preempt ? "preempted" : "voluntary",
                       state_str(e->prev_task_state));
            }
            break;
        case EVT_MIGRATE:
            printf("    -> cpu%d -> cpu%d\n", e->orig_cpu, e->dest_cpu);
            break;
        case EVT_FORK:
            if (e->child_pid != 0) {
                printf("    -> child pid=%u\n", e->child_pid);
            }
            break;
        case EVT_EXEC:
            if (e->filename[0]) {
                printf("    -> exec: %s\n", e->filename);
            }
            break;
        case EVT_WAIT: {
            char wq_flags_buf[32];
            wait_queue_flags_str(wq_flags_buf, sizeof(wq_flags_buf), e->wait_queue_flags);
            printf("    -> wait_queue type=%s addr=%s flags=%s state=%s\n",
                   wq_type_str, wq_addr_str, wq_flags_buf, state_str(e->task_state));
            break;
        }
        case EVT_WQ_ENQUEUE: {
            char wq_flags_buf[32];
            wait_queue_flags_str(wq_flags_buf, sizeof(wq_flags_buf), e->wait_queue_flags);
            printf("    -> enqueued wait: pid=%u tgid=%u %s addr=%s type=%s flags=%s\n",
                   e->pid, e->tgid, e->comm, wq_addr_str, wq_type_str, wq_flags_buf);
            break;
        }
        case EVT_WQ_SUMMARY: {
            char wq_flags_buf[32];
            wait_queue_flags_str(wq_flags_buf, sizeof(wq_flags_buf), e->wait_queue_flags);
            printf("    -> waiting task: pid=%u tgid=%u %s queue_addr=%s type=%s flags=%s size=%u\n",
                   e->pid, e->tgid, e->comm, wq_addr_str, wq_type_str, wq_flags_buf, e->wait_queue_size);
            break;
        }
    }
    printf("    policy=%s nice=%d rq_nr=%u rq_clock=%llu",
           policy_str(e->policy), e->nice, e->rq_nr_running, (unsigned long long)e->rq_clock);
    if (e->event_type == EVT_WAKEUP || e->event_type == EVT_WAKEUP_NEW) {
        if (e->wake_cpu >= 0)
            printf(" wake_cpu=%d", e->wake_cpu);
    }
    printf("\n");
}

/* Comparator for qsort: groups WQ_SUMMARY events by queue address, then timestamp. */
static int compare_wq_summary(const void *a, const void *b)
{
    const struct sched_event *ea = (const struct sched_event *)a;
    const struct sched_event *eb = (const struct sched_event *)b;
    
    if (ea->wait_queue_head_addr < eb->wait_queue_head_addr)
        return -1;
    if (ea->wait_queue_head_addr > eb->wait_queue_head_addr)
        return 1;
    if (ea->timestamp_ns < eb->timestamp_ns)
        return -1;
    if (ea->timestamp_ns > eb->timestamp_ns)
        return 1;
    return 0;
}

/* Sorts and prints all buffered WQ_SUMMARY events, grouped by wait queue. */
static void flush_wq_summary_buffer(void)
{
    if (wq_summary_count == 0) return;
    
    qsort(wq_summary_buffer, wq_summary_count, sizeof(struct sched_event), compare_wq_summary);
    
	//print_header();
    
    unsigned long long last_queue_addr = 0;
    int queue_started = 0;
    
    for (int i = 0; i < wq_summary_count; i++) {
        const struct sched_event *e = &wq_summary_buffer[i];
        
        if (e->wait_queue_head_addr != last_queue_addr) {
            if (queue_started) {
                printf("\n");
            }
            if (e->wait_queue_head_addr != 0) {
                const char *wq_type_str = (e->wait_queue_type != 0) ? wait_queue_type_str(e->wait_queue_type) : "-";
                printf("  === Wait Queue: addr=0x%016llx type=%s size=%u ===\n",
                       e->wait_queue_head_addr, wq_type_str, e->wait_queue_size);
            } else {
                printf("  === Wait Queue: addr=- (no address) ===\n");
            }
            last_queue_addr = e->wait_queue_head_addr;
            queue_started = 1;
        }
        
        print_event_row(e);
        printf("\n");
    }
    
    if (queue_started) {
        printf("\n");
    }
    
    wq_summary_count = 0;
    fflush(stdout);
}

/* Ring-buffer callback: dispatches each BPF event for printing or buffering. */
static int handle_event(void *ctx, void *data, size_t sz)
{
    const struct sched_event *e = data;
    
    if (e->event_type == EVT_WQ_SUMMARY) {
        if (wq_summary_count < MAX_WQ_SUMMARY_BUFFER) {
            memcpy(&wq_summary_buffer[wq_summary_count], e, sizeof(struct sched_event));
            wq_summary_count++;
        }
        return 0;
    }
    
    if (e->event_type == EVT_WAIT) {
        if (wq_summary_count > 0) {
            flush_wq_summary_buffer();
        }
        print_header();
        print_event_row(e);
        printf("\n");
        fflush(stdout);
        last_wait_queue_was_tracked = (e->wait_queue_head_addr != 0);
        expecting_wq_summary = last_wait_queue_was_tracked;
        return 0;
    }
    
    if (wq_summary_count > 0) {
        flush_wq_summary_buffer();
        expecting_wq_summary = 0;
        last_wait_queue_was_tracked = 0;
    } else if (expecting_wq_summary) {
        printf("  (No active wait queues - wait_queue_map was empty)\n");
        printf("  (Note: If the WAIT_QUEUE event shows addr=-, the blocking path wasn't instrumented)\n\n");
        expecting_wq_summary = 0;
        last_wait_queue_was_tracked = 0;
    } else if (last_wait_queue_was_tracked) {
        last_wait_queue_was_tracked = 0;
    }
    
    print_header();
    print_event_row(e);
    printf("\n");
    fflush(stdout);
    return 0;
}

static volatile int stop = 0;
static void sig_handler(int sig) { stop = 1; }

int main(int argc, char **argv)
{
    int duration = 30;
    if (argc > 1) duration = atoi(argv[1]);

    libbpf_set_print(NULL);

    struct scheduler_deep_tracer_bpf *skel = scheduler_deep_tracer_bpf__open();
    if (!skel) {
        fprintf(stderr, "Failed to open BPF skeleton\n");
        return 1;
    }

    int     err = scheduler_deep_tracer_bpf__load(skel);
    if (err) {
        fprintf(stderr, "Failed to load BPF skeleton: %d\n", err);
        goto cleanup;
    }

    skel->links.handle_sched_switch = bpf_program__attach(skel->progs.handle_sched_switch);
    if (!skel->links.handle_sched_switch) {
        err = -errno;
        fprintf(stderr, "Failed to attach sched_switch: %d\n", err);
        goto cleanup;
    }
    skel->links.handle_wakeup = bpf_program__attach(skel->progs.handle_wakeup);
    if (!skel->links.handle_wakeup) { err = -errno; fprintf(stderr, "Failed to attach sched_wakeup: %d\n", err); goto cleanup; }
    skel->links.handle_wakeup_new = bpf_program__attach(skel->progs.handle_wakeup_new);
    if (!skel->links.handle_wakeup_new) { err = -errno; fprintf(stderr, "Failed to attach sched_wakeup_new: %d\n", err); goto cleanup; }
    skel->links.handle_migrate = bpf_program__attach(skel->progs.handle_migrate);
    if (!skel->links.handle_migrate) { err = -errno; fprintf(stderr, "Failed to attach sched_migrate_task: %d\n", err); goto cleanup; }
    skel->links.handle_fork = bpf_program__attach(skel->progs.handle_fork);
    if (!skel->links.handle_fork) { err = -errno; fprintf(stderr, "Failed to attach sched_process_fork: %d\n", err); goto cleanup; }
    skel->links.handle_exit = bpf_program__attach(skel->progs.handle_exit);
    if (!skel->links.handle_exit) { err = -errno; fprintf(stderr, "Failed to attach sched_process_exit: %d\n", err); goto cleanup; }
    skel->links.handle_exec = bpf_program__attach(skel->progs.handle_exec);
    if (!skel->links.handle_exec) { err = -errno; fprintf(stderr, "Failed to attach sched_process_exec: %d\n", err); goto cleanup; }

    #define ATTACH_OPTIONAL(link, prog, name) do { \
        (link) = bpf_program__attach(prog); \
        if (!(link) && errno != ENOENT) { err = -errno; fprintf(stderr, "Failed to attach %s: %d\n", name, err); goto cleanup; } \
    } while (0)
    ATTACH_OPTIONAL(skel->links.handle_dequeue, skel->progs.handle_dequeue, "dequeue_task_fair");
    ATTACH_OPTIONAL(skel->links.handle_prepare_to_wait, skel->progs.handle_prepare_to_wait, "prepare_to_wait");
    ATTACH_OPTIONAL(skel->links.handle_prepare_to_wait_exclusive, skel->progs.handle_prepare_to_wait_exclusive, "prepare_to_wait_exclusive");
    ATTACH_OPTIONAL(skel->links.handle_mutex_lock_slowpath, skel->progs.handle_mutex_lock_slowpath, "__mutex_lock_slowpath");
    ATTACH_OPTIONAL(skel->links.handle_wait_on_bit, skel->progs.handle_wait_on_bit, "__wait_on_bit");
    ATTACH_OPTIONAL(skel->links.handle_wait_on_page_bit, skel->progs.handle_wait_on_page_bit, "wait_on_page_bit");
    ATTACH_OPTIONAL(skel->links.handle_schedule_timeout, skel->progs.handle_schedule_timeout, "schedule_timeout");
    ATTACH_OPTIONAL(skel->links.handle_poll_schedule_timeout, skel->progs.handle_poll_schedule_timeout, "poll_schedule_timeout");
    #undef ATTACH_OPTIONAL

    struct ring_buffer *rb = ring_buffer__new(
        bpf_map__fd(skel->maps.events), handle_event, NULL, NULL);
    if (!rb) {
        fprintf(stderr, "Failed to create ring buffer\n");
        goto cleanup;
    }

    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);

    printf("=== Deep Scheduler Tracer ===\n");
    printf("Tracing CFS internals: vruntime, weights, cfs_rq, wait/ready queues\n");
    printf("Duration: %d seconds  (Ctrl-C to stop early)\n", duration);
    printf("WQ_ENQUEUE: logged each time we record a task about to wait. WQ_TYPE: I/O, MUTEX, SLEEP, OTHER. EXIT: when processes exit.\n");
    printf("WQ_SUMMARY: after each WAIT_QUEUE event, prints all active wait queues (tasks currently waiting).\n");
    printf("Extra line per event: policy, nice, rq_nr (rq runnable count), rq_clock; SWITCH shows voluntary/preempted + prev_state; WAKEUP shows wake_cpu.\n");
    printf("Output format: Tabular\n");
    header_printed = 0;

    time_t start = time(NULL);
    while (!stop && (time(NULL) - start) < duration) {
        err = ring_buffer__poll(rb, 100);
        if (err < 0 && err != -EINTR) {
            fprintf(stderr, "Ring buffer poll error: %d\n", err);
            break;
        }
    }

    flush_wq_summary_buffer();
    
    printf("\n=== Tracer stopped ===\n");
    ring_buffer__free(rb);

cleanup:
    flush_wq_summary_buffer();
    scheduler_deep_tracer_bpf__destroy(skel);
    return err < 0 ? 1 : 0;
}
