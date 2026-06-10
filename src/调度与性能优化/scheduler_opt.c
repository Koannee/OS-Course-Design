#include <stdio.h>
#include <stdlib.h>

#define MAX_PROCESS 20 // 定义最多支持的进程数量
#define RR_QUEUE_SIZE 10000 // 定义 RR 队列容量
#define AGING_FACTOR 1  // 动态老化 SJF 的老化因子

typedef struct {
    int pid;  // 进程编号
    int arrival_time;  // 到达时间
    int burst_time;  // 服务时间
    int remaining_time; // 剩余运行时间
    int finish_time; // 完成时间
    int turnaround_time;  // 周转时间
    int waiting_time;  // 等待时间
    double weighted_turnaround_time; // 带权周转时间 
} Process;

// 输入缓冲清理
static void clear_input_buffer() {
    int ch;
    while ((ch = getchar()) != '\n' && ch != EOF) {}
}

// 交换进程
static void swap_process(Process* a, Process* b) {
    Process temp = *a;
    *a = *b;
    *b = temp;
}

// 根据到达时间从小到大排序
static void sort_by_arrival_time(Process p[], int n) {
    for (int i = 0; i < n - 1; i++) {
        for (int j = 0; j < n - 1 - i; j++) {
            if (p[j].arrival_time > p[j + 1].arrival_time) {
                swap_process(&p[j], &p[j + 1]);
            }
        }
    }
}

// FCFS 先来先服务调度算法
void fcfs(Process p[], int n) {
    int current_time = 0;
    printf("\nFCFS 先来先服务调度运行顺序：\n");
    // 进程已经按到达时间排序，因此按数组顺序依次执行
    for (int i = 0; i < n; i++) {
        // 如果当前时间早于该进程到达时间，说明 CPU 需要空闲等待
        if (current_time < p[i].arrival_time) {
            printf("[时间 %d-%d：CPU 空闲] ", current_time, p[i].arrival_time);
            current_time = p[i].arrival_time;
        }
        // 当前进程从 current_time 开始运行，连续执行完整服务时间
        printf("[时间 %d-%d：P%d] ", current_time, current_time + p[i].burst_time, p[i].pid);
        current_time += p[i].burst_time;
        // 计算该进程的完成时间、周转时间、等待时间和带权周转时间
        p[i].finish_time = current_time;
        p[i].turnaround_time = p[i].finish_time - p[i].arrival_time;
        p[i].waiting_time = p[i].turnaround_time - p[i].burst_time;
        p[i].weighted_turnaround_time = (double)p[i].turnaround_time / p[i].burst_time;
    }

    printf("\n\n调度结果：\n");
    printf("进程\t到达时间\t服务时间\t完成时间\t等待时间\t周转时间\t带权周转时间\n");
    double avg_waiting = 0, avg_turnaround = 0, avg_weighted = 0;
    for (int i = 0; i < n; i++) {
        printf("P%d\t%d\t\t%d\t\t%d\t\t%d\t\t%d\t\t%.2f\n",
            p[i].pid, p[i].arrival_time, p[i].burst_time,
            p[i].finish_time, p[i].waiting_time,
            p[i].turnaround_time, p[i].weighted_turnaround_time);
        avg_turnaround += p[i].turnaround_time;
        avg_weighted += p[i].weighted_turnaround_time;
        avg_waiting += p[i].waiting_time;
    }
    printf("平均等待时间：%.2f\n", avg_waiting / n);
    printf("平均周转时间：%.2f\n", avg_turnaround / n);
    printf("平均带权周转时间：%.2f\n", avg_weighted / n);
}

