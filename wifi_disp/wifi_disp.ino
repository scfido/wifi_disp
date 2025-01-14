#include <FS.h>
#define VER "1.36"
#define HOSTNAME "disp_"
extern "C" {
#include "user_interface.h"
}
#include "default.h"
bool temp_ok = false; //测温ok
bool lcd_flash = false;
uint32_t temp_start;
void ht16c21_cmd(uint8_t cmd, uint8_t dat);
char disp_buf[22];
uint32_t next_disp = 1800; //下次开机
String hostname = HOSTNAME;
float v;
uint8_t proc; //用lcd ram 0 传递过来的变量， 用于通过重启，进行功能切换
//0,1-正常 2-AP 3-OTA  4-http update
#define AP_MODE 2
#define OTA_MODE 3
#define OFF_MODE 4
#define LORA_RECEIVE_MODE 5
#define LORA_SEND_MODE 6

#include "fs.h"
#include "ota.h"
#include "ds1820.h"
#include "wifi_client.h"
#include "ap_web.h"
#include "ht16c21.h"
#include "http_update.h"
#include "lora.h"
bool power_in = false;
void setup()
{
  uint8_t i;
  Serial.begin(115200);
  Serial.println("\x0c\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b");
  Serial.println("Software Ver=" VER "\r\nBuildtime=" __DATE__ " " __TIME__);
  hostname += String(ESP.getChipId(), HEX);
  WiFi.hostname(hostname);
  Serial.println("Hostname: " + hostname);
  delay(100);
  Serial.flush();
  if (!ds_init() && !ds_init()) ds_init();
  ht16c21_setup();
  get_batt();
  Serial.print("电池电压");
  Serial.println(v);
  if (power_in) {
    Serial.println("外接电源");
  }
  Serial.flush();
  proc = ram_buf[0];
  switch (proc) {
    case LORA_RECEIVE_MODE:
      wdt_disable();
      ram_buf[0] = 0;
      disp("L-" VER);
      Serial.println("lora  接收模式");
      send_ram();
      lora_init();
      wifi_station_disconnect();
      wifi_set_opmode(NULL_MODE);
      delay(1000);
      return;
      break;
    case LORA_SEND_MODE:
      wdt_disable();
      ram_buf[0] = LORA_RECEIVE_MODE;
      disp("S-" VER);
      send_ram();
      lora_init();
      wifi_station_disconnect();
      wifi_set_opmode(NULL_MODE);
      delay(1000);
      return;
      break;
    case OFF_MODE: //OFF
      wdt_disable();
      ram_buf[0] = LORA_SEND_MODE;
      disp(" OFF ");
      delay(5000);
      disp("     ");
      ht16c21_cmd(0x84, 0x02); //关闭ht16c21
      lora_init();
      lora.sleep();
      poweroff(0);
      return;
      break;
    case OTA_MODE:
      wdt_disable();
      ram_buf[7] |= 1; //充电
      ram_buf[0] = OFF_MODE;//ota以后，
      disp(" OTA ");
      break;
    case AP_MODE:
      ram_buf[7] |= 1; //充电
      ram_buf[0] = OTA_MODE; //ota
      send_ram();
      AP();
      return;
      break;
    default:
      ram_buf[0] = AP_MODE;
      sprintf(disp_buf, " %3.2f ", v);
      disp(disp_buf);
      lora_init();
      lora.sleep();
      break;
  }
  send_ram();
  //更新时闪烁
  ht16c21_cmd(0x88, 1); //闪烁

  if (wifi_connect() == false) {
    if (proc == OTA_MODE || proc == OFF_MODE) {
      ram_buf[0] = 0;
      send_ram();
      ESP.restart();
    }
    ram_buf[9] |= 0x10; //x1
    ram_buf[0] = 0;
    send_ram();
    Serial.print("不能链接到AP\r\n30分钟后再试试\r\n本次上电时长");
    Serial.print(millis());
    Serial.println("ms");
    poweroff(1800);
    return;
  }

  if (temp_ok == false) {
    delay(temp_start + 2000 - millis());
    temp_ok = get_temp();
  }
  ht16c21_cmd(0x88, 0); //停止闪烁
  if (proc == AP_MODE) return;
  if (proc == OTA_MODE) {
    ota_setup();
    return;
  }
  if (proc == OFF_MODE) {
    if (http_update() == true) {
      ram_buf[0] = 0;
      send_ram();
      disp("HUP O");
      delay(2000);
    }
    ESP.restart();
    return;
  }

  uint16_t httpCode = http_get((ram_buf[7] >> 1) & 1); //先试试上次成功的url
  if (httpCode < 200  || httpCode >= 300) {
    httpCode = http_get((~ram_buf[7] >> 1) & 1); //再试试另一个的url
  }
  if (httpCode < 200 || httpCode >= 400) {
    SPIFFS.begin();
    if (SPIFFS.exists("/wifi_set.txt")) {
      SPIFFS.remove("/wifi_set.txt");
      //换url;
      if (ram_buf[7] & 2) ram_buf[7] &= ~2;
      else ram_buf[7] |= 2;
      Serial.print("不能链接到web\r\n清除上次配置文件，再试一次\r\n");
      ram_buf[0] = 0;
      send_ram();
      poweroff(3);
      return;
    }
    Serial.print("不能链接到web\r\n60分钟后再试试\r\n本次上电时长");
    ram_buf[0] = 0;
    send_ram();
    Serial.print(millis());
    Serial.println("ms");
    poweroff(3600);
    return;
  }
  if (v < 3.6)
    ht16c21_cmd(0x88, 2); //0-不闪 1-2hz 2-1hz 3-0.5hz
  else
    ht16c21_cmd(0x88, 0); //0-不闪 1-2hz 2-1hz 3-0.5hz
  Serial.print("uptime=");
  Serial.print(millis());
  if (next_disp < 60) next_disp = 1800;
  Serial.print("ms,sleep=");
  Serial.println(next_disp);
  poweroff(next_disp);
}
bool power_off = false;
void poweroff(uint32_t sec) {
  get_batt();
  if (ds_pin == 12) Serial.println("V1.0");
  else
    Serial.println("V2.0");
  if (power_in) Serial.println("有外接电源");
  else Serial.println("无外接电源");
  Serial.flush();
  if (ram_buf[7] & 1) {
    if (v > 4.20) {
      Serial.println("v=" + String(v) + ",停止充电");
      ram_buf[7] &= ~1;
      send_ram();
    }
  }
  if (power_in && (ram_buf[7] & 1)) { //如果外面接了电， 就进入LIGHT_SLEEP模式 电流0.8ma， 保持充电
    sec = sec / 2;
    wifi_set_sleep_type(MODEM_SLEEP_T);
    Serial.print("休眠");
    if (sec > 60) {
      Serial.print(sec / 60);
      Serial.print("分钟");
    }
    Serial.print(sec % 60);
    Serial.println("秒");
    Serial.println("充电中");
    Serial.flush();
    wdt_disable();
    if (ds_pin == 12) digitalWrite(13, LOW);
    else {
      Serial.end();
      pinMode(1, OUTPUT);
      digitalWrite(1, HIGH);
    }
    for (uint32_t i = 0; i < sec / 2; i++) {
      system_soft_wdt_feed ();
      delay(2000); //空闲时进入LIGHT_SLEEP_T模式
    }
  }
  wifi_set_sleep_type(LIGHT_SLEEP_T);
  if (ds_pin != 12) {
    Serial.begin(115200);
    Serial.println();
  }
  if ((ram_buf[7] & 1) && power_in)
    Serial.println("充电结束");
  Serial.print("关机");
  if (sec > 0) {
    if (sec > 60) {
      Serial.print(sec / 60);
      Serial.print("分钟");
    }
    Serial.print(sec % 60);
    Serial.println("秒");
    Serial.println("bye!");
  }
  Serial.flush();
  if (ds_pin == 12) digitalWrite(13, HIGH); //v1.0硬件
  else {
    Serial.println("关闭充电");
    Serial.end();
    pinMode(1, OUTPUT);
    digitalWrite(1, LOW);
  }
  wdt_disable();
  system_deep_sleep_set_option(4);
  digitalWrite(LED_BUILTIN, LOW);
  if (sec == 0) ht16c21_cmd(0x84, 0x2); //lcd off
  ESP.deepSleep((uint64_t) 1000000 * sec, WAKE_RF_DEFAULT);
  power_off = true;
}
float get_batt() {
  float v0;
  if (ds_pin == 12) { //v1.0硬件
    pinMode(13, OUTPUT);
    digitalWrite(13, HIGH); //不充电
  } else { //v2.0硬件
    Serial.flush();
    Serial.end();
    pinMode(1, OUTPUT);
    digitalWrite(1, LOW); //不充电
  }
  delay(1);
  get_batt0();
  v0 = v;
  if (ds_pin == 12) //v1.0硬件
    digitalWrite(13, LOW); //充电
  else //v2.0硬件
    digitalWrite(1, HIGH); //充电
  delay(1);
  get_batt0();
  if (v > v0) { //有外接电源
    v0 = v;
    if (ds_pin == 12)
      digitalWrite(13, HIGH); //不充电
    else
      digitalWrite(1, LOW); //不充电
    delay(1);
    get_batt0();
    if (v0 > v) {
      v0 = v;
      if (ds_pin == 12)
        digitalWrite(13, LOW); //充电
      else
        digitalWrite(1, HIGH); //充电
      delay(1);
      get_batt0();
      if (v > v0) {
        v0 = v;
        if (ds_pin == 12)
          digitalWrite(13, HIGH); //不充电
        else
          digitalWrite(1, LOW); //不充电
        delay(1);
        get_batt0();
        if (v0 > v) {
          if (!power_in) {
            power_in = true;
            Serial.println("测得电源插入");
          }
        } else power_in = false;
      } else power_in = false;
    } else power_in = false;
  } else power_in = false;

  if (ds_pin == 12)
    digitalWrite(13, HIGH); //不充电
  else
    digitalWrite(1, LOW); //不充电
  delay(1);
  get_batt0();
  if ((ram_buf[7] & 1) == 0) {
    if (v < 3.8) {
      ram_buf[7] |= 1;
      send_ram();
    }
  }
  if (ds_pin != 12) {
    Serial.begin(115200);
  }
  return v;
}
float get_batt0() {//锂电池电压
  uint32_t dat = analogRead(A0);
  dat = analogRead(A0)
        + analogRead(A0)
        + analogRead(A0)
        + analogRead(A0)
        + analogRead(A0)
        + analogRead(A0)
        + analogRead(A0)
        + analogRead(A0);

  if (ds_pin == 12) //V1.0硬件分压电阻 499k 97.6k
    v = (float) dat / 8 * (499 + 97.6) / 97.6 / 1023 ;
  //else    //V2.0硬件 分压电阻 470k/100k
  v = (float) dat / 8 * (470.0 + 100.0) / 100.0 / 1023 ;
  return v;
}
void loop()
{
  system_soft_wdt_feed ();
  if (power_off) return;
  switch (proc) {
    case LORA_RECEIVE_MODE:
      lora_init();
      lora_receive_loop();
      break;
    case LORA_SEND_MODE:
      lora_init();
      lora_send_loop();
      delay(400);
      break;
    case OTA_MODE:
      ota_loop();
      break;
    case AP_MODE:
      ap_loop();
      break;
  }
}
