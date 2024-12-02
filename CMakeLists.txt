cmake_minimum_required(VERSION 3.29)
project(p2p C)

# 设置 C 标准
set(CMAKE_C_STANDARD 11)

# 添加源文件到可执行文件
add_executable(p2p
        p2p.h
        tracker/tracker.c
        peer1/peer1.c
        peer2/peer2.c
        peer3/peer3.c
        peer4/peer4.c
        torrent.c
        torrent.h
        Test.c
        Test.c
)