// 原始 SJF 短作业优先调度算法
void sjf(Process p[], int n) {
    Process proc[MAX_PROCESS];
    for (int i = 0; i < n; i++) proc[i] = p[i];
    int completed = 0, current_time = 0; // completed 记录已完成进程数
    int visited[MAX_PROCESS] = { 0 };  // visited 标记进程是否已经执行完成

    printf("\nSJF 短作业优先调度运行顺序：\n");

    // 直到所有进程都被调度完成
    while (completed < n) {
        int idx = -1; // 当前选中的进程下标
        int min_burst = 1 << 30; // 当前可运行进程中的最短服务时间
        // 在已经到达且未完成的进程中选择服务时间最短的进程
        for (int i = 0; i < n; i++) {
            if (!visited[i] && proc[i].arrival_time <= current_time && proc[i].burst_time < min_burst) {
                min_burst = proc[i].burst_time;
                idx = i;
            }
        }
        // 如果没有进程到达，CPU 空闲一个时间单位
        if (idx == -1) {
            current_time++;
            printf("[时间 %d：CPU 空闲] ", current_time - 1);
        }
        else {
            // 非抢占式 SJF 选中进程后一次性运行到结束
            printf("[时间 %d-%d：P%d] ", current_time, current_time + proc[idx].burst_time, proc[idx].pid);
            current_time += proc[idx].burst_time;
            // 计算调度结果
            proc[idx].finish_time = current_time;
            proc[idx].turnaround_time = proc[idx].finish_time - proc[idx].arrival_time;
            proc[idx].waiting_time = proc[idx].turnaround_time - proc[idx].burst_time;
            proc[idx].weighted_turnaround_time = (double)proc[idx].turnaround_time / proc[idx].burst_time;
            visited[idx] = 1;
            completed++;
        }
    }

    printf("\n\n调度结果：\n");
    printf("进程\t到达时间\t服务时间\t完成时间\t等待时间\t周转时间\t带权周转时间\n");
    double avg_turnaround = 0, avg_weighted = 0, avg_waiting = 0;
    for (int i = 0; i < n; i++) {
        printf("P%d\t%d\t\t%d\t\t%d\t\t%d\t\t%d\t\t%.2f\n",
            proc[i].pid, proc[i].arrival_time, proc[i].burst_time,
            proc[i].finish_time, proc[i].waiting_time,
            proc[i].turnaround_time, proc[i].weighted_turnaround_time);
        avg_waiting += proc[i].waiting_time;
        avg_turnaround += proc[i].turnaround_time;
        avg_weighted += proc[i].weighted_turnaround_time;
    }
    printf("平均等待时间：%.2f\n", avg_waiting / n);
    printf("平均周转时间：%.2f\n", avg_turnaround / n);
    printf("平均带权周转时间：%.2f\n", avg_weighted / n);
}

// 原始 RR 时间片轮转调度算法
void rr(Process p[], int n, int quantum) {
    Process proc[MAX_PROCESS];
    for (int i = 0; i < n; i++) {
        proc[i] = p[i];
        proc[i].remaining_time = proc[i].burst_time;
    }

    int queue[RR_QUEUE_SIZE], front = 0, rear = 0; // 使用数组模拟循环队列中的等待队列
    int current_time = 0, completed = 0;  // current_time 为系统时间，completed 为完成数
    int in_queue[MAX_PROCESS] = { 0 }; // 标记进程是否已经进入过队列

    printf("\nRR 时间片轮转调度运行顺序：\n");

    // 初始化队列，将 0 时刻已经到达的进程加入等待队列
    for (int i = 0; i < n; i++) {
        if (proc[i].arrival_time <= current_time && !in_queue[i]) {
            queue[rear++] = i;
            in_queue[i] = 1;
        }
    }
    // 循环调度，直到所有进程完成
    while (completed < n) {
        // 队列为空说明当前没有可运行进程，CPU 空闲一个时间单位
        if (front == rear) {
            printf("[时间 %d：CPU 空闲] ", current_time);
            current_time++;
            // 时间推进后检查是否有新进程到达
            for (int i = 0; i < n; i++)
                if (proc[i].arrival_time <= current_time && !in_queue[i]) {
                    queue[rear++] = i;
                    in_queue[i] = 1;
                }
            continue;
        }
        // 取出队首进程进行一个时间片的运行
        int idx = queue[front++];
        int t = (proc[idx].remaining_time > quantum) ? quantum : proc[idx].remaining_time;
        printf("[时间 %d-%d：P%d] ", current_time, current_time + t, proc[idx].pid);
        current_time += t;
        proc[idx].remaining_time -= t;

        // 新到达进程入队
        for (int i = 0; i < n; i++)
            if (proc[i].arrival_time <= current_time && !in_queue[i]) {
                queue[rear++] = i;
                in_queue[i] = 1;
            }
        // 如果当前进程未完成，则重新加入队尾；否则记录完成信息
        if (proc[idx].remaining_time > 0)
            queue[rear++] = idx;
        else {
            proc[idx].finish_time = current_time;
            proc[idx].turnaround_time = proc[idx].finish_time - proc[idx].arrival_time;
            proc[idx].waiting_time = proc[idx].turnaround_time - proc[idx].burst_time;
            proc[idx].weighted_turnaround_time = (double)proc[idx].turnaround_time / proc[idx].burst_time;
            completed++;
        }
    }

    printf("\n\n调度结果：\n");
    printf("进程\t到达时间\t服务时间\t完成时间\t等待时间\t周转时间\t带权周转时间\n");
    double avg_waiting = 0, avg_turnaround = 0, avg_weighted = 0;
    for (int i = 0; i < n; i++) {
        printf("P%d\t%d\t\t%d\t\t%d\t\t%d\t\t%d\t\t%.2f\n",
            proc[i].pid, proc[i].arrival_time, proc[i].burst_time,
            proc[i].finish_time, proc[i].waiting_time,
            proc[i].turnaround_time, proc[i].weighted_turnaround_time);
        avg_turnaround += proc[i].turnaround_time;
        avg_weighted += proc[i].weighted_turnaround_time;
        avg_waiting += proc[i].waiting_time;
    }
    printf("平均等待时间：%.2f\n", avg_waiting / n);
    printf("平均周转时间：%.2f\n", avg_turnaround / n);
    printf("平均带权周转时间：%.2f\n", avg_weighted / n);
}

