/* @brief 任务*/
#include "tasks.h"
#include "config.h"
#include "macro.h"
#include "handle.h"
#include "protocol.h"

void Task_Control(void *Parameters) {
    TickType_t LastWakeTime = xTaskGetTickCount();
    // LASER_ON;

    while (1) {
        ControlMode = LEFT_SWITCH_BOTTOM && RIGHT_SWITCH_BOTTOM ? 2 : 1;
        if (ControlMode == 1) {

            // 遥控器模式
            // PsAimEnabled  = LEFT_SWITCH_TOP && RIGHT_SWITCH_TOP;
            FastmoveMode = LEFT_SWITCH_TOP && RIGHT_SWITCH_MIDDLE;
            //            MagzineOpened = LEFT_SWITCH_MIDDLE && RIGHT_SWITCH_TOP;
            FrictEnabled      = 0;
            StirEnabled       = LEFT_SWITCH_BOTTOM && RIGHT_SWITCH_TOP;
            StirSingleShoot   = LEFT_SWITCH_BOTTOM && RIGHT_SWITCH_MIDDLE;
            StirReloadEnabled = LEFT_SWITCH_MIDDLE && RIGHT_SWITCH_MIDDLE;       
            // unused
            // FastShootMode = StirEnabled;
            // PsShootEnabled = 0;
            // SwingMode     = (LEFT_SWITCH_TOP && RIGHT_SWITCH_TOP) ? (HAS_SLIP_RING ? 3 : 4) : 0;
            SafetyMode = LEFT_SWITCH_TOP && RIGHT_SWITCH_TOP;
        } else if (ControlMode == 2) {
            // 键鼠模式
            PsShootEnabled    = 0;
            StirEnabled       = mouseData.pressLeft;
            PsAimEnabled      = mouseData.pressRight;
            StirSingleShoot   = keyboardData.Q;
            StirReloadEnabled = keyboardData.R;
            // 摩擦轮
            if (keyboardData.Z) {
                FrictEnabled = 0;
            } else if (keyboardData.X) {
                FrictEnabled = 0;
            }
            // 弹舱盖
            MagzineOpened = keyboardData.F;
            // 小陀螺
            if (keyboardData.C) {
                SwingMode = 5;
            } else if (keyboardData.V) {
                SwingMode = 7;
            }
            // 高射速模式
            FastShootMode = keyboardData.E;
            // 高速移动模式(关闭底盘功率上限，飞坡用)
            FastmoveMode = keyboardData.Shift;
        }
        // 调试视觉用
        // FrictEnabled   = (remoteData.switchLeft == 2) || (remoteData.switchLeft == 1) && (remoteData.switchRight != 2);
        // PsAimEnabled   = (remoteData.switchLeft == 1) && (remoteData.switchRight != 3);
        // PsShootEnabled = (remoteData.switchLeft == 1) && (remoteData.switchRight == 1);
        vTaskDelayUntil(&LastWakeTime, 5);
    }
    vTaskDelete(NULL);
}

void Task_Can_Send(void *Parameters) {
    TickType_t LastWakeTime = xTaskGetTickCount(); // 时钟
    float      interval     = 0.01;                // 任务运行间隔 s
    int        intervalms   = interval * 1000;     // 任务运行间隔 ms
    while (1) {
        Bridge_Send_Motor(&BridgeData, SafetyMode);
        vTaskDelayUntil(&LastWakeTime, intervalms); // 发送频率
    }
    vTaskDelete(NULL);
}

