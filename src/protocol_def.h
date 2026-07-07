#ifndef PROTOCOL_DEF_H
#define PROTOCOL_DEF_H

#include <stdint.h>

// 强制 1 字节对齐，防止不同平台内存补齐导致数据错乱
#pragma pack(push, 1)

// 指令类型枚举
enum CommandType {
    CMD_HEARTBEAT = 0x00, // 心跳包
    CMD_MOVE      = 0x01, // 移动控制 (vx, vy, vz)
    CMD_CONTROL   = 0x02, // 硬件控制 (LED, 蜂鸣器)
    CMD_TARGET    = 0x03  // AI 目标坐标下发
};

// 1. 移动载荷：对应麦克纳姆轮的三个自由度
struct MovePayload {
    float vx; // 前后速度 (正前负后)
    float vy; // 左右平移 (麦轮专用)
    float vz; // 自转角速度
};

// 2. 硬件控制载荷
struct ControlPayload {
    uint8_t mode;          // 0:手动, 1:自动
    uint8_t led_switch;    // 1:开, 0:关
    uint8_t buzzer_switch; // 1:鸣叫, 0:静音
    uint8_t padding;       // 字节对齐占位
};

// 3. 统一帧头
struct FrameHeader {
    uint8_t  header; // 固定为 0x5A
    uint8_t  type;   // 指令类型 (CommandType)
    uint16_t len;    // 后续载荷长度
};

#pragma pack(pop)
#endif