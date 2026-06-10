#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BLOCK_SIZE 64
#define BLOCK_COUNT 128
#define MAX_FILES 32
#define NAME_LEN 25
#define MAX_CONTENT (BLOCK_SIZE * BLOCK_COUNT)
#define FAT_FREE -1
#define FAT_END -2

typedef struct {
    char name[NAME_LEN];
    int size;
    int first_block;
    int used;
} FileEntry;

typedef struct {
    const char *image_path;
    FileEntry directory[MAX_FILES];
    int fat[BLOCK_COUNT];
    char blocks[BLOCK_COUNT][BLOCK_SIZE];
} MiniFileSystem;

static char *trim_left(char *s)
{
    while (*s != '\0' && isspace((unsigned char)*s)) {
        s++;
    }
    return s;
}

static int valid_name(const char *name)
{
    return name != NULL && name[0] != '\0' && strlen(name) < NAME_LEN;
}

static void format_fs(MiniFileSystem *fs, int save_image, int announce);
static int create_file_internal(MiniFileSystem *fs, const char *name, const char *content,
                                int save_image, int announce);

static int find_file(MiniFileSystem *fs, const char *name)
{
    for (int i = 0; i < MAX_FILES; i++) {
        if (fs->directory[i].used && strcmp(fs->directory[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static int find_free_entry(MiniFileSystem *fs)
{
    for (int i = 0; i < MAX_FILES; i++) {
        if (!fs->directory[i].used) {
            return i;
        }
    }
    return -1;
}

static int free_block_count(MiniFileSystem *fs)
{
    int count = 0;
    for (int i = 0; i < BLOCK_COUNT; i++) {
        if (fs->fat[i] == FAT_FREE) {
            count++;
        }
    }
    return count;
}

static int get_chain(MiniFileSystem *fs, int first_block, int chain[])
{
    int count = 0;
    int seen[BLOCK_COUNT] = {0};
    int current = first_block;
    while (current >= 0 && current < BLOCK_COUNT && !seen[current]) {
        chain[count++] = current;
        seen[current] = 1;
        if (fs->fat[current] == FAT_END) {
            break;
        }
        current = fs->fat[current];
    }
    return count;
}

static void free_chain(MiniFileSystem *fs, int first_block)
{
    int chain[BLOCK_COUNT];
    int count = get_chain(fs, first_block, chain);
    for (int i = 0; i < count; i++) {
        int block = chain[i];
        fs->fat[block] = FAT_FREE;
        memset(fs->blocks[block], 0, BLOCK_SIZE);
    }
}

static int allocate_blocks(MiniFileSystem *fs, int content_size, int allocated[], int *allocated_count)
{
    int needed = content_size == 0 ? 0 : (content_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    *allocated_count = 0;
    if (free_block_count(fs) < needed) {
        return 0;
    }
    for (int i = 0; i < BLOCK_COUNT && *allocated_count < needed; i++) {
        if (fs->fat[i] == FAT_FREE) {
            allocated[(*allocated_count)++] = i;
        }
    }
    for (int i = 0; i < *allocated_count; i++) {
        fs->fat[allocated[i]] = (i + 1 < *allocated_count) ? allocated[i + 1] : FAT_END;
    }
    return 1;
}

static void write_content_to_blocks(MiniFileSystem *fs, const char *content, int content_size,
                                    int allocated[], int allocated_count)
{
    for (int i = 0; i < allocated_count; i++) {
        int block = allocated[i];
        int offset = i * BLOCK_SIZE;
        int copy_len = content_size - offset;
        if (copy_len > BLOCK_SIZE) {
            copy_len = BLOCK_SIZE;
        }
        memset(fs->blocks[block], 0, BLOCK_SIZE);
        if (copy_len > 0) {
            memcpy(fs->blocks[block], content + offset, (size_t)copy_len);
        }
    }
}

static char *read_content(MiniFileSystem *fs, FileEntry *entry)
{
    char *result = (char *)calloc((size_t)entry->size + 1, 1);
    int chain[BLOCK_COUNT];
    int copied = 0;
    int count;
    if (result == NULL) {
        return NULL;
    }
    count = get_chain(fs, entry->first_block, chain);
    for (int i = 0; i < count && copied < entry->size; i++) {
        int copy_len = entry->size - copied;
        if (copy_len > BLOCK_SIZE) {
            copy_len = BLOCK_SIZE;
        }
        memcpy(result + copied, fs->blocks[chain[i]], (size_t)copy_len);
        copied += copy_len;
    }
    result[entry->size] = '\0';
    return result;
}

static int hex_value(int ch)
{
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return -1;
}

static void save_fs(MiniFileSystem *fs)
{
    static const char digits[] = "0123456789ABCDEF";
    FILE *out = fopen(fs->image_path, "w");
    if (out == NULL) {
        printf("警告：无法保存虚拟磁盘文件 %s。\n", fs->image_path);
        return;
    }
    fprintf(out, "MINIFS1\n");
    for (int i = 0; i < MAX_FILES; i++) {
        if (fs->directory[i].used) {
            char *content = read_content(fs, &fs->directory[i]);
            fprintf(out, "%s %d ", fs->directory[i].name, fs->directory[i].size);
            if (content != NULL) {
                for (int j = 0; j < fs->directory[i].size; j++) {
                    unsigned char c = (unsigned char)content[j];
                    fputc(digits[c >> 4], out);
                    fputc(digits[c & 15], out);
                }
                free(content);
            }
            fputc('\n', out);
        }
    }
    fclose(out);
}

static void load_fs(MiniFileSystem *fs)
{
    FILE *in = fopen(fs->image_path, "r");
    char magic[32];
    if (in == NULL) {
        return;
    }
    if (fgets(magic, sizeof(magic), in) == NULL || strncmp(magic, "MINIFS1", 7) != 0) {
        printf("检测到无法识别的虚拟磁盘镜像，已使用空文件系统。\n");
        fclose(in);
        return;
    }

    format_fs(fs, 0, 0);
    while (!feof(in)) {
        char name[NAME_LEN];
        int size;
        char hex[MAX_CONTENT * 2 + 1];
        if (fscanf(in, "%24s %d %16384s", name, &size, hex) != 3) {
            break;
        }
        if (size < 0 || size > MAX_CONTENT || (int)strlen(hex) < size * 2) {
            continue;
        }
        char *content = (char *)malloc((size_t)size + 1);
        if (content == NULL) {
            break;
        }
        for (int i = 0; i < size; i++) {
            int hi = hex_value(hex[i * 2]);
            int lo = hex_value(hex[i * 2 + 1]);
            content[i] = (char)((hi << 4) | lo);
        }
        content[size] = '\0';
        create_file_internal(fs, name, content, 0, 0);
        free(content);
    }
    fclose(in);
}

static void format_fs(MiniFileSystem *fs, int save_image, int announce)
{
    memset(fs->directory, 0, sizeof(fs->directory));
    memset(fs->blocks, 0, sizeof(fs->blocks));
    for (int i = 0; i < BLOCK_COUNT; i++) {
        fs->fat[i] = FAT_FREE;
    }
    if (save_image) {
        save_fs(fs);
    }
    if (announce) {
        printf("文件系统已格式化，块数=%d，块大小=%d 字节。\n", BLOCK_COUNT, BLOCK_SIZE);
    }
}

static void init_fs(MiniFileSystem *fs, const char *path)
{
    fs->image_path = path;
    format_fs(fs, 0, 0);
    load_fs(fs);
}

static int create_file_internal(MiniFileSystem *fs, const char *name, const char *content,
                                int save_image, int announce)
{
    int entry;
    int allocated[BLOCK_COUNT];
    int allocated_count;
    int content_size = (int)strlen(content);

    if (!valid_name(name)) {
        printf("文件名不能为空，且长度不能超过 24。\n");
        return 0;
    }
    if (content_size > MAX_CONTENT) {
        printf("创建失败：内容超过虚拟磁盘容量。\n");
        return 0;
    }
    if (find_file(fs, name) != -1) {
        printf("创建失败：文件已存在。\n");
        return 0;
    }
    entry = find_free_entry(fs);
    if (entry == -1) {
        printf("创建失败：目录项已满。\n");
        return 0;
    }
    if (!allocate_blocks(fs, content_size, allocated, &allocated_count)) {
        printf("创建失败：空闲块不足。\n");
        return 0;
    }

    snprintf(fs->directory[entry].name, NAME_LEN, "%s", name);
    fs->directory[entry].size = content_size;
    fs->directory[entry].first_block = allocated_count == 0 ? FAT_FREE : allocated[0];
    fs->directory[entry].used = 1;
    write_content_to_blocks(fs, content, content_size, allocated, allocated_count);
    if (save_image) {
        save_fs(fs);
    }
    if (announce) {
        printf("已创建文件 %s，大小 %d 字节，占用块数 %d。\n",
               name, content_size, allocated_count);
    }
    return 1;
}

static int write_file(MiniFileSystem *fs, const char *name, const char *content)
{
    int idx = find_file(fs, name);
    int old_chain[BLOCK_COUNT];
    int old_count;
    int needed;
    int allocated[BLOCK_COUNT];
    int allocated_count;
    int content_size = (int)strlen(content);

    if (idx == -1) {
        printf("写入失败：文件不存在。\n");
        return 0;
    }
    if (content_size > MAX_CONTENT) {
        printf("写入失败：内容超过虚拟磁盘容量。\n");
        return 0;
    }
    old_count = get_chain(fs, fs->directory[idx].first_block, old_chain);
    needed = content_size == 0 ? 0 : (content_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    if (free_block_count(fs) + old_count < needed) {
        printf("写入失败：空闲块不足，原文件保持不变。\n");
        return 0;
    }

    free_chain(fs, fs->directory[idx].first_block);
    allocate_blocks(fs, content_size, allocated, &allocated_count);
    fs->directory[idx].size = content_size;
    fs->directory[idx].first_block = allocated_count == 0 ? FAT_FREE : allocated[0];
    write_content_to_blocks(fs, content, content_size, allocated, allocated_count);
    save_fs(fs);
    printf("已写入文件 %s，新大小=%d 字节，占用块数 %d。\n",
           name, content_size, allocated_count);
    return 1;
}

static int read_file_cmd(MiniFileSystem *fs, const char *name)
{
    int idx = find_file(fs, name);
    char *content;
    if (idx == -1) {
        printf("读取失败：文件不存在。\n");
        return 0;
    }
    content = read_content(fs, &fs->directory[idx]);
    if (content == NULL) {
        printf("读取失败：内存不足。\n");
        return 0;
    }
    printf("文件 %s 内容:\n%s\n", name, content);
    free(content);
    return 1;
}

static int delete_file(MiniFileSystem *fs, const char *name)
{
    int idx = find_file(fs, name);
    if (idx == -1) {
        printf("删除失败：文件不存在。\n");
        return 0;
    }
    free_chain(fs, fs->directory[idx].first_block);
    memset(&fs->directory[idx], 0, sizeof(fs->directory[idx]));
    save_fs(fs);
    printf("已删除文件 %s，对应磁盘块已释放。\n", name);
    return 1;
}

static void list_files(MiniFileSystem *fs)
{
    int any = 0;
    printf("\n目录表:\n");
    printf("%-24s%-10s%s\n", "文件名", "大小", "块链");
    for (int i = 0; i < MAX_FILES; i++) {
        if (fs->directory[i].used) {
            int chain[BLOCK_COUNT];
            int count = get_chain(fs, fs->directory[i].first_block, chain);
            any = 1;
            printf("%-24s%-10d", fs->directory[i].name, fs->directory[i].size);
            if (count == 0) {
                printf("-");
            } else {
                for (int j = 0; j < count; j++) {
                    printf("%d%s", chain[j], j + 1 < count ? "->" : "");
                }
            }
            printf("\n");
        }
    }
    if (!any) {
        printf("(空目录)\n");
    }
    printf("空闲块数量: %d/%d\n", free_block_count(fs), BLOCK_COUNT);
}

static void show_bitmap(MiniFileSystem *fs)
{
    printf("\n空闲空间位示图（0=空闲，1=占用）:\n");
    for (int i = 0; i < BLOCK_COUNT; i++) {
        putchar(fs->fat[i] == FAT_FREE ? '0' : '1');
        if ((i + 1) % 32 == 0) {
            putchar('\n');
        }
    }
}

static void print_help(void)
{
    printf("\n命令列表:\n");
    printf("format                         格式化虚拟磁盘\n");
    printf("create <文件名> <内容>          创建文件\n");
    printf("write  <文件名> <新内容>        覆盖写文件\n");
    printf("read   <文件名>                 读取文件\n");
    printf("delete <文件名>                 删除文件并释放块\n");
    printf("ls                             查看目录表和块链\n");
    printf("bitmap                         查看空闲空间位示图\n");
    printf("help                           查看帮助\n");
    printf("exit                           退出\n");
}

int main(void)
{
    MiniFileSystem fs;
    char line[MAX_CONTENT + 128];
    init_fs(&fs, "virtual_disk.img");
    printf("简易文件系统实验，虚拟磁盘镜像: virtual_disk.img\n");
    print_help();

    while (1) {
        char *command;
        printf("fs> ");
        if (fgets(line, sizeof(line), stdin) == NULL) {
            break;
        }
        line[strcspn(line, "\r\n")] = '\0';
        command = strtok(line, " \t");
        if (command == NULL) {
            continue;
        }

        if (strcmp(command, "format") == 0) {
            format_fs(&fs, 1, 1);
        } else if (strcmp(command, "create") == 0) {
            char *name = strtok(NULL, " \t");
            char *content = trim_left(strtok(NULL, ""));
            create_file_internal(&fs, name, content == NULL ? "" : content, 1, 1);
        } else if (strcmp(command, "write") == 0) {
            char *name = strtok(NULL, " \t");
            char *content = trim_left(strtok(NULL, ""));
            write_file(&fs, name, content == NULL ? "" : content);
        } else if (strcmp(command, "read") == 0) {
            char *name = strtok(NULL, " \t");
            read_file_cmd(&fs, name);
        } else if (strcmp(command, "delete") == 0) {
            char *name = strtok(NULL, " \t");
            delete_file(&fs, name);
        } else if (strcmp(command, "ls") == 0) {
            list_files(&fs);
        } else if (strcmp(command, "bitmap") == 0) {
            show_bitmap(&fs);
        } else if (strcmp(command, "help") == 0) {
            print_help();
        } else if (strcmp(command, "exit") == 0) {
            break;
        } else {
            printf("未知命令，输入 help 查看可用命令。\n");
        }
    }
    return 0;
}
