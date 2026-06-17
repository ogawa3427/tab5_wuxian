#pragma once

#include <cstdint>

static constexpr uint8_t kPacketMagic = 0xA5;
static constexpr uint16_t kUdpPort = 1234;

#pragma pack(push, 1)
struct ImuPacket {
    uint8_t magic;
    uint8_t seq;
    float accel_x;
    float accel_y;
    float accel_z;
    float gyro_x;
    float gyro_y;
    float gyro_z;
    uint32_t uptime_ms;
};
#pragma pack(pop)

static_assert(sizeof(ImuPacket) == 30, "ImuPacket size mismatch");
