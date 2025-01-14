#ifndef __HTTP_UPDATE_H__
#define __HTTP_UPDATE_H__
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>

#define USE_SERIAL Serial

bool http_update()
{
  if (get_batt() < 3.6) {
    Serial.println("电压太低,不做升级");
    ESP.restart();
    return false;
  }
  ram_buf[7] |= 1; //开充电模式
  send_ram();
  disp(" H UP");
  String update_url = "http://www.anheng.com.cn/wifi_disp.bin"; // get_url((ram_buf[7] >> 1) & 1) + "?p=update&sn=" + String(hostname) + "&ver=" VER;
  USE_SERIAL.print("下载firmware from ");
  USE_SERIAL.println(update_url);

  t_httpUpdate_return  ret = ESPhttpUpdate.update(update_url);

  switch (ret) {
    case HTTP_UPDATE_FAILED:
      USE_SERIAL.printf("HTTP_UPDATE_FAILD Error (%d): %s\r\n", ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());
      ram_buf[0] = 0;
      ESP.restart();
      break;

    case HTTP_UPDATE_NO_UPDATES:
      USE_SERIAL.println("HTTP_UPDATE_NO_UPDATES");
      break;

    case HTTP_UPDATE_OK:
      USE_SERIAL.println("HTTP_UPDATE_OK");
      return true;
      break;
  }
  delay(1000);
  return false;
}
#endif //__HTTP_UPDATE_H__