// 优化 SJF：动态老化
void sjf_optimized(Process p[], int n) {
    Process proc[MAX_PROCESS];
    for (int i = 0; i < n; i++) proc[i] = p[i];

    int completed = 0;
    int current_time = 0;
    int visited[MAX_PROCESS] = { 0 };

    printf("\n优化 SJF 动态老化调度运行顺序：\n");

    while (completed < n) {
        int idx = -1;
        int min_score = 1 << 30; // score 越小，进程越优先执行
        for (int i = 0; i < n; i++) {
            if (!visited[i] && proc[i].arrival_time <= current_time) {
                int waiting_time = current_time - proc[i].arrival_time; // 等待时间
                int score = proc[i].remaining_time - AGING_FACTOR * waiting_time;  // 老化后的优先级分数
                // 分数更小表示更应该被调度
                if (score < min_score) {
                    min_score = score;
                    idx = i;
                }
            }
        }
        // 当前没有进程可运行时，CPU 空闲并推进时间
        if (idx == -1) {
            printf("[时间 %d：CPU 空闲] ", current_time);
            current_time++;
        }
        else {
            // 选中进程后仍采用非抢占式方式执行到完成
            printf("[时间 %d-%d：P%d] ", current_time, current_time + proc[idx].remaining_time, proc[idx].pid);
            current_time += proc[idx].remaining_time;
            // 记录完成后的统计指标
            proc[idx].finish_time = current_time;
            proc[idx].turnaround_time = proc[idx].finish_time - proc[idx].arrival_time;
            proc[idx].waiting_time = proc[idx].turnaround_time - proc[idx].burst_time;
            proc[idx].weighted_turnaround_time = (double)proc[idx].turnaround_time / proc[idx].burst_time;
            visited[idx] = 1;
            completed++;
        }
    }

    printf("\n\n调度结果：\n");
    printf("进程\t到达时间\t服务时间\t完成时间\t等待时间\t周转时间\t带权周转时间\n");
    double avg_waiting = 0, avg_turnaround = 0, avg_weighted = 0;
    for (int i = 0; i < n; i++) {
        printf("P%d\t%d\t\t%d\t\t%d\t\t%d\t\t%d\t\t%.2f\n",
            proc[i].pid, proc[i].arrival_time, proc[i].burst_time,
            proc[i].finish_time, proc[i].waiting_time,
            proc[i].turnaround_time, proc[i].weighted_turnaround_time);
        avg_turnaround += proc[i].turnaround_time;
        avg_weighted += proc[i].weighted_turnaround_time;
        avg_waiting += proc[i].waiting_time;
    }
    printf("平均等待时间：%.2f\n", avg_waiting / n);
    printf("平均周转时间：%.2f\n", avg_turnaround / n);
    printf("平均带权周转时间：%.2f\n", avg_weighted / n);
}