void Task_Gimbal(void *Parameters) {
    // 任务
    TickType_t LastWakeTime = xTaskGetTickCount(); // 时钟
    float      interval     = 0.005;               // 任务运行间隔 s
    int16_t    intervalms   = interval * 1000;     // 任务运行间隔 ms

    // 反馈值
    float yawAngle, yawSpeed, pitchAngle, pitchSpeed, chassisAngle;

    // 目标值
    float yawAngleTarget            = 0; // 目标Yaw
    float pitchAngleTarget          = 0; // 目标Pitch
    float yawAngleTargetControl     = 0; // 遥控器输入
    float pitchAngleTargetControl   = 0; // 遥控器输入
    float pitchAngleTargetFix       = 0; // 上坡补偿
    float pitchAngleTargetFixStable = 0; // 上坡补偿
    float yawAngleTargetPs          = 0; // 视觉辅助
    float pitchAngleTargetPs        = 0; // 视觉辅助

    // 输出量
    int32_t yawCurrent   = 0;
    int32_t pitchCurrent = 0;

    // Pitch轴斜坡参数
    float pitchRampProgress    = 0;
    float pitchRampStart       = Gyroscope_EulerData.pitch - 90;
    float pitchAngleTargetRamp = 0;

    // 初始化云台PID
    PID_Init(&PID_Cloud_YawAngle, 10, 0, 0, 1000, 10);
    PID_Init(&PID_Cloud_YawSpeed, 10, 0, 0, 4000, 0);
    PID_Init(&PID_Cloud_PitchAngle, 15, 0, 0, 16000, 0);
    PID_Init(&PID_Cloud_PitchSpeed, 75, 0, 0, 16000, 0);

    while (1) {
        // 重置目标
        yawAngleTarget   = 0;
        pitchAngleTarget = 0;

        // 设置反馈
        yawAngle     = -1 * Gyroscope_EulerData.yaw;    // 逆时针为正-1 * Gyroscope_EulerData.yaw;
        yawSpeed     = ImuData.gy / GYROSCOPE_LSB;      // 逆时针为正
        pitchAngle   = Gyroscope_EulerData.pitch - 90;  // 逆时针为正
        pitchSpeed   = -1 * ImuData.gx / GYROSCOPE_LSB; // 逆时针为正
        chassisAngle = -1 * Motor_Pitch.angle + pitchAngle;

        // 遥控器输入角度目标
        if (ControlMode == 1) {
            if (ABS(remoteData.rx) > 30) yawAngleTargetControl -= remoteData.rx / 660.0f * 360 * interval;
            if (ABS(remoteData.ry) > 30) pitchAngleTargetControl -= remoteData.ry / 660.0f * 360 * interval;
        } else {
            yawAngleTargetControl -= mouseData.x * 1.3 * 0.01; // 0.005
            pitchAngleTargetControl += mouseData.y * 0.65 * 0.01;
        }
        MIAO(pitchAngleTargetControl, GIMBAL_PITCH_MIN, GIMBAL_PITCH_MAX);
        yawAngleTarget += yawAngleTargetControl;
        pitchAngleTarget += pitchAngleTargetControl;

        // 视觉辅助
        /*        if (!PsAimEnabled) {
                    yawAngleTargetControl += yawAngleTargetPs;
                    pitchAngleTargetControl += pitchAngleTargetPs;
                    yawAngleTarget += yawAngleTargetPs;
                    pitchAngleTarget += pitchAngleTargetPs;
                    yawAngleTargetPs   = 0;
                    pitchAngleTargetPs = 0;
                } else {
                    yawAngleTargetPs += HostAutoaimData.yaw_angle_diff;
                    pitchAngleTargetPs += HostAutoaimData.pitch_angle_diff;
                }
                MIAO(pitchAngleTargetPs, GIMBAL_PITCH_MIN - pitchAngleTarget, GIMBAL_PITCH_MAX - pitchAngleTarget);
                yawAngleTarget += yawAngleTargetPs;
                pitchAngleTarget += pitchAngleTargetPs;
        */
        // 斜坡补偿
        // pitchAngleTargetFix = -1 * (chassisAngle / 40.0) * (GIMBAL_PITCH_MIN - pitchAngleTarget);
        // FirstOrderLowPassFilter(pitchAngleTargetFix, &pitchAngleTargetFixStable, 200, 20);
        // pitchAngleTarget += pitchAngleTargetFixStable;

        // 限制云台运动范围
        MIAO(pitchAngleTarget, GIMBAL_PITCH_MIN, GIMBAL_PITCH_MAX);

        // 开机时pitch轴匀速抬起
        pitchAngleTargetRamp = RAMP(pitchRampStart, pitchAngleTarget, pitchRampProgress);
        if (pitchRampProgress < 1) {
            pitchRampProgress += 0.005f; // 英雄减速0.01
        }

        // 计算PID
        PID_Calculate(&PID_Cloud_YawAngle, -1 * yawAngleTarget, -1 * Gyroscope_EulerData.yaw); // yawAngleTarget
        PID_Calculate(&PID_Cloud_YawSpeed, PID_Cloud_YawAngle.output, yawSpeed * 1.1);
        PID_Calculate(&PID_Cloud_PitchAngle, pitchAngleTargetRamp, pitchAngle);
        PID_Calculate(&PID_Cloud_PitchSpeed, PID_Cloud_PitchAngle.output, pitchSpeed * 1.1);

        // 输出电流
        yawCurrent   = 20 * PID_Cloud_YawSpeed.output;
        pitchCurrent = PID_Cloud_PitchSpeed.output;

        MIAO(yawCurrent, -12000, 12000);
        MIAO(pitchCurrent, -12000, 12000);

        Motor_Yaw.input    = yawCurrent; // yaw电机反置
        Motor_Pitch.input  = pitchCurrent;
        Motor_Pitch2.input = -1 * pitchCurrent;

        // 调试信息
        //
        // if (pitchAngle < 1 && pitchAngle > -1) {
        //     DebugData.debug5 = Motor_Pitch.position;
        // }
        // DebugData.debug1 = pitchAngle * 1000;
        // DebugData.debug2 = Motor_Pitch.position;
        // DebugData.debug3 = chassisAngle;
        // DebugData.debug4 = Motor_Pitch.angle;
        // DebugData.debug3 = yawAngle;
        // DebugData.debug4 = ImuData.gz;
        // DebugData.debug5 = Motor_Yaw.position;
        // DebugData.debug6 = Motor_Pitch.position;
        // DebugData.debug5 = Motor_Pitch.angle;
        // DebugData.debug6 = PID_Cloud_YawSpeed.output;
        // DebugData.debug7 = yawCurrent;

        // 任务间隔
        vTaskDelayUntil(&LastWakeTime, intervalms);
    }
    vTaskDelete(NULL);
}

