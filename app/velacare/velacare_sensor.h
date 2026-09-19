/****************************************************************************
 * apps/velacare/velacare_sensor.h — 传感器抽象层头文件
 ****************************************************************************/

#ifndef __APPS_VELACARE_VELACARE_SENSOR_H
#define __APPS_VELACARE_VELACARE_SENSOR_H

#include <stdbool.h>
#include <stdint.h>

/*
 * 传感器网关串口协议：
 *
 *   VC1,<seq>,<smoke>,<water>,<door>,<fall>,<wifi>*<crc16>\n
 *
 * 四类传感器状态：0=离线，1=正常，2=报警；wifi：0=离线，1=在线。
 * crc16 为从 "VC1" 到 '*' 前一个字符的 CRC-16/CCITT-FALSE。
 * 网关还会发送 CFG1 配置帧和 VCB1 蜂鸣器控制帧：
 *
 *   VCB1,<muted>,<acknowledged>,<severity>*<crc16>
 *
 * muted/acknowledged 只停止本地蜂鸣器，不改写 VC1 传感器真值。
 * D12x 使用 SOS1 发出局域网求助，网关以 SACK1 确认。SACK1 的状态为：
 * 1=未配置照护人，2=等待家属。求助撤销和日常关怀使用独立消息，
 * 绝不复用跌倒确认：
 *
 *   SOSX1,<id>*<crc16>
 *   SXACK1,<id>,<status>*<crc16>
 *   CARE1,<id>,<type>*<crc16>             type: 1=请联系我，2=今日报平安
 *   CACK1,<id>,<type>,<status>*<crc16>    status: 1=未配置照护人，
 *                                        2=等待家属
 *   CAREX1,<id>,<type>*<crc16>
 *   CXACK1,<id>,<type>,<status>*<crc16>
 *
 * 撤销回执状态允许 0/1/2；2 表示请求已由家属处理，保持 FAMILY_SEEN。
 * ESP32 使用 FAM1,<id>,<type>,<reply> 下发家属回复，type 为 0(SOS)、
 * 1(请联系我)、2(今日报平安)，reply 为 1..4。D12x 对每个匹配当前事件的
 * FAM1（包括重复帧）回复 FACK1,<id>,<type>。
 *
 * 所有回执都必须与当前活动 request_id 完全匹配，旧回执会被忽略。
 * D12x 使用 OK1 发出老人“确认安全”，不清除跌倒/门窗等告警真值：
 *
 *   OK1,<id>*<crc16>
 *   OACK1,<id>*<crc16>
 *
 * 网关用 OACK1 确认已收到老人确认安全；该消息不清除跌倒真值。
 */

#define VELACARE_SENSOR_UART_PATH        "/dev/ttyS1"
#define VELACARE_SENSOR_TIMEOUT_MS       3000

enum velacare_sos_status_e
{
  VELACARE_SOS_IDLE = 0,
  VELACARE_SOS_SENDING = 1,
  VELACARE_SOS_WAIT_GATEWAY = 2,
  VELACARE_SOS_NO_CAREGIVER = 3,
  VELACARE_SOS_WAIT_FAMILY = 4,
  VELACARE_SOS_FAMILY_SEEN = 5,
  VELACARE_SOS_CANCELLING = 6,
  VELACARE_SOS_CANCELLED = 7,
  VELACARE_SOS_RETRYING = -1
};

enum velacare_care_kind_e
{
  VELACARE_CARE_NONE = 0,
  VELACARE_CARE_CONTACT_ME = 1,
  VELACARE_CARE_CHECKIN_OK = 2
};

enum velacare_care_status_e
{
  VELACARE_CARE_IDLE = 0,
  VELACARE_CARE_SENDING = 1,
  VELACARE_CARE_NO_CAREGIVER = 2,
  VELACARE_CARE_WAIT_FAMILY = 3,
  VELACARE_CARE_FAMILY_SEEN = 4,
  VELACARE_CARE_CANCELLING = 5,
  VELACARE_CARE_CANCELLED = 6,
  VELACARE_CARE_RETRYING = -1
};

int velacare_sensor_init(void);
void velacare_sensor_poll(uint32_t now_ms);
bool velacare_sensor_gateway_online(void);
bool velacare_sensor_wifi_online(void);
uint32_t velacare_sensor_last_sequence(void);

bool velacare_sensor_smoke_online(void);
bool velacare_sensor_smoke_alarm(void);
bool velacare_sensor_water_online(void);
bool velacare_sensor_water_alarm(void);
bool velacare_sensor_door_online(void);
bool velacare_sensor_door_alarm(void);
bool velacare_sensor_fall_online(void);
bool velacare_sensor_fall_alarm(void);

bool velacare_sensor_caregiver_configured(void);
const char *velacare_sensor_caregiver_masked(void);
int velacare_sensor_send_sos(void);
int velacare_sensor_cancel_sos(void);
int velacare_sensor_send_ok(void);
int velacare_sensor_send_care(int kind);
int velacare_sensor_cancel_care(void);
bool velacare_sensor_buzzer_suppressed(void);
bool velacare_sensor_family_acknowledged(void);
bool velacare_sensor_elder_confirmed(void);
int velacare_sensor_sos_status(void);
int velacare_sensor_care_kind(void);
int velacare_sensor_care_status(void);
int velacare_sensor_family_reply(void);
int velacare_sensor_sos_family_reply(void);

#endif