// 优化 RR：自适应时间片
void rr_adaptive(Process p[], int n) {
    Process proc[MAX_PROCESS];
    for (int i = 0; i < n; i++) {
        proc[i] = p[i];
        proc[i].remaining_time = proc[i].burst_time;
    }

    int queue[RR_QUEUE_SIZE], front = 0, rear = 0; // 等待队列及其队头、队尾指针
    int current_time = 0, completed = 0;
    int in_queue[MAX_PROCESS] = { 0 }; // 防止同一进程被重复加入队列

    printf("\n自适应 RR 时间片轮转调度运行顺序：\n");
    // 将初始时刻已经到达的进程加入队列
    for (int i = 0; i < n; i++)
        if (proc[i].arrival_time <= current_time && !in_queue[i]) {
            queue[rear++] = i;
            in_queue[i] = 1;
        }

    while (completed < n) {
        // 队列为空时表示暂时没有可运行进程，CPU 空闲一个时间单位
        if (front == rear) {
            printf("[时间 %d：CPU 空闲] ", current_time);
            current_time++;
            // 时间推进后检查新到达进程
            for (int i = 0; i < n; i++)
                if (proc[i].arrival_time <= current_time && !in_queue[i]) {
                    queue[rear++] = i;
                    in_queue[i] = 1;
                }
            continue;
        }

        // 动态计算时间片：取当前队列中进程剩余时间的平均值
        int total_remaining = 0, count = 0;
        for (int i = front; i < rear; i++) {
            int idx2 = queue[i];
            if (proc[idx2].remaining_time > 0) {
                total_remaining += proc[idx2].remaining_time;
                count++;
            }
        }
        // 若队列统计异常则设为 1，否则使用平均剩余时间作为自适应时间片
        int adaptive_quantum = (count == 0) ? 1 : (total_remaining / count);
        if (adaptive_quantum < 1) adaptive_quantum = 1;
        // 取出队首进程，并按自适应时间片执行
        int idx = queue[front++];
        int t = (proc[idx].remaining_time > adaptive_quantum) ? adaptive_quantum : proc[idx].remaining_time;
        printf("[时间 %d-%d：P%d] ", current_time, current_time + t, proc[idx].pid);
        current_time += t;
        proc[idx].remaining_time -= t;
        // 将运行期间新到达的进程加入队列
        for (int i = 0; i < n; i++)
            if (proc[i].arrival_time <= current_time && !in_queue[i]) {
                queue[rear++] = i;
                in_queue[i] = 1;
            }
        // 未完成则回到队尾继续等待；完成则记录统计结果
        if (proc[idx].remaining_time > 0)
            queue[rear++] = idx;
        else {
            proc[idx].finish_time = current_time;
            proc[idx].turnaround_time = proc[idx].finish_time - proc[idx].arrival_time;
            proc[idx].waiting_time = proc[idx].turnaround_time - proc[idx].burst_time;
            proc[idx].weighted_turnaround_time = (double)proc[idx].turnaround_time / proc[idx].burst_time;
            completed++;
        }
    }

    printf("\n\n调度结果：\n");
    printf("进程\t到达时间\t服务时间\t完成时间\t等待时间\t周转时间\t带权周转时间\n");
    double avg_waiting = 0, avg_turnaround = 0, avg_weighted = 0;
    for (int i = 0; i < n; i++) {
        printf("P%d\t%d\t\t%d\t\t%d\t\t%d\t\t%d\t\t%.2f\n",
            proc[i].pid, proc[i].arrival_time, proc[i].burst_time,
            proc[i].finish_time, proc[i].waiting_time,
            proc[i].turnaround_time, proc[i].weighted_turnaround_time);
        avg_turnaround += proc[i].turnaround_time;
        avg_weighted += proc[i].weighted_turnaround_time;
        avg_waiting += proc[i].waiting_time;
    }
    printf("平均等待时间：%.2f\n", avg_waiting / n);
    printf("平均周转时间：%.2f\n", avg_turnaround / n);
    printf("平均带权周转时间：%.2f\n", avg_weighted / n);
}

int main() {
    int n;
    Process p[MAX_PROCESS];

    printf("请输入进程数量，最多 20 个：");
    scanf("%d", &n);
    if (n > MAX_PROCESS) n = MAX_PROCESS;
    for (int i = 0; i < n; i++) {
        p[i].pid = i + 1;
        printf("\n请输入进程 P%d 的到达时间：", i + 1);
        scanf("%d", &p[i].arrival_time);
        printf("请输入进程 P%d 的服务时间：", i + 1);
        scanf("%d", &p[i].burst_time);
        p[i].remaining_time = p[i].burst_time;
        p[i].waiting_time = 0;
    }

    // 先排序按到达时间
    sort_by_arrival_time(p, n);

    // 原始算法实现
    fcfs(p, n);
    sjf(p, n);
    rr(p, n, 2);

    // 优化算法
    sjf_optimized(p, n);
    rr_adaptive(p, n);

    return 0;
}