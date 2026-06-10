#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_BLOCKS 512
#define PID_LEN 32

typedef struct {
    int start;
    int size;
    int free;
    char pid[PID_LEN];
} Block;

static void merge_free_blocks(Block blocks[], int *count)
{
    for (int i = 0; i + 1 < *count;) {
        if (blocks[i].free && blocks[i + 1].free) {
            blocks[i].size += blocks[i + 1].size;
            for (int j = i + 1; j + 1 < *count; j++) {
                blocks[j] = blocks[j + 1];
            }
            (*count)--;
        } else {
            i++;
        }
    }
}

static void show_partitions(Block blocks[], int count)
{
    printf("\n当前内存分区:\n");
    printf("%-10s%-10s%-12s%s\n", "起址", "大小", "状态", "进程");
    for (int i = 0; i < count; i++) {
        printf("%-10d%-10d%-12s%s\n",
               blocks[i].start,
               blocks[i].size,
               blocks[i].free ? "空闲" : "已分配",
               blocks[i].free ? "-" : blocks[i].pid);
    }
}

static int allocate_partition(Block blocks[], int *count, const char *pid, int size, int best_fit)
{
    int chosen = -1;
    for (int i = 0; i < *count; i++) {
        if (!blocks[i].free || blocks[i].size < size) {
            continue;
        }
        if (!best_fit) {
            chosen = i;
            break;
        }
        if (chosen == -1 || blocks[i].size < blocks[chosen].size) {
            chosen = i;
        }
    }

    if (chosen == -1) {
        printf("分配失败：没有足够大的空闲分区。\n");
        return 0;
    }

    if (blocks[chosen].size == size) {
        blocks[chosen].free = 0;
        snprintf(blocks[chosen].pid, PID_LEN, "%s", pid);
    } else {
        if (*count >= MAX_BLOCKS) {
            printf("分配失败：分区表已满。\n");
            return 0;
        }
        for (int i = *count; i > chosen; i--) {
            blocks[i] = blocks[i - 1];
        }
        blocks[chosen].start = blocks[chosen + 1].start;
        blocks[chosen].size = size;
        blocks[chosen].free = 0;
        snprintf(blocks[chosen].pid, PID_LEN, "%s", pid);
        blocks[chosen + 1].start += size;
        blocks[chosen + 1].size -= size;
        (*count)++;
    }

    printf("已为 %s 分配 %dKB。\n", pid, size);
    return 1;
}

static int free_partition(Block blocks[], int *count, const char *pid)
{
    for (int i = 0; i < *count; i++) {
        if (!blocks[i].free && strcmp(blocks[i].pid, pid) == 0) {
            blocks[i].free = 1;
            blocks[i].pid[0] = '\0';
            merge_free_blocks(blocks, count);
            printf("已回收进程 %s 的内存。\n", pid);
            return 1;
        }
    }
    printf("回收失败：未找到进程 %s。\n", pid);
    return 0;
}

static void partition_simulation(int best_fit)
{
    int memory_size;
    printf("请输入内存总大小(KB): ");
    if (scanf("%d", &memory_size) != 1 || memory_size <= 0) {
        printf("内存大小必须大于 0。\n");
        return;
    }

    Block blocks[MAX_BLOCKS];
    int block_count = 1;
    blocks[0].start = 0;
    blocks[0].size = memory_size;
    blocks[0].free = 1;
    blocks[0].pid[0] = '\0';

    printf("\n命令：alloc <进程名> <大小KB>，free <进程名>，show，back\n");
    while (1) {
        char command[32];
        printf("partition> ");
        if (scanf("%31s", command) != 1) {
            return;
        }
        if (strcmp(command, "alloc") == 0) {
            char pid[PID_LEN];
            int size;
            if (scanf("%31s%d", pid, &size) != 2 || size <= 0) {
                printf("分配大小必须大于 0。\n");
            } else {
                allocate_partition(blocks, &block_count, pid, size, best_fit);
            }
        } else if (strcmp(command, "free") == 0) {
            char pid[PID_LEN];
            if (scanf("%31s", pid) == 1) {
                free_partition(blocks, &block_count, pid);
            }
        } else if (strcmp(command, "show") == 0) {
            show_partitions(blocks, block_count);
        } else if (strcmp(command, "back") == 0) {
            break;
        } else {
            printf("未知命令。\n");
        }
    }
}

static int find_int(int values[], int count, int target)
{
    for (int i = 0; i < count; i++) {
        if (values[i] == target) {
            return i;
        }
    }
    return -1;
}

static void print_frames(int frames[], int frame_count)
{
    printf("[");
    for (int i = 0; i < frame_count; i++) {
        if (frames[i] == -1) {
            printf("-");
        } else {
            printf("%d", frames[i]);
        }
        if (i + 1 < frame_count) {
            printf(" ");
        }
    }
    printf("]");
}

