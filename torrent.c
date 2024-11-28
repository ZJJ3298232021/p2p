#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "p2p.h"
#include "torrent.h"

void parse_torrent_file(const char *torrent_file, TorrentFile *torrent) {
    FILE *file = fopen(torrent_file, "r");
    if (!file) {
        perror("Failed to open torrent file");
        exit(1);
    }

    char line[256];
    while (fgets(line, sizeof(line), file)) {
        if (strncmp(line, "file_name=", 10) == 0) {
            strncpy(torrent->file_name, line + 10, sizeof(torrent->file_name) - 1);
            torrent->file_name[strcspn(torrent->file_name, "\n")] = '\0'; // 去掉换行符
        } else if (strncmp(line, "num_blocks=", 11) == 0) {
            torrent->num_blocks = atoi(line + 11);
        } else if (strncmp(line, "file_size=", 10) == 0) {
            torrent->file_size = atol(line + 10); // 读取文件大小
        }
    }

    fclose(file);
}

void split_file_into_blocks(const char *file_path, FileBlocks *blocks) {
    FILE *file = fopen(file_path, "rb");
    if (!file) {
        perror("Error opening file");
        exit(1);
    }

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file); // 获取文件大小
    fseek(file, 0, SEEK_SET);

    long block_size = file_size / BLOCK_NUM; // 每块的固定大小
    long remaining_size = file_size % BLOCK_NUM; // 最后一块的额外大小

    if (blocks) {
        blocks->total_blocks = BLOCK_NUM; // 固定块数量
    }

    BYTE buffer[block_size + remaining_size];
    for (int i = 0; i < BLOCK_NUM; i++) {
        size_t bytes_to_read = block_size + (i == BLOCK_NUM - 1 ? remaining_size : 0); // 最后一块包含余数
        size_t bytes_read = fread(buffer, 1, bytes_to_read, file);

        // 如果需要额外处理块数据，可以在这里扩展逻辑
    }

    fclose(file);
}

void create_torrent_file(const char *file_path, const char *tracker_ip, int tracker_port, const char *output_file) {
    TorrentFile torrent;

    // 提取文件名部分（去掉路径）
    const char *file_name = strrchr(file_path, '/');
    if (!file_name) {
        file_name = file_path; // 如果没有路径分隔符，直接使用整个路径
    } else {
        file_name++; // 跳过路径分隔符
    }

    strncpy(torrent.file_name, file_name, sizeof(torrent.file_name) - 1);
    torrent.file_name[sizeof(torrent.file_name) - 1] = '\0';

    // 获取文件大小
    FILE *file = fopen(file_path, "rb");
    if (!file) {
        perror("Error opening file");
        exit(1);
    }

    fseek(file, 0, SEEK_END);
    torrent.file_size = ftell(file);
    fclose(file);

    FileBlocks blocks;
    split_file_into_blocks(file_path, &blocks);
    torrent.num_blocks = blocks.total_blocks;

    FILE *torrent_file = fopen(output_file, "w");
    if (!torrent_file) {
        perror("Error creating torrent file");
        exit(1);
    }

    fprintf(torrent_file, "file_name=%s\n", torrent.file_name); // 只写入文件名
    fprintf(torrent_file, "tracker_ip=%s\n", tracker_ip);
    fprintf(torrent_file, "tracker_port=%d\n", tracker_port);
    fprintf(torrent_file, "num_blocks=%d\n", torrent.num_blocks);
    fprintf(torrent_file, "file_size=%ld\n", torrent.file_size); // 添加文件大小

    fflush(torrent_file); // 确保写入磁盘
    fclose(torrent_file);

    printf("Torrent file created: %s\n", output_file);
}
