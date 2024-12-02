#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include "../p2p.h"
#include "../torrent.h"
#include <pthread.h>
#define MAX_BLOCKS 1024

typedef struct {
    PeerInfo *peer;         // 当前 Peer 信息
    TorrentFile *torrent;   // Torrent 文件信息
    int *blocks;            // 负责的块集合
    int num_blocks;         // 负责块的数量
    char *pieces_dir;       // 块存储目录
    long file_size;         // 文件大小
    int *block_downloaded;  // 块下载状态
} ThreadData;

// Function to calculate the size of each block dynamically
long calculate_block_size(long file_size, int total_blocks, int block_index) {
    long base_size = file_size / total_blocks;
    long remaining = file_size % total_blocks;
    return block_index == total_blocks - 1 ? base_size + remaining : base_size;
}

PeerInfo *query_peers_for_file(const char *tracker_ip, int tracker_port, const char *file_name, int *peer_count) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Socket creation failed");
        return NULL;
    }

    struct sockaddr_in tracker_addr;
    memset(&tracker_addr, 0, sizeof(tracker_addr));
    tracker_addr.sin_family = AF_INET;
    tracker_addr.sin_port = htons(tracker_port);
    inet_pton(AF_INET, tracker_ip, &tracker_addr.sin_addr);

    if (connect(sock, (struct sockaddr *)&tracker_addr, sizeof(tracker_addr)) < 0) {
        perror("Connection to tracker failed");
        close(sock);
        return NULL;
    }

    int request_type = 3; // 文件查询请求类型
    send(sock, &request_type, sizeof(request_type), 0);
    send(sock, file_name, strlen(file_name) + 1, 0);

    recv(sock, peer_count, sizeof(*peer_count), 0);

    PeerInfo *peers = NULL;
    if (*peer_count > 0) {
        peers = (PeerInfo *)malloc(sizeof(PeerInfo) * (*peer_count));
        recv(sock, peers, sizeof(PeerInfo) * (*peer_count), 0);
    }

    close(sock);
    return peers;
}

// Function to download block from peer
int download_block_from_peer(const PeerInfo *peer, const TorrentFile *torrent, int block_index, char *block_path, long block_size) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Socket creation failed");
        return 0;
    }

    struct sockaddr_in peer_addr;
    memset(&peer_addr, 0, sizeof(peer_addr));
    peer_addr.sin_family = AF_INET;
    peer_addr.sin_port = htons(peer->peer_port);
    inet_pton(AF_INET, peer->peer_ip, &peer_addr.sin_addr);

    if (connect(sock, (struct sockaddr *)&peer_addr, sizeof(peer_addr)) < 0) {
        perror("Connection to peer failed");
        close(sock);
        return 0;
    }

    struct {
        char file_name[256];
        int block_index;
    } request;

    strncpy(request.file_name, torrent->file_name, sizeof(request.file_name));
    request.block_index = block_index;

    if (send(sock, &request, sizeof(request), 0) < 0) {
        perror("Failed to send file request");
        close(sock);
        return 0;
    }

    FILE *block_file = fopen(block_path, "wb");
    if (!block_file) {
        perror("Failed to create block file");
        close(sock);
        return 0;
    }

    char *buffer = (char *)malloc(block_size);
    if (!buffer) {
        perror("Failed to allocate buffer");
        fclose(block_file);
        close(sock);
        return 0;
    }

    int received = recv(sock, buffer, block_size, 0);
    if (received > 0) {
        fwrite(buffer, 1, received, block_file);
    }

    free(buffer);
    fclose(block_file);
    close(sock);

    return received > 0 ? 1 : 0;
}

// Thread function
void *thread_function(void *arg) {
    ThreadData *data = (ThreadData *)arg;

    for (int i = 0; i < data->num_blocks; i++) {
        int block_index = data->blocks[i];
        char block_path[512];
        snprintf(block_path, sizeof(block_path), "%s/block_%d", data->pieces_dir, block_index);

        long block_size = calculate_block_size(data->file_size, data->torrent->num_blocks, block_index);
        if (download_block_from_peer(data->peer, data->torrent, block_index, block_path, block_size)) {
            data->block_downloaded[block_index] = 1;
        } else {
            printf("Peer %d: Failed to download block %d\n", data->peer->peer_port, block_index);
        }
    }

    pthread_exit(NULL);
}