static void fifo_replacement(int refs[], int ref_count, int frame_count)
{
    int *frames = (int *)malloc(sizeof(int) * frame_count);
    int *fifo = (int *)malloc(sizeof(int) * frame_count);
    int head = 0, tail = 0, faults = 0;
    if (frames == NULL || fifo == NULL) {
        printf("内存不足。\n");
        free(frames);
        free(fifo);
        return;
    }
    for (int i = 0; i < frame_count; i++) {
        frames[i] = -1;
    }

    printf("\nFIFO 页面置换过程:\n");
    for (int i = 0; i < ref_count; i++) {
        int page = refs[i];
        int hit = find_int(frames, frame_count, page) != -1;
        if (!hit) {
            faults++;
            int empty = find_int(frames, frame_count, -1);
            if (empty != -1) {
                frames[empty] = page;
                fifo[tail++ % frame_count] = empty;
            } else {
                int victim = fifo[head++ % frame_count];
                frames[victim] = page;
                fifo[tail++ % frame_count] = victim;
            }
        }
        printf("引用 %3d ", page);
        print_frames(frames, frame_count);
        printf("%s\n", hit ? " 命中" : " 缺页");
    }

    printf("缺页次数: %d\n", faults);
    printf("缺页率: %.2f%%\n", 100.0 * faults / ref_count);
    free(frames);
    free(fifo);
}

static void lru_replacement(int refs[], int ref_count, int frame_count)
{
    int *frames = (int *)malloc(sizeof(int) * frame_count);
    int *last_used = (int *)malloc(sizeof(int) * frame_count);
    int faults = 0;
    if (frames == NULL || last_used == NULL) {
        printf("内存不足。\n");
        free(frames);
        free(last_used);
        return;
    }
    for (int i = 0; i < frame_count; i++) {
        frames[i] = -1;
        last_used[i] = -1;
    }

    printf("\nLRU 页面置换过程:\n");
    for (int time = 0; time < ref_count; time++) {
        int page = refs[time];
        int pos = find_int(frames, frame_count, page);
        int hit = pos != -1;
        if (hit) {
            last_used[pos] = time;
        } else {
            faults++;
            pos = find_int(frames, frame_count, -1);
            if (pos == -1) {
                pos = 0;
                for (int i = 1; i < frame_count; i++) {
                    if (last_used[i] < last_used[pos]) {
                        pos = i;
                    }
                }
            }
            frames[pos] = page;
            last_used[pos] = time;
        }
        printf("引用 %3d ", page);
        print_frames(frames, frame_count);
        printf("%s\n", hit ? " 命中" : " 缺页");
    }

    printf("缺页次数: %d\n", faults);
    printf("缺页率: %.2f%%\n", 100.0 * faults / ref_count);
    free(frames);
    free(last_used);
}

static void page_replacement(int lru)
{
    int frame_count;
    printf("请输入物理块数量: ");
    if (scanf("%d", &frame_count) != 1 || frame_count <= 0) {
        printf("物理块数量必须大于 0。\n");
        return;
    }

    int ref_count;
    printf("请输入页面引用串长度: ");
    if (scanf("%d", &ref_count) != 1 || ref_count <= 0) {
        printf("引用串长度必须大于 0。\n");
        return;
    }

    int *refs = (int *)malloc(sizeof(int) * ref_count);
    if (refs == NULL) {
        printf("内存不足。\n");
        return;
    }
    printf("请输入页面引用串: ");
    for (int i = 0; i < ref_count; i++) {
        scanf("%d", &refs[i]);
    }

    if (lru) {
        lru_replacement(refs, ref_count, frame_count);
    } else {
        fifo_replacement(refs, ref_count, frame_count);
    }
    free(refs);
}

int main(void)
{
    while (1) {
        int choice;
        printf("\n内存管理模拟实验\n");
        printf("1. 动态分区 - 首次适应 FF\n");
        printf("2. 动态分区 - 最佳适应 BF\n");
        printf("3. 页面置换 - FIFO\n");
        printf("4. 页面置换 - LRU\n");
        printf("0. 退出\n");
        printf("请输入选项: ");

        if (scanf("%d", &choice) != 1) {
            break;
        }
        if (choice == 0) {
            break;
        } else if (choice == 1) {
            partition_simulation(0);
        } else if (choice == 2) {
            partition_simulation(1);
        } else if (choice == 3) {
            page_replacement(0);
        } else if (choice == 4) {
            page_replacement(1);
        } else {
            printf("无效选项。\n");
        }
    }
    return 0;
}
