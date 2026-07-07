#ifndef PROTOCOL_DEF_H
#define PROTOCOL_DEF_H

#include <stdint.h>

// --- 基础常量定义 ---
#define FRAME_HEAD  0xAA
#define FRAME_TAIL  0x55

// --- 指令类型 (功能码) ---
enum CmdType {
    CMD_HEARTBEAT = 0x00,
    CMD_MOVE      = 0x01,
    CMD_CONTROL   = 0x02,
    CMD_TARGET    = 0x03  // 新增：下发视觉锁定的草心坐标
};

#pragma pack(push, 1)

struct FrameHeader {
    uint8_t header;
    uint8_t type;
    uint8_t len;
};

struct MovePayload {
    int16_t vx;
    int16_t vy;
    int16_t vz;
};

struct ControlPayload {
    uint8_t led_switch;
    uint8_t buzzer_switch;
    uint8_t mode;
    uint8_t padding;
};

// 新增：坐标载荷
struct TargetPayload {
    int16_t x;
    int16_t y;
};

#pragma pack(pop)

#endif // PROTOCOL_DEF_H
