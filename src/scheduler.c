#include <limits.h>
#include <stdio.h>
#include <string.h>

#define MAX_PROCESS 100
#define MAX_SEGMENTS 4096
#define MAX_QUEUE 10000
#define PID_LEN 16

typedef struct {
    char pid[PID_LEN];
    int arrival;
    int burst;
    int priority;
    int remaining;
    int completion;
    int response;
} Process;

typedef struct {
    char pid[PID_LEN];
    int start;
    int end;
} Segment;

static int min_int(int a, int b)
{
    return a < b ? a : b;
}

static void add_segment(Segment timeline[], int *count, const char *pid, int start, int end)
{
    if (start == end) {
        return;
    }
    if (*count > 0 &&
        strcmp(timeline[*count - 1].pid, pid) == 0 &&
        timeline[*count - 1].end == start) {
        timeline[*count - 1].end = end;
        return;
    }
    if (*count >= MAX_SEGMENTS) {
        return;
    }
    snprintf(timeline[*count].pid, PID_LEN, "%s", pid);
    timeline[*count].start = start;
    timeline[*count].end = end;
    (*count)++;
}

static void reset_processes(Process processes[], int n)
{
    for (int i = 0; i < n; i++) {
        processes[i].remaining = processes[i].burst;
        processes[i].completion = 0;
        processes[i].response = -1;
    }
}

static void sort_order_by_arrival(Process processes[], int n, int order[])
{
    for (int i = 0; i < n; i++) {
        order[i] = i;
    }
    for (int i = 0; i < n - 1; i++) {
        for (int j = 0; j < n - 1 - i; j++) {
            int a = order[j];
            int b = order[j + 1];
            if (processes[a].arrival > processes[b].arrival ||
                (processes[a].arrival == processes[b].arrival &&
                 strcmp(processes[a].pid, processes[b].pid) > 0)) {
                int tmp = order[j];
                order[j] = order[j + 1];
                order[j + 1] = tmp;
            }
        }
    }
}

static int simulate_fcfs(Process processes[], int n, Segment timeline[])
{
    int order[MAX_PROCESS];
    int count = 0;
    int time = 0;
    reset_processes(processes, n);
    sort_order_by_arrival(processes, n, order);

    for (int i = 0; i < n; i++) {
        Process *p = &processes[order[i]];
        if (time < p->arrival) {
            add_segment(timeline, &count, "IDLE", time, p->arrival);
            time = p->arrival;
        }
        p->response = time - p->arrival;
        add_segment(timeline, &count, p->pid, time, time + p->burst);
        time += p->burst;
        p->remaining = 0;
        p->completion = time;
    }
    return count;
}

static int simulate_sjf(Process processes[], int n, Segment timeline[])
{
    int done[MAX_PROCESS] = {0};
    int completed = 0;
    int time = 0;
    int count = 0;
    reset_processes(processes, n);

    while (completed < n) {
        int chosen = -1;
        for (int i = 0; i < n; i++) {
            if (done[i] || processes[i].arrival > time) {
                continue;
            }
            if (chosen == -1 ||
                processes[i].burst < processes[chosen].burst ||
                (processes[i].burst == processes[chosen].burst &&
                 processes[i].arrival < processes[chosen].arrival) ||
                (processes[i].burst == processes[chosen].burst &&
                 processes[i].arrival == processes[chosen].arrival &&
                 strcmp(processes[i].pid, processes[chosen].pid) < 0)) {
                chosen = i;
            }
        }

        if (chosen == -1) {
            int next_arrival = INT_MAX;
            for (int i = 0; i < n; i++) {
                if (processes[i].remaining > 0 && processes[i].arrival < next_arrival) {
                    next_arrival = processes[i].arrival;
                }
            }
            add_segment(timeline, &count, "IDLE", time, next_arrival);
            time = next_arrival;
            continue;
        }

        Process *p = &processes[chosen];
        p->response = time - p->arrival;
        add_segment(timeline, &count, p->pid, time, time + p->burst);
        time += p->burst;
        p->remaining = 0;
        p->completion = time;
        done[chosen] = 1;
        completed++;
    }
    return count;
}

static int simulate_priority(Process processes[], int n, Segment timeline[])
{
    int done[MAX_PROCESS] = {0};
    int completed = 0;
    int time = 0;
    int count = 0;
    reset_processes(processes, n);

    while (completed < n) {
        int chosen = -1;
        for (int i = 0; i < n; i++) {
            if (done[i] || processes[i].arrival > time) {
                continue;
            }
            if (chosen == -1 ||
                processes[i].priority < processes[chosen].priority ||
                (processes[i].priority == processes[chosen].priority &&
                 processes[i].arrival < processes[chosen].arrival) ||
                (processes[i].priority == processes[chosen].priority &&
                 processes[i].arrival == processes[chosen].arrival &&
                 processes[i].burst < processes[chosen].burst)) {
                chosen = i;
            }
        }

        if (chosen == -1) {
            int next_arrival = INT_MAX;
            for (int i = 0; i < n; i++) {
                if (processes[i].remaining > 0 && processes[i].arrival < next_arrival) {
                    next_arrival = processes[i].arrival;
                }
            }
            add_segment(timeline, &count, "IDLE", time, next_arrival);
            time = next_arrival;
            continue;
        }

        Process *p = &processes[chosen];
        p->response = time - p->arrival;
        add_segment(timeline, &count, p->pid, time, time + p->burst);
        time += p->burst;
        p->remaining = 0;
        p->completion = time;
        done[chosen] = 1;
        completed++;
    }
    return count;
}

