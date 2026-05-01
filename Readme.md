1. First of all install all the dependencies using below command
sudo apt update
sudo apt install -y \
  build-essential \
  clang \
  llvm \
  gcc \
  make \
  pkg-config \
  libelf-dev \
  libbpf-dev \
  libbpf-tools \
  bpftrace \
  bpftool \
  bcc-tools \
  linux-headers-$(uname -r) \
  git \
  python3 \
  python3-pip


2. The code has 3 files - the Makefile, scheduler_deep_tracer.c (which handles userspace code) and scheduler_deep_tracer.bpf.c (which handles the kernel side) 
Use make to compile the code and run the below command
   sudo ./scheduler_deep_tracer > output.log 
This traces various events throughout the kernel and stores all information in output.log. The default timer duration is 30 seconds and can be changed.

The logs consist of the following columns.
EVENT	Event type label (e.g. SCHED_SWITCH, WAKEUP, FORK, WAIT_QUEUE, etc.)
CPU	CPU core the event occurred on
TIME(s)	Timestamp in seconds.microseconds since boot
PID	Process ID of the primary task
TGID	Thread group ID of the primary task
COMM	Process name (up to 16 chars)
PRIO	Dynamic scheduling priority
STATIC	Static (nice-based) priority
VRUNTIME(ns)	CFS virtual runtime in nanoseconds (scientific notation if > 1 billion)
SUM_EXEC	Total CPU time consumed, formatted as milliseconds (e.g. 12.345ms)
LOAD_WT	se.load.weight -- CFS load weight of this task
ON_RQ	Whether the task is currently on a run queue (1 or 0)
STATE	Task state: RUNNING, INTERRUPTIBLE_SLEEP, UNINTERRUPTIBLE_SLEEP, STOPPED, DEAD, ZOMBIE, or OTHER
CFS_NR	cfs_rq->nr_running -- number of CFS-runnable tasks on this CPU
CFS_MIN_VR	cfs_rq->min_vruntime (scientific notation if large)
CFS_LD_WT	cfs_rq->load.weight -- total CFS load weight on this CPU's run queue
PREV_PID	PID of the previous/secondary task (or - if none)
PREV_VR	Previous task's vruntime (or - if no prev task)
WQ_TYPE	Wait queue type: I/O, MUTEX, SEMAPHORE, SLEEP, OTHER,
WQ_ADDR	Wait queue head kernel address (hex), or - if none

Per-Event Scheduling Extra Line
Printed for every event, after the detail line:
policy=<NORMAL|FIFO|RR|...> nice=<N> rq_nr=<N> rq_clock=<N>
policy -- scheduling policy (NORMAL, FIFO, RR, BATCH, ISO, IDLE, DEADLINE)
nice -- nice value (-20 to 19)
rq_nr -- total number of runnable tasks on this CPU across all scheduling classes
rq_clock -- the run queue's clock value in nanoseconds
For WAKEUP and WAKEUP_NEW events only, if wake_cpu >= 0, an additional field is appended: wake_cpu=<N> -- the CPU the task is being woken up onto.


Wait QUeue Block
After a WAIT_QUEUE event, all buffered WQ_SUMMARY events are flushed as a grouped block:
For each distinct wait queue address, a header line:
=== Wait Queue: addr=0x<hex> type=<type> size=<N> ===
or if the address is 0:
=== Wait Queue: addr=- (no address) ===
Under each header, one print_event_row + detail line per waiting task (same format as sections 3-5 above).
Blank lines separate different queues and follow the entire block.

If a WAIT_QUEUE event was expected to be followed by WQ_SUMMARY events but none arrived, these two lines are printed before the next non-WQ event:
  (No active wait queues - wait_queue_map was empty)
  (Note: If the WAIT_QUEUE event shows addr=-, the blocking path wasn't instrumented)

Below is the list of events that take place:-
SCHED_SWITCH : -> prev: pid=<N> <comm> vruntime=<V> <preempted|voluntary> prev_state=<state> -- shows the outgoing task's PID, name, vruntime, whether it was preempted or yielded voluntarily, and its state. Only printed if prev_pid != 0.
MIGRATE : -> cpu<N> -> cpu<N> -- shows the source and destination CPUs.
FORK: -> child pid=<N> -- shows the child's PID. Only printed if child_pid != 0.
EXEC : -> exec: <filename> -- shows the executable path. Only printed if filename is non-empty.
WAIT_QUEUE : -> wait_queue type=<type> addr=<addr> flags=<flags> state=<state> -- shows the wait queue type, kernel address, flags (e.g. EXCLUSIVE 0x1 or 0), and the task's state when entering the queue.
WQ_ENQUEUE : -> enqueued wait: pid=<N> tgid=<N> <comm> addr=<addr> type=<type> flags=<flags> -- shows which task was recorded as about to wait, on which queue, with what type and flags.
WQ_SUMMARY : -> waiting task: pid=<N> tgid=<N> <comm> queue_addr=<addr> type=<type> flags=<flags> size=<N> -- shows one currently-waiting task, its queue address, type, flags, and how many total tasks are waiting on that queue.
