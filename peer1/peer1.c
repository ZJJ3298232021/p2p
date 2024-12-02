#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include "../p2p.h"
#include "../torrent.h"

// 检查文件是否为普通文件
int is_regular_file(const char *path) {
    struct stat path_stat;
    stat(path, &path_stat);
    return S_ISREG(path_stat.st_mode);
}

// 连接到 Tracker
int connect_to_tracker(const char *tracker_ip, int tracker_port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Socket creation failed");
        exit(1);
    }

    struct sockaddr_in tracker_addr;
    memset(&tracker_addr, 0, sizeof(tracker_addr));
    tracker_addr.sin_family = AF_INET;
    tracker_addr.sin_port = htons(tracker_port);
    inet_pton(AF_INET, tracker_ip, &tracker_addr.sin_addr);

    if (connect(sock, (struct sockaddr *) &tracker_addr, sizeof(tracker_addr)) < 0) {
        perror("Connection to tracker failed");
        close(sock);
        exit(1);
    }

    return sock;
}

// 注册到 Tracker
void register_with_tracker(const char *tracker_ip, int tracker_port, const char *peer_ip, int peer_port) {
    int sock = connect_to_tracker(tracker_ip, tracker_port);

    int request_type = 1; // 注册请求类型
    send(sock, (const char *) &request_type, sizeof(request_type), 0);

    PeerInfo peer_info;
    strcpy(peer_info.peer_ip, peer_ip);
    peer_info.peer_port = peer_port;
    peer_info.file_count = 0;

    send(sock, (const char *) &peer_info, sizeof(peer_info), 0);

    char response[64];
    recv(sock, response, sizeof(response), 0);
    printf("Tracker response: %s\n", response);

    close(sock);
}

// 添加文件到 Tracker
void add_files_to_tracker(const char *tracker_ip, int tracker_port, const char *peer_ip, int peer_port,
                          const char *directory) {
    DIR *dir = opendir(directory);
    if (!dir) {
        perror("Failed to open directory");
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        char file_path[512];
        snprintf(file_path, sizeof(file_path), "%s/%s", directory, entry->d_name);

        if (is_regular_file(file_path)) {
            int sock = connect_to_tracker(tracker_ip, tracker_port);

            int request_type = 2; // 添加文件请求类型
            send(sock, (const char *) &request_type, sizeof(request_type), 0);

            send(sock, peer_ip, 16, 0);
            send(sock, (const char *) &peer_port, sizeof(peer_port), 0);

            TorrentFile torrent_file;
            strncpy(torrent_file.file_name, entry->d_name, sizeof(torrent_file.file_name) - 1);
            torrent_file.file_name[sizeof(torrent_file.file_name) - 1] = '\0';
            torrent_file.num_blocks = 1; // 简化为 1 个块

            send(sock, (const char *) &torrent_file, sizeof(torrent_file), 0);

            char response[64];
            recv(sock, response, sizeof(response), 0);
            printf("Tracker response: %s\n", response);

            close(sock);

            char torrent_path[512];
            snprintf(torrent_path, sizeof(torrent_path), "peer1/%s.torrent", entry->d_name);
            create_torrent_file(file_path, tracker_ip, tracker_port, torrent_path);
            printf("Torrent file created: %s\n", torrent_path);
        }
    }

    closedir(dir);
}

// 文件传输处理
void handle_file_transfer(int client_sock) {
    struct {
        char file_name[256];
        int block_index;
    } request;

    // 接收固定大小的请求结构
    if (recv(client_sock, &request, sizeof(request), 0) <= 0) {
        perror("Failed to receive file transfer request");
        return;
    }

    char file_name[256];
    int block_index = request.block_index;
    strncpy(file_name, request.file_name, sizeof(file_name));

    // 构建文件的完整路径
    char file_path[512];
    snprintf(file_path, sizeof(file_path), "peer1/files/%s", file_name);

    printf("Peer received request for file: %s, block: %d\n", file_path, block_index);

    FILE *file = fopen(file_path, "rb");
    if (!file) {
        perror("File not found");
        send(client_sock, "ERROR: File not found", 21, 0);
        return;
    }

    // 获取文件大小
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    // 计算分片大小
    long block_size = file_size / BLOCK_NUM;
    long remaining_size = file_size % BLOCK_NUM;
    long current_block_size = block_index == BLOCK_NUM - 1 ? block_size + remaining_size : block_size;

    // 跳转到指定分片的起始位置
    fseek(file, block_index * block_size, SEEK_SET);

    // 分配缓冲区
    char *buffer = (char *)malloc(current_block_size);
    if (!buffer) {
        perror("Failed to allocate buffer");
        fclose(file);
        return;
    }

    // 读取并发送分片数据
    size_t bytes_read = fread(buffer, 1, current_block_size, file);
    if (bytes_read > 0) {
        if (send(client_sock, buffer, bytes_read, 0) < 0) {
            perror("Error sending file block");
        } else {
            printf("Sent block %d of size %ld bytes for file %s\n", block_index, bytes_read, file_name);
        }
    } else {
        perror("Error reading file block");
    }

    // 清理
    free(buffer);
    fclose(file);
}


// 启动 Peer 监听
void start_peer_listener(int peer_port) {
    int server_sock, client_sock;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);

    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        perror("Socket creation failed");
        exit(1);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(peer_port);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_sock, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(server_sock);
        exit(1);
    }

    if (listen(server_sock, 10) < 0) {
        perror("Listen failed");
        close(server_sock);
        exit(1);
    }

    printf("Peer is listening on port %d...\n", peer_port);

    while (1) {
        client_sock = accept(server_sock, (struct sockaddr *) &client_addr, &client_len);
        if (client_sock < 0) {
            perror("Accept failed");
            continue;
        }

        handle_file_transfer(client_sock);
        close(client_sock);
    }

    close(server_sock);
}

// 主函数
int main() {
#ifdef _WIN32
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        perror("WSAStartup failed");
        exit(1);
    }
#endif

    const char *tracker_ip = "192.168.216.235"; // Tracker 的 IP
    int tracker_port = TRACKER_PORT;     // Tracker 的端口
    const char *peer_ip = "192.168.216.148";   // Peer 的 IP
    int peer_port = 9006;                // 当前 Peer 的端口
    const char *files_directory = "peer1/files"; // 文件目录

    printf("Peer starting: %s:%d\n", peer_ip, peer_port);

    // 注册到 Tracker
    register_with_tracker(tracker_ip, tracker_port, peer_ip, peer_port);

    // 添加文件到 Tracker 并生成 Torrent 文件
    add_files_to_tracker(tracker_ip, tracker_port, peer_ip, peer_port, files_directory);

    // 启动监听
    start_peer_listener(peer_port);

#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