void Task_Chassis(void *Parameters) {
    // 任务
    TickType_t LastWakeTime = xTaskGetTickCount(); // 时钟
    float      interval     = 0.005;               // 任务运行间隔 s
    int        intervalms   = interval * 1000;     // 任务运行间隔 ms

    // 底盘运动
    float vx             = 0;
    float vy             = 0;
    float vw             = 0;
    float vwRamp         = 0;
    float vwRampProgress = 0;
    float targetPower;

    // 反馈值
    float motorAngle, motorSpeed;
    float lastMotorAngle = Motor_Yaw.angle;
    float filter[6]      = {0, 0, 0, 0, 0, 0};
    int   filterp        = 0;
    float power          = 0;
    float powerBuffer    = 0;

    // 小陀螺
    float   swingAngle       = 0;
    uint8_t swingModeEnabled = 0;
    float   swingInterval    = 0.25;
    float   swingTimer       = swingInterval;
    int8_t  swingDir         = 1; // 瞬时针猫猫步

    // 底盘跟随PID
    float followDeadRegion = 3.0;
    PID_Init(&PID_Follow_Angle, 1, 0, 0, 900, 100);
    PID_Init(&PID_Follow_Speed, 10, 0, 0, 900, 0);

    // 麦轮速度PID
    PID_Init(&PID_LFCM, 20, 0, 0, 6000, 1200);
    PID_Init(&PID_LBCM, 20, 0, 0, 6000, 1200);
    PID_Init(&PID_RBCM, 20, 0, 0, 6000, 1200);
    PID_Init(&PID_RFCM, 20, 0, 0, 6000, 1200);

    // 初始化底盘
    Chassis_Init(&ChassisData);

    // 底盘运动斜坡函数
    float xRampProgress = 0;
    float xRampStart    = 0;
    float xTargetRamp   = 0;
    float yRampProgress = 0;
    float yRampStart    = 0;
    float yTargetRamp   = 0;

    while (1) {
        // 设置反馈值
        motorAngle  = Motor_Yaw.angle;                                 // 电机角度
        motorSpeed  = Motor_Yaw.speed * RPM2RPS;                       // 电机角速度
        power       = ProtocolData.powerHeatData.chassis_power;        // 裁判系统功率
        powerBuffer = ProtocolData.powerHeatData.chassis_power_buffer; // 裁判系统功率缓冲

        // 视觉专属follow PID
        if (PsAimEnabled) {
            PID_Follow_Angle.p = 1.5;
        } else {
            PID_Follow_Angle.p = 1.3;
        }

        // 底盘跟随死区
        if (SwingMode) {
            followDeadRegion = 0; // 关闭底盘跟随死区
        } else {
            followDeadRegion = 1; // 开启底盘跟随死区
        }

        // 小陀螺
        if (SwingMode == 1) {
            swingModeEnabled = 1;
            swingInterval    = 0.45;
            // 先转45后90转动
            swingTimer += interval;
            if (swingAngle == 0) {
                swingAngle = 45;
                // swingTimer = -0.2;
            } else if (swingTimer >= swingInterval) {
                swingAngle += 90;
                swingTimer = 0;
            }
        } else if (SwingMode == 2) {
            swingModeEnabled = 1;
            swingInterval    = 0.3;
            // 不规律旋转
            swingTimer += interval;
            if (swingTimer >= swingInterval) {
                swingAngle += rand() % 40 + 50;
                swingTimer = 0;
            }
        } else if (SwingMode == 3) {
            swingModeEnabled = 1;
            // 匀速旋转
            swingAngle += 450 * interval; // 英雄陀螺减速500
        } else if (SwingMode == 4) {
            swingModeEnabled = 1; // 猫猫步
            swingInterval    = 0.40;
            // 先顺时旋转至45度位置, 后逆时旋转到-45度位置
            swingTimer += interval;
            if (swingAngle == 0) {
                swingAngle = 45;
                // swingTimer = -0.2;
            } else if (swingTimer >= swingInterval) {
                swingAngle -= 90 * swingDir;
                swingDir   = -swingDir;
                swingTimer = 0;
            }
        } else if (SwingMode == 5) {
            swingModeEnabled = 1;
            // 匀速旋转
            swingAngle += 300 * interval;
        } else if (SwingMode == 6) {
            swingModeEnabled = 1;
            // 匀速旋转
            swingAngle += 285 * interval;
        } else {
            if (swingModeEnabled) {
                swingModeEnabled = 0; // 圈数清零
                Motor_Yaw.round  = 0;
            }
            swingAngle = 0;
        }
        PID_Calculate(&PID_Follow_Angle, swingAngle, motorAngle);
        PID_Calculate(&PID_Follow_Speed, PID_Follow_Angle.output, motorSpeed);

        // 设置底盘总体移动速度
        vx = 0;
        vy = 0;
        vw = 0;
        if (ControlMode == 1) {
            vx = -remoteData.lx / 660.0f * 20.0;
            vy = remoteData.ly / 660.0f * 20.0;
        } else if (ControlMode == 2) {
            xTargetRamp = RAMP(xRampStart, 660, xRampProgress);
            if (xRampProgress <= 0.5) {
                xRampProgress += 0.002f;
            } else if (xRampProgress > 0.5 && xRampProgress < 1) {
                xRampProgress += 0.001f;
            }
            yTargetRamp = RAMP(yRampStart, 660, yRampProgress);
            if (yRampProgress <= 0.5) {
                yRampProgress += 0.004f;
            } else if (yRampProgress > 0.5 && yRampProgress < 1) {
                yRampProgress += 0.002f;
            }
            vx = (keyboardData.A - keyboardData.D) * xTargetRamp / 660.0f * 8;
            vy = (keyboardData.W - keyboardData.S) * yTargetRamp / 660.0f * 12;

            if (keyboardData.W == 0 && keyboardData.S == 0) {
                yRampProgress = 0;
                yRampStart    = 0;
            }
            if (keyboardData.A == 0 && keyboardData.D == 0) {
                xRampProgress = 0;
                xRampStart    = 0;
            }
        }
        // 底盘跟随云台
        // vw = ABS(PID_Follow_Angle.error) < followDeadRegion ? 0 : (PID_Follow_Speed.output * DPS2RPS); // gensui -1*PID

        // Host control
        vx += HostChassisData.vx;
        vy += HostChassisData.vy;

        // 按条件开启底盘K_I
        if (vx < 0.1 && vy < 0.1 && vw < 0.3) {
            PID_LFCM.i = 1;
            PID_LBCM.i = 1;
            PID_RBCM.i = 1;
            PID_RFCM.i = 1;
        } else {
            PID_LFCM.i = 0;
            PID_LBCM.i = 0;
            PID_RBCM.i = 0;
            PID_RFCM.i = 0;
        }

        // 开机时底盘匀速回正
        vwRamp = RAMP(0, vw, vwRampProgress);
        if (vwRampProgress < 1) {
            vwRampProgress += 0.001f; // jiansu
        }

        // 麦轮解算及限速
        // targetPower = 70.0 - WANG(30 - ChassisData.powerBuffer, 0.0, 10.0) / 10.0 * 70.0; // 设置目标功率
        // targetPower = ProtocolData.gameRobotstatus.chassis_power_limit * (1 - WANG(60.0 - ChassisData.powerBuffer, 0.0, 40.0) / 40.0); // 设置目标功率 ?

        Chassis_Update(&ChassisData, vx, vy, vwRamp); // 更新麦轮转速
        // Chassis_Fix(&ChassisData, motorAngle);        // 修正旋转后底盘的前进方向
        Chassis_Calculate_Rotor_Speed(&ChassisData);  // 麦轮解算

        // Chassis_Limit_Rotor_Speed(&ChassisData, 800);                                 // 设置转子速度上限 (rad/s)
        // Chassis_Limit_Power(&ChassisData, targetPower, power, powerBuffer, interval); // 根据功率限幅
        if (FastmoveMode == 1) {
            ;
        } else {
            Chassis_Limit_Rotor_Speed(&ChassisData, 800);                                 // 设置转子速度上限 (rad/s)
            Chassis_Limit_Power(&ChassisData, targetPower, power, powerBuffer, interval); // 根据功率限幅
        }

        // 计算输出电流PID
        PID_Calculate(&PID_LFCM, ChassisData.rotorSpeed[0], Motor_LF.speed * RPM2RPS);
        PID_Calculate(&PID_LBCM, ChassisData.rotorSpeed[1], Motor_LB.speed * RPM2RPS);
        PID_Calculate(&PID_RBCM, ChassisData.rotorSpeed[2], Motor_RB.speed * RPM2RPS);
        PID_Calculate(&PID_RFCM, ChassisData.rotorSpeed[3], Motor_RF.speed * RPM2RPS);

        // 输出电流值到电调
        Motor_LF.input = PID_LFCM.output * ChassisData.powerScale;
        Motor_LB.input = PID_LBCM.output * ChassisData.powerScale;
        Motor_RB.input = PID_RBCM.output * ChassisData.powerScale;
        Motor_RF.input = PID_RFCM.output * ChassisData.powerScale;

        // 调试信息
        // DebugData.debug1 = vx * 1000;
        // DebugData.debug2 = vwRamp * 1000;
        // DebugData.debug3 = vw * 1000;
        // 底盘运动更新频率
        vTaskDelayUntil(&LastWakeTime, intervalms);
    }

    vTaskDelete(NULL);
}

