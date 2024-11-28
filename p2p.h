#ifndef P2P_H
#define P2P_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")  // Windows 需要链接 Winsock 库
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#endif

#define MAX_PEERS 10           // 最大支持的 Peer 数量
#define MAX_FILES 100          // 每个 Peer 最大共享文件数量
#define MAX_BLOCKS 1000        // 每个文件的最大分块数量
#define BLOCK_NUM 12           // 固定分块数量
#define BLOCK_SIZE 512         // 分块大小
#define TRACKER_PORT 8080      // Tracker 监听的端口

// 表示一个 Torrent 文件的信息
typedef struct {
    char file_name[256];       // 文件名
    int num_blocks;            // 文件的分块数
    long file_size;
} TorrentFile;

// 表示一个文件的分块信息
typedef struct {
    int total_blocks;          // 分块总数
} FileBlocks;

// 表示一个 Peer 的信息
typedef struct {
    char peer_ip[16];          // Peer 的 IP 地址
    int peer_port;             // Peer 的端口
    TorrentFile files[MAX_FILES]; // 该 Peer 共享的文件
    int file_count;            // 共享文件的数量
} PeerInfo;

// 表示 Tracker 中记录的所有 Peers 信息
typedef struct {
    PeerInfo peers[MAX_PEERS]; // 所有注册的 Peer 信息
    int peer_count;            // 已注册的 Peer 数量
} TrackerInfo;

#endif // P2P_H
