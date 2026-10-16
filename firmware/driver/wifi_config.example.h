/*
 * WiFi 与云端部署参数模板
 *
 * 使用步骤：
 *   1. 把本文件复制为同目录下的 wifi_config.h
 *   2. 填入自己的 WiFi 名称、密码，以及运行云端 server.py 那台电脑的 IP
 *   3. wifi_config.h 已被 .gitignore 忽略，不会提交，凭证不会外泄
 *
 * 说明：本仓库所有源码均为 UTF-8 编码，保存时请保持 UTF-8。
 */
#ifndef __WIFI_CONFIG_H
#define __WIFI_CONFIG_H

/* 柜机要接入的 WiFi 名称。建议用手机热点或电脑热点，确保与服务器同网段 */
#define ESP_SSID     "YOUR_WIFI_SSID"

/* WiFi 密码 */
#define ESP_PASS     "YOUR_WIFI_PASSWORD"

/* 运行云端 cloud/server.py 的那台电脑的 IP。
   若使用 Windows 移动热点，网关通常是 192.168.137.1 */
#define ESP_TCP_IP   "192.168.137.1"

/* 云端柜机 TCP 服务端口，需与 cloud/server.py 中柜机服务监听端口一致 */
#define ESP_TCP_PORT 8090

#endif