void Task_Host(void *Parameters) {
    TickType_t         LastWakeTime = xTaskGetTickCount();
    ProtocolInfo_Type *protocolInfo;
    int16_t            lastReceiveSeq = 0;
    int64_t            sinceReceive;
    while (1) {
        // transmit
        ProtocolData.gyroscopeData.pitch = Gyroscope_EulerData.pitch;
        ProtocolData.gyroscopeData.yaw   = Gyroscope_EulerData.yaw;
        ProtocolData.gyroscopeData.roll  = Gyroscope_EulerData.roll;
        memcpy(ProtocolData.dbusData.dbusBuffer, remoteBuffer, 19);

        // receive autoaim data
        protocolInfo = Protocol_Get_Info_Handle(0x401);
        if (protocolInfo->lastReceiveSeq != protocolInfo->receiveSeq) {
            memcpy(HostAutoaimData.data, ProtocolData.autoaimData.data, protocolInfo->length);
            protocolInfo->lastReceiveSeq = protocolInfo->receiveSeq;
        } else {
            memset(HostAutoaimData.data, 0, protocolInfo->length);
        }
        FacingEnemyMode = HostAutoaimData.yaw_angle_diff != 0 || HostAutoaimData.pitch_angle_diff != 0;
        // DebugData.debug5 = protocolInfo->receiveSeq;

        // receive chassis data
        protocolInfo = Protocol_Get_Info_Handle(0x402);
        sinceReceive = xTaskGetTickCount() - protocolInfo->receiveTime;
        if (sinceReceive > 0 && sinceReceive < 200) {
            memcpy(HostChassisData.data, ProtocolData.chassisData.data, protocolInfo->length);
        } else {
            memset(HostChassisData.data, 0, protocolInfo->length);
        }

        // debug
        // DebugData.debug1 = HostAutoaimData.yaw_angle_diff * 1000;
        // DebugData.debug2 = ProtocolData.autoaimData.yaw_angle_diff * 1000;
        // DebugData.debug3 = HostAutoaimData.pitch_angle_diff*1000;
        // DebugData.debug4 = ProtocolData.autoaimData.pitch_angle_diff*1000;

        vTaskDelayUntil(&LastWakeTime, 8);
    }
    vTaskDelete(NULL);
}