void merge_blocks(const TorrentFile *torrent, const char *pieces_dir, const char *output_dir, long file_size) {
    char output_path[512];
    snprintf(output_path, sizeof(output_path), "%s/%s", output_dir, torrent->file_name);

    FILE *output = fopen(output_path, "wb");
    if (!output) {
        perror("Error creating output file");
        return;
    }

    for (int i = 0; i < torrent->num_blocks; i++) {
        char block_path[512];
        snprintf(block_path, sizeof(block_path), "%s/block_%d", pieces_dir, i);

        FILE *block_file = fopen(block_path, "rb");
        if (!block_file) {
            perror("Error opening block file");
            fclose(output);
            return;
        }

        long block_size = calculate_block_size(file_size, torrent->num_blocks, i);
        char *buffer = (char *)malloc(block_size);
        if (!buffer) {
            perror("Failed to allocate buffer");
            fclose(block_file);
            fclose(output);
            return;
        }

        size_t bytes_read = fread(buffer, 1, block_size, block_file);
        fwrite(buffer, 1, bytes_read, output);

        free(buffer);
        fclose(block_file);
    }

    fclose(output);
    printf("File %s downloaded and merged successfully.\n", torrent->file_name);
}
void process_torrent_files(const char *directory, const char *tracker_ip, int tracker_port) {
    DIR *dir = opendir(directory);
    if (!dir) {
        perror("Failed to open directory");
        return;
    }

    char pieces_dir[512];
    snprintf(pieces_dir, sizeof(pieces_dir), "%s/pieces", directory);
    mkdir(pieces_dir);

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strstr(entry->d_name, ".torrent")) {
            char torrent_path[512];
            snprintf(torrent_path, sizeof(torrent_path), "%s/%s", directory, entry->d_name);

            TorrentFile torrent;
            parse_torrent_file(torrent_path, &torrent);
            printf("Parsed Torrent File: %s\n", torrent.file_name);

            int peer_count;
            // 这个是从Tracker中返回的能够提供该服务的peers列表
            PeerInfo *peers = query_peers_for_file(tracker_ip, tracker_port, torrent.file_name, &peer_count);

            if (!peers || peer_count == 0) {
                printf("No peers found with file %s.\n", torrent.file_name);
                free(peers);
                continue;
            }

            printf("Found %d peers with file %s.\n", peer_count, torrent.file_name);

            long file_size = torrent.file_size;
            int block_downloaded[MAX_BLOCKS] = {0};

            pthread_t threads[peer_count];
            ThreadData thread_data[peer_count];

            // 初始化块分配数据
            int *peer_blocks[peer_count];
            int blocks_per_peer[peer_count];
            memset(blocks_per_peer, 0, sizeof(blocks_per_peer));

            for (int i = 0; i < peer_count; i++) {
                peer_blocks[i] = malloc(sizeof(int) * (torrent.num_blocks / peer_count + 1));
            }

            // 按照块编号分配给对应的 Peer
            for (int i = 0; i < torrent.num_blocks; i++) {
                int peer_id = i % peer_count; // 根据块编号对 peer_count 取模
                peer_blocks[peer_id][blocks_per_peer[peer_id]++] = i; // 添加到对应 Peer 的块集合中
            }

            // 为每个 Peer 创建线程
            for (int i = 0; i < peer_count; i++) {
                thread_data[i].peer = &peers[i];
                thread_data[i].torrent = &torrent;
                thread_data[i].pieces_dir = pieces_dir;
                thread_data[i].file_size = file_size;
                thread_data[i].block_downloaded = block_downloaded;
                thread_data[i].blocks = peer_blocks[i];
                thread_data[i].num_blocks = blocks_per_peer[i];

                if (pthread_create(&threads[i], NULL, thread_function, &thread_data[i]) != 0) {
                    perror("Failed to create thread");
                }
            }

            // 等待所有线程完成
            for (int i = 0; i < peer_count; i++) {
                pthread_join(threads[i], NULL);
                free(peer_blocks[i]);
            }

            merge_blocks(&torrent, pieces_dir, directory, file_size);
            free(peers);
        }
    }

    closedir(dir);
}

int main() {
#ifdef _WIN32
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        perror("WSAStartup failed");
        exit(1);
    }
#endif

    const char *tracker_ip = "127.0.0.1";
    int tracker_port = TRACKER_PORT;
    const char *torrent_directory = "peer4";

    process_torrent_files(torrent_directory, tracker_ip, tracker_port);

#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
