#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib") // Windows 需要链接 Winsock 库
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#endif

#include "../p2p.h"

// 全局 Tracker 数据
TrackerInfo tracker;

// 初始化 Tracker 数据
void initialize_tracker() {
    memset(&tracker, 0, sizeof(tracker));
    printf("Tracker initialized.\n");
}

// 处理 Peer 注册请求
void handle_register(int client_sock) {
    PeerInfo new_peer;
    if (recv(client_sock, &new_peer, sizeof(new_peer), 0) <= 0) {
        perror("Failed to receive peer data");
        return;
    }

    if (tracker.peer_count >= MAX_PEERS) {
        printf("Tracker is full, cannot register more peers.\n");
        send(client_sock, "ERROR: Tracker full", 20, 0);
        return;
    }

    for (int i = 0; i < tracker.peer_count; i++) {
        if (strcmp(tracker.peers[i].peer_ip, new_peer.peer_ip) == 0 &&
            tracker.peers[i].peer_port == new_peer.peer_port) {
            printf("Peer %s:%d is already registered.\n", new_peer.peer_ip, new_peer.peer_port);
            send(client_sock, "ERROR: Already registered", 26, 0);
            return;
        }
    }

    tracker.peers[tracker.peer_count++] = new_peer;
    printf("Peer %s:%d registered successfully.\n", new_peer.peer_ip, new_peer.peer_port);
    send(client_sock, "OK", 2, 0);
}

// 处理文件添加请求
void handle_add_file(int client_sock) {
    char peer_ip[16];
    int peer_port;
    TorrentFile new_file;

    if (recv(client_sock, peer_ip, sizeof(peer_ip), 0) <= 0 ||
        recv(client_sock, &peer_port, sizeof(peer_port), 0) <= 0 ||
        recv(client_sock, &new_file, sizeof(new_file), 0) <= 0) {
        perror("Failed to receive file data");
        return;
    }

    for (int i = 0; i < tracker.peer_count; i++) {
        if (strcmp(tracker.peers[i].peer_ip, peer_ip) == 0 &&
            tracker.peers[i].peer_port == peer_port) {

            for (int j = 0; j < tracker.peers[i].file_count; j++) {
                if (strcmp(tracker.peers[i].files[j].file_name, new_file.file_name) == 0) {
                    printf("File %s already exists for peer %s:%d\n",
                           new_file.file_name, peer_ip, peer_port);
                    send(client_sock, "ERROR: File already exists", 27, 0);
                    return;
                }
            }

            tracker.peers[i].files[tracker.peers[i].file_count++] = new_file;
            printf("File %s added for peer %s:%d\n", new_file.file_name, peer_ip, peer_port);
            send(client_sock, "OK", 2, 0);
            return;
        }
    }

    printf("Peer %s:%d not found. Please register first.\n", peer_ip, peer_port);
    send(client_sock, "ERROR: Peer not registered", 27, 0);
}

// 处理文件查询请求
void handle_query_file(int client_sock) {
    char file_name[256];
    if (recv(client_sock, file_name, sizeof(file_name), 0) <= 0) {
        perror("Failed to receive file name");
        return;
    }

    PeerInfo result[MAX_PEERS];
    int count = 0;
    for (int i = 0; i < tracker.peer_count; i++) {
        for (int j = 0; j < tracker.peers[i].file_count; j++) {
            if (strcmp(tracker.peers[i].files[j].file_name, file_name) == 0) {
                result[count++] = tracker.peers[i];
                break;
            }
        }
    }

    send(client_sock, &count, sizeof(count), 0);
    if (count > 0) {
        send(client_sock, result, sizeof(PeerInfo) * count, 0);
    }

    printf("File query: %s, found %d peers.\n", file_name, count);
}

// 主函数：启动 Tracker 并监听请求
int main() {
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        fprintf(stderr, "Failed to initialize Winsock.\n");
        return 1;
    }
#endif

    initialize_tracker();

    int server_sock, client_sock;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);

    // 创建套接字
    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
#ifdef _WIN32
        fprintf(stderr, "Socket creation failed: %d\n", WSAGetLastError());
#else
        perror("Socket creation failed");
#endif
        return 1;
    }

    // 配置服务器地址
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(TRACKER_PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    // 绑定套接字
    if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(server_sock);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    // 监听连接
    if (listen(server_sock, 10) < 0) {
        perror("Listen failed");
        close(server_sock);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    printf("Tracker is listening on port %d...\n", TRACKER_PORT);

    while (1) {
        client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &client_len);
        if (client_sock < 0) {
            perror("Accept failed");
            continue;
        }

        int request_type;
        if (recv(client_sock, &request_type, sizeof(request_type), 0) <= 0) {
            perror("Failed to receive request type");
            close(client_sock);
            continue;
        }

        switch (request_type) {
            case 1:
                handle_register(client_sock);
                break;
            case 2:
                handle_add_file(client_sock);
                break;
            case 3:
                handle_query_file(client_sock);
                break;
            default:
                printf("Unknown request type: %d\n", request_type);
                send(client_sock, "ERROR: Unknown request", 23, 0);
                break;
        }

        close(client_sock);
    }

    close(server_sock);

#ifdef _WIN32
    WSACleanup();
#endif

    return 0;
}