void Task_UI(void *Parameters) {
    TickType_t LastWakeTime  = xTaskGetTickCount();
    float      interval      = 0.1;
    int        intervalms    = interval * 1000;
    uint8_t    isInitialized = 0;

    while (1) {
        
        // Bridge_Send_Protocol_Once(&Node_Judge, 0xF301);
        // delay_ms(200);
        

        if (!isInitialized) {
            UI_Protocol_Update(&ProtocolData.robotInteractiveData);  // 调用并传入robotInteractiveData
            ProtocolData.robotInteractiveData.payload.clientCustomGraphicSingle.grapic_data_struct[0].operate_tpye = 2;
            isInitialized = 1;
        } else {
            ProtocolData.robotInteractiveData.payload.clientCustomGraphicSingle.grapic_data_struct[0].details_c = targetSpeed >> 22;
            ProtocolData.robotInteractiveData.payload.clientCustomGraphicSingle.grapic_data_struct[0].details_d = (targetSpeed >> 11) & 0x7FF;
            ProtocolData.robotInteractiveData.payload.clientCustomGraphicSingle.grapic_data_struct[0].details_e = targetSpeed & 0x3FF;  
        }
        vTaskDelayUntil(&LastWakeTime, intervalms);
    }
    vTaskDelete(NULL);
}


