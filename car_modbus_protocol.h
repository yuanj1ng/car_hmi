#ifndef CAR_MODBUS_PROTOCOL_H
#define CAR_MODBUS_PROTOCOL_H
#include <QString>

enum CarModbusMap : unsigned int {

    Addr_PowerEnable      = 0,
    Addr_DirectionForward = 1,
    Addr_DirectionBackward= 2,
    Addr_DirectionLeft    = 3,
    Addr_DirectionRight   = 4,
    Addr_LaserSwitch      = 10,
    Addr_RadiatorSwitch   = 11,
    Addr_CameraSwitch     = 12,
    Addr_StrobeSwitch     = 13,
    Addr_LaserFire        = 20,
};

enum CarRegisterAddr : unsigned int {
    Reg_SpeedSet       = 0,  // 对应 PLC 的 40001
    Reg_CurrentSpeed   = 1,  // 对应 PLC 的 40002
    Reg_LaserTargetX   = 2,  // 对应 PLC 的 40003
    Reg_LaserTargetY   = 3,  // 对应 PLC 的 40004
};

const QString CarModbus_IP   = "127.0.0.1";
const int     CarModbus_Port = 502;
const int     CarServerID = 1;

#endif // CAR_MODBUS_PROTOCOL_H