static int simulate_rr(Process processes[], int n, int quantum, Segment timeline[])
{
    int order[MAX_PROCESS];
    int ready[MAX_QUEUE];
    int front = 0, rear = 0;
    int next = 0, completed = 0;
    int time = 0, count = 0;
    reset_processes(processes, n);
    sort_order_by_arrival(processes, n, order);

    if (n > 0) {
        time = processes[order[0]].arrival;
    }
    while (next < n && processes[order[next]].arrival <= time) {
        ready[rear++ % MAX_QUEUE] = order[next++];
    }

    while (completed < n) {
        if (front == rear) {
            int next_time = processes[order[next]].arrival;
            add_segment(timeline, &count, "IDLE", time, next_time);
            time = next_time;
            while (next < n && processes[order[next]].arrival <= time) {
                ready[rear++ % MAX_QUEUE] = order[next++];
            }
            continue;
        }

        int idx = ready[front++ % MAX_QUEUE];
        Process *p = &processes[idx];
        if (p->response == -1) {
            p->response = time - p->arrival;
        }
        int run = min_int(quantum, p->remaining);
        add_segment(timeline, &count, p->pid, time, time + run);
        time += run;
        p->remaining -= run;

        while (next < n && processes[order[next]].arrival <= time) {
            ready[rear++ % MAX_QUEUE] = order[next++];
        }

        if (p->remaining > 0) {
            ready[rear++ % MAX_QUEUE] = idx;
        } else {
            p->completion = time;
            completed++;
        }
    }
    return count;
}

static void print_timeline(Segment timeline[], int count)
{
    printf("\n运行顺序/Gantt 图:\n");
    for (int i = 0; i < count; i++) {
        printf("[%d,%d) %s\n", timeline[i].start, timeline[i].end, timeline[i].pid);
    }
}

static void print_stats(Process processes[], int n)
{
    double total_turnaround = 0.0;
    double total_waiting = 0.0;
    double total_response = 0.0;

    printf("\n进程统计:\n");
    printf("%-8s%-10s%-10s%-10s%-10s%-10s%-10s%-10s\n",
           "PID", "到达", "服务", "优先级", "完成", "周转", "等待", "响应");

    for (int i = 0; i < n; i++) {
        int turnaround = processes[i].completion - processes[i].arrival;
        int waiting = turnaround - processes[i].burst;
        total_turnaround += turnaround;
        total_waiting += waiting;
        total_response += processes[i].response;
        printf("%-8s%-10d%-10d%-10d%-10d%-10d%-10d%-10d\n",
               processes[i].pid,
               processes[i].arrival,
               processes[i].burst,
               processes[i].priority,
               processes[i].completion,
               turnaround,
               waiting,
               processes[i].response);
    }

    printf("\n平均周转时间: %.2f\n", total_turnaround / n);
    printf("平均等待时间: %.2f\n", total_waiting / n);
    printf("平均响应时间: %.2f\n", total_response / n);
}

int main(void)
{
    Process processes[MAX_PROCESS];
    Segment timeline[MAX_SEGMENTS];
    int n, choice, timeline_count = 0;

    printf("处理机调度模拟实验\n");
    printf("说明：优先级数值越小，优先级越高。\n\n");
    printf("请输入进程数量: ");
    if (scanf("%d", &n) != 1 || n <= 0 || n > MAX_PROCESS) {
        printf("进程数量必须在 1..%d 之间。\n", MAX_PROCESS);
        return 1;
    }

    for (int i = 0; i < n; i++) {
        snprintf(processes[i].pid, PID_LEN, "P%d", i + 1);
        printf("输入 %s 的 到达时间 服务时间 优先级: ", processes[i].pid);
        if (scanf("%d%d%d", &processes[i].arrival, &processes[i].burst, &processes[i].priority) != 3 ||
            processes[i].arrival < 0 || processes[i].burst <= 0) {
            printf("到达时间不能为负，服务时间必须大于 0。\n");
            return 1;
        }
    }

    printf("\n选择调度算法:\n");
    printf("1. FCFS 先来先服务\n");
    printf("2. SJF 短作业优先（非抢占）\n");
    printf("3. RR 时间片轮转\n");
    printf("4. Priority 优先级调度（非抢占）\n");
    printf("请输入选项: ");
    if (scanf("%d", &choice) != 1) {
        return 1;
    }

    if (choice == 1) {
        timeline_count = simulate_fcfs(processes, n, timeline);
    } else if (choice == 2) {
        timeline_count = simulate_sjf(processes, n, timeline);
    } else if (choice == 3) {
        int quantum;
        printf("请输入时间片大小: ");
        if (scanf("%d", &quantum) != 1 || quantum <= 0) {
            printf("时间片必须大于 0。\n");
            return 1;
        }
        timeline_count = simulate_rr(processes, n, quantum, timeline);
    } else if (choice == 4) {
        timeline_count = simulate_priority(processes, n, timeline);
    } else {
        printf("无效选项。\n");
        return 1;
    }

    print_timeline(timeline, timeline_count);
    print_stats(processes, n);
    return 0;
}
