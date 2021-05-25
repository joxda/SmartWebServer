/*
 * Title       OnStep Smart Web Server
 * by          Howard Dutton
 *
 * Copyright (C) 2016 to 2021 Howard Dutton
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Revision History, see GitHub
 *
 * Author: Howard Dutton
 * http://www.stellarjourney.com
 * hjd1964@gmail.com
 *
 * Description
 *
 * Web and IP Servers for OnStep and OnStepX
 *
 */

#define Product "Smart Web Server"
#define FirmwareVersionMajor  "0"
#define FirmwareVersionMinor  "9"
#define FirmwareVersionPatch  "l"

// Use Config.h to configure the SWS to your requirements

#include "src/SmartWebServer.h"
NVS nv;
#include "src/tasks/OnTask.h"
Tasks tasks;
#include "src/commands/Commands.h"
#include "src/ethernetServers/EthernetServers.h"
#include "src/wifiServers/WifiServers.h"
#include "src/bleGamepad/BleGamepad.h"
#include "src/encoders/Encoders.h"
#include "src/pages/Pages.h"
#include "src/status/MountStatus.h"

bool connected = false;

void systemServices() {
  nv.poll();
}

void setup(void) {
  strcpy(firmwareVersion.str, FirmwareVersionMajor "." FirmwareVersionMinor FirmwareVersionPatch);

  // start debug serial port
  if (DEBUG == ON || DEBUG == VERBOSE) SERIAL_DEBUG.begin(SERIAL_DEBUG_BAUD);
  delay(2000);

  VF("SWS: SmartWebServer "); VL(firmwareVersion.str);
  VF("SWS: MCU =  "); VF(MCU_STR); V(", "); VF("Pinmap = "); VLF(PINMAP_STR);

  // call gamepad BLE initialization
  #if BLE_GAMEPAD == ON
    VLF("SWS: Init BLE");
    bleInit();
  #endif

  // call hardware specific initialization
  VLF("SWS: Init HAL");
  HAL_INIT();

  // System services
  // add task for system services, runs at 10ms intervals so commiting 1KB of NV takes about 10 seconds
  VF("SWS: Setup, starting system services task (rate 10ms priority 7)... ");
  if (tasks.add(10, 0, true, 7, systemServices, "SysSvcs")) { VL("success"); } else { VL("FAILED!"); }

  // if requested, cause defaults to be written back into NV
  if (NV_WIPE == ON) { nv.update(EE_KEY_HIGH, (int16_t)0); nv.update(EE_KEY_LOW, (int16_t)0); }

  // read settings from NV or init. as required
  VLF("SWS: Init Encoders");
  encoders.init();

  VLF("SWS: Init Webserver");
  #if OPERATIONAL_MODE == WIFI
    wifiInit();
  #else
    ethernetInit();
  #endif

  // init is done, write the NV key if necessary
  nv.update(EE_KEY_HIGH, (int16_t)NV_KEY_HIGH); nv.update(EE_KEY_LOW, (int16_t)NV_KEY_LOW);

  #if LED_STATUS != OFF
    pinMode(LED_STATUS_PIN, OUTPUT);
  #endif

  // attempt to connect to OnStep
  int serialSwap = OFF;
  if (OPERATIONAL_MODE == WIFI) serialSwap = SERIAL_SWAP;
  if (serialSwap == AUTO) serialSwap = AUTO_OFF;

  long serial_baud = SERIAL_BAUD;
  serialBegin(SERIAL_BAUD_DEFAULT, serialSwap);
  uint8_t tb = 1;

Again:
  VLF("SWS: Clearing serial channel");
  clearSerialChannel();

  // look for OnStep
  VLF("SWS: Attempting to contact OnStep");
  SERIAL_ONSTEP.print(":GVP#"); delay(100);
  String s = SERIAL_ONSTEP.readString();
  if (s == "On-Step#" || s == "OnStepX#") {
    // if there is more than one baud rate specified
    if (SERIAL_BAUD != SERIAL_BAUD_DEFAULT) {
      // get fastest baud rate, Mega2560 returns '4' for 19200 baud recommended
      SERIAL_ONSTEP.print(":GB#"); delay(100);
      if (SERIAL_ONSTEP.available() != 1) { serialRecvFlush(); goto Again; }
      if (SERIAL_ONSTEP.read() == '4' && serial_baud > 19200) serial_baud = 19200;
      // set faster baud rate
      SERIAL_ONSTEP.print(highSpeedCommsStr(serial_baud)); delay(100);
      if (SERIAL_ONSTEP.available() != 1) { serialRecvFlush(); goto Again; }
      if (SERIAL_ONSTEP.read() != '1') goto Again;
    }

    // we're all set, just change the baud rate to match OnStep
    serialBegin(serial_baud, serialSwap);

    connected = true;
    VLF("SWS: OnStep Connection established");
  } else {
    if (DEBUG == ON || DEBUG == VERBOSE) { VF("SWS: No valid reply found ("); V(s); VL(")"); }
    #if LED_STATUS == ON
      digitalWrite(LED_STATUS_PIN, LED_STATUS_OFF_STATE);
    #endif
    // got nothing back, toggle baud rate and/or swap ports
    serialRecvFlush();
    tb++;
    if (tb == 16) { tb = 1; if (serialSwap == AUTO_OFF) serialSwap = AUTO_ON; else if (serialSwap == AUTO_ON) serialSwap = AUTO_OFF; }
    if (tb == 1) serialBegin(SERIAL_BAUD_DEFAULT, serialSwap);
    if (tb == 6) serialBegin(serial_baud, serialSwap);
    if (tb == 11) { if (SERIAL_BAUD_DEFAULT == 9600) { serialBegin(19200, serialSwap); } else tb = 15; }
    goto Again;
  }

  #if BLE_GAMEPAD == ON
    bleSetup();
  #endif
  
  // bring servers up
  clearSerialChannel();

  VLF("SWS: Starting port 80 web svr");
  #if OPERATIONAL_MODE == WIFI
    wifiStart();
  #else
    ethernetStart();
  #endif

  VLF("SWS: Set webpage handlers");
  server.on("/index.htm", handleRoot);
  server.on("/configuration.htm", handleConfiguration);
  server.on("/configurationA.txt", configurationAjaxGet);
  server.on("/settings.htm", handleSettings);
  server.on("/settingsA.txt", settingsAjaxGet);
  server.on("/settings.txt", settingsAjax);
  #if ENCODERS == ON
    server.on("/enc.htm", handleEncoders);
    server.on("/encA.txt", encAjaxGet);
    server.on("/enc.txt", encAjax);
  #endif
  server.on("/library.htm", handleLibrary);
  server.on("/libraryA.txt", libraryAjaxGet);
  server.on("/library.txt", libraryAjax);
  server.on("/control.htm", handleControl);
  server.on("/controlA.txt", controlAjaxGet);
  server.on("/control.txt", controlAjax);
  server.on("/auxiliary.htm", handleAux);
  server.on("/auxiliaryA.txt", auxAjaxGet);
  server.on("/auxiliary.txt", auxAjax);
  server.on("/pec.htm", handlePec);
  server.on("/pec.txt", pecAjax);
  server.on("/net.htm", handleNetwork);
  server.on("/", handleRoot);
  
  server.onNotFound(handleNotFound);

  #if STANDARD_COMMAND_CHANNEL == ON
    VLF("SWS: Starting port 9999 cmd svr");
    #if OPERATIONAL_MODE == WIFI
      cmdSvr.begin();
      cmdSvr.setNoDelay(true);
    #else
      cmdSvr.init(9999, 500);
    #endif
  #endif

  #if PERSISTENT_COMMAND_CHANNEL == ON && OPERATIONAL_MODE == WIFI
    VLF("SWS: Starting port 9998 persistant cmd svr");
    persistentCmdSvr.begin();
    persistentCmdSvr.setNoDelay(true);
  #endif

  #if OPERATIONAL_MODE == WIFI
    VLF("SWS: Starting port 80 web svr");
    server.begin();
     #if DNS == ON
        MDNS.begin( HOSTNAME );
        MDNS.addService("http", "tcp", 80);
    #endif
  #endif

  // allow time for the background servers to come up
  delay(2000);

  // clear the serial channel one last time
  clearSerialChannel();

  #if ENCODERS == ON
    VLF("SWS: Starting encoders");
    encoders.init();
  #endif

  if (mountStatus.valid()) {
    mountStatus.update(false);
    delay(100);
  }
    
  VLF("SWS: SmartWebServer ready");
}

void loop(void) {
  server.handleClient();

  #if DNS == ON
   #ifndef ESP32
    MDNS.update();
   #endif
  #endif

  #if ENCODERS == ON
    encoders.poll();
  #endif
  
  #if BLE_GAMEPAD == ON
    bleTimers();
    bleConnTest(); 
  #endif

  #if OPERATIONAL_MODE == WIFI
    wifiCommandChannel();
    wifiPersistantCommandChannel();
  #else
    ethernetCommandChannel();
  #endif

  tasks.yield();
}