/**
 * @brief 发射机构 (拨弹轮)
 */

void Task_Fire_Stir(void *Parameters) {
    TickType_t LastWakeTime = xTaskGetTickCount(); // 时钟
    float      interval     = 0.05;                // 任务运行间隔 s
    int        intervalms   = interval * 1000;     // 任务运行间隔 ms

    // 射击模式
    enum shootMode_e { shootIdle = 0, shootToDeath = 1, shootReload = 2, shootSingle = 3, emergencyStop = -1 }; // 停止, 连发
    enum shootMode_e shootMode = shootIdle;

    // 热量控制
    int   shootNum        = 0;
    int   mayShootNum     = 0;
    int   maxBulletSpeed  = 0;
    float lastBulletSpeed = 0;
    float maxShootHeat    = 0;
    int   stirSpeed       = 0;
    int   stirAngle       = 0;

    // 视觉系统
    int16_t lastSeq = 0;

    // PID 初始化
    PID_Init(&PID_StirAngle, 10, 0, 0, 18000, 6000);   // 拨弹轮角度环
    PID_Init(&PID_StirSpeed, 25, 0, 0.3, 20000, 1000); // 拨弹轮速度环

    // 开启激光
    // LASER_ON;

    while (1) {
        // 弹舱盖开关
        /*
        if (ROBOT_MIAO) {
            PWM_Set_Compare(&PWM_Magazine_Servo, MagzineOpened ? 10 : 5);
        } else if (ROBOT_WANG) {
            PWM_Set_Compare(&PWM_Magazine_Servo, MagzineOpened ? 16 : 6);
        } else if (ROBOT_SHARK) {
            PWM_Set_Compare(&PWM_Magazine_Servo, MagzineOpened ? 6 : 15);
        } */

        // 拨弹速度
        stirSpeed = -200; // 测试。。。。。。。。。。。。。。。。。。。。。
        //        if (ProtocolData.gameRobotstatus.shooter_id1_42mm_cooling_rate == 20)
        //        {
        //            stirSpeed = -110;
        //        }
        //        else if (ProtocolData.gameRobotstatus.shooter_id1_42mm_cooling_rate == 30) {
        //            stirSpeed = -140;
        //        }
        // stirSpeed = 143; // 热量：120
        // stirSpeed = 120; // 热量：240
        // stirSpeed = 120; // 热量：360

        // 抄家模式
        if (FastShootMode) {
            stirSpeed *= 1.5;
        }

        // 热量控制
        //  maxShootHeat = (float) (ProtocolData.gameRobotstatus.shooter_barrel_heat_limit - ProtocolData.powerHeatData.shooter_id1_42mm_cooling_heat);

        // 输入射击模式
        shootMode = shootIdle;
        if (StirReloadEnabled) {
            shootMode = shootReload;
        } else if (StirEnabled) {
            shootMode = shootToDeath;
        } else if (StirSingleShoot) {
            shootMode = shootSingle;
        }
        // 视觉辅助
        // if (PsShootEnabled && lastSeq != Ps.autoaimData.seq && Ps.autoaimData.biu_biu_state) {
        //     shootMode = shootToDeath;
        // }
        // lastSeq = Ps.autoaimData.seq;
		// 		if(!FastShootMode) {
        // if (maxShootHeat < 100 && StirEnabled) {
        //     PID_Calculate(&PID_StirSpeed, 50, Motor_Stir.speed * RPM2RPS);
        //     Motor_Stir.input = PID_StirSpeed.output;
        //     shootMode        = emergencyStop;
        //     if (Motor_Stir.lastPosition == Motor_Stir.position) {
        //         shootMode = shootIdle;
        //     }
        // }
		// 	}
        // 控制拨弹轮
        if (shootMode == shootIdle) {
            // 停止
            PID_Calculate(&PID_StirSpeed, 0, Motor_Stir.speed * RPM2RPS);
            Motor_Stir.input = PID_StirSpeed.output;
            //						int targetSpeed = 0;
            //						PID_Calculate(&PID_FireL, -1*targetSpeed, Motor_FL.speed);
            //            PID_Calculate(&PID_FireR, targetSpeed, Motor_FR.speed);
            //					  Motor_FL.input = PID_FireL.output;
            //            Motor_FR.input = PID_FireR.output;
        } else if (shootMode == shootReload) {
            PID_Calculate(&PID_StirSpeed, 100, Motor_Stir.speed * RPM2RPS);
            Motor_Stir.input = PID_StirSpeed.output;
            if (Motor_Stir.lastPosition == Motor_Stir.position) {
                shootMode = shootIdle;
            }
        } else if (shootMode == shootToDeath) {
            // 连发
            PID_Calculate(&PID_StirSpeed, stirSpeed, Motor_Stir.speed * RPM2RPS);
            Motor_Stir.input = PID_StirSpeed.output;
            //						int targetSpeed = 4000;
            //						PID_Calculate(&PID_FireL, -1*targetSpeed, Motor_FL.speed);
            //            PID_Calculate(&PID_FireR, targetSpeed, Motor_FR.speed);
            //					  Motor_FL.input = PID_FireL.output;
            //            Motor_FR.input = PID_FireR.output;
        }  else if (shootMode == shootSingle) {
            // 单发
            static float target_angle = 0;  
            if (target_angle == 0) {  
                target_angle = Motor_Stir.angle + 60.0f;
            }
            PID_Calculate(&PID_StirAngle, target_angle, Motor_Stir.angle);
            Motor_Stir.input = PID_StirAngle.output;
            if ((target_angle - Motor_Stir.angle) < 0.5 && (target_angle - Motor_Stir.angle) > -0.5) {
                shootMode == shootIdle;
                target_angle = 0;  // 复位目标角度，使下次按 Q 时可以重新触发旋转
            }
        }
        // DebugData.debug1 = PID_StirSpeed.output;
        // DebugData.debug2 = shootMode;

        vTaskDelayUntil(&LastWakeTime, intervalms);
    }

    vTaskDelete(NULL);
}

void Task_Fire_Frict(void *Parameters) {
    TickType_t LastWakeTime = xTaskGetTickCount(); // 时钟
    float      interval     = 0.05;                // 任务运行间隔 s
    int        intervalms   = interval * 1000;     // 任务运行间隔 ms

    float    motorLSpeed;
    float    motorRSpeed;
    uint32_t targetSpeed = 0;
    int      lastStatus  = 0;
    int      status;

    PID_Init(&PID_FireL, 6, 0, 0, 16384, 1200);
    PID_Init(&PID_FireR, 6, 0, 0, 16384, 1200);

    while (1) {

        if (FrictEnabled) {
            LASER_ON;
        } else {
            LASER_OFF;
        }
        status = keyboardData.G;
        if ((status - lastStatus) < 0) { // 下降沿触发
            targetSpeed = targetSpeed == 0 ? 6000 : 0;
        }
        lastStatus = status; // Update lastStatus
				int targetSpeed_L = -1*targetSpeed;
        PID_Calculate(&PID_FireL, targetSpeed_L, Motor_FL.speed);
        PID_Calculate(&PID_FireR, targetSpeed, Motor_FR.speed);
        Motor_FL.input = PID_FireL.output;
					Motor_FR.input = PID_FireR.output;

        // DebugData.debug1 = Motor_FL.speed;
        // DebugData.debug2 = targetSpeed;
        // DebugData.debug3 = PID_FireR.output;
        vTaskDelayUntil(&LastWakeTime, intervalms);
    }
    vTaskDelete(NULL);
}

void Task_Blink(void *Parameters) {
    TickType_t LastWakeTime = xTaskGetTickCount();
    while (1) {
        LED_Run_Horse_XP(); // XP开机动画,建议延时200ms
        // LED_Run_Horse(); // 跑马灯,建议延时20ms
        vTaskDelayUntil(&LastWakeTime, 200);
    }

    vTaskDelete(NULL);
}

void Task_Startup_Music(void *Parameters) {
    TickType_t LastWakeTime = xTaskGetTickCount();
    while (1) {
        if (KTV_Play(Music_Earth)) break;
        vTaskDelayUntil(&LastWakeTime, 120);
    }
    vTaskDelete(NULL);
}
