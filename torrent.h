#ifndef TORRENT_H
#define TORRENT_H

#include "p2p.h"  // 引入公共结构定义

/* Torrent 文件操作 */
void create_torrent_file(const char *file_path, const char *tracker_ip, int tracker_port, const char *output_file); // 创建 Torrent 文件
void parse_torrent_file(const char *torrent_file, TorrentFile *torrent); // 解析 Torrent 文件

#endif // TORRENT_H
