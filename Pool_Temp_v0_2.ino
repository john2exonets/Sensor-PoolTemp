// 
//  Pool_Temp
//  Arduino + DS18x20 + MQTT
//
// http://www.pjrc.com/teensy/td_libs_OneWire.html
//
// The DallasTemperature library can do all this work for you!
// http://milesburton.com/Dallas_Temperature_Control_Library
//
//  Sensor wireing:
//  Yellow -> 4.7k Res., side 1 -> pin 10
//  Red -> 4.7k Res., side 2 -> V+
//  Black -> GND
//  DS18x20 works with either 3.3v or 5.0v
//
//  John D. Allen
//  October, 2017
//
//------------------------------------------------------------------------
//  Function connectAP() --
//  This function in this file is written to cycle through an array of APs
//  to connect to AND test the connection. I had problems with other sensors
//  that can connect to the AP, but the AP is not passing the MQTT connection
//  along correctly...so this function does a connection to the MQTT Broker
//  on port 1883 before it returns.
//------------------------------------------------------------------------

#include <SPI.h>
#include <OneWire.h>
#include <WiFi101.h>
#include <PubSubClient.h>

//#define DEBUG 1

// WiFi APs to try and connect to.
const char *APssid[] = {"AP1", "AP2"};
const char *APpass[] = {"password1", "password2"};
int APlen = 2;

#define WINC_CS 8
#define WINC_IRQ 7
#define WINC_RST 4
#define VBATPIN  A7
#define DSPIN  10

#define mqtt_server               "10.1.1.28"
#define mqtt_client_name          "Pool"
#define DELAY                     300000           // Every 5 minutes

// Initialize Libraries
OneWire  ds(DSPIN);  // on pin 10 (a 4.7K resistor is necessary)
int WFstatus = WL_IDLE_STATUS;
WiFiClient wincClient;
PubSubClient client(wincClient);

//----------------------------------------------------
// Global Vars
//----------------------------------------------------
byte i;
byte present = 0;
byte type_s;
byte data[12];
byte addr[8];
float celsius, fahrenheit;
unsigned long lastBat = 0;
unsigned long lastChk = DELAY + DELAY;
unsigned long now;
float temp;
bool batFlag = false;

// MQTT Payload tempaltes
String t1 = "{\"name\": \"";
String t2 = "\", \"temp\": ";
String t3 = " }";
String ttt = "";
char t[50];

//---------------------------------------------------------------------------------------------------------
//---------------------------------------[       SETUP       ]---------------------------------------------
//---------------------------------------------------------------------------------------------------------
void setup(void) {
  WiFi.setPins(WINC_CS, WINC_IRQ, WINC_RST, 2);
  WiFi.lowPowerMode();
  Serial.begin(9600);

  // Test to see if Wifi board is responding.
  if (WiFi.status() == WL_NO_SHIELD) {
   #if DEBUG
    Serial.println(WiFi.status());
    Serial.println("No WiFi Device found...Aborting.");
   #endif
    while (true);  //loop forever.
  }
}

//---------------------------------------------------------------------------------------------------------
//---------------------------------------[         LOOP         ]------------------------------------------
//---------------------------------------------------------------------------------------------------------
void loop(void) {
  //
  //  Every DELAY (5 min), read the temp and send it via MQTT
  //
  now = millis();
  if (now - lastChk > DELAY) {
    lastChk = now;
   #ifdef DEBUG
    Serial.println("//-------------------------------------");
   #endif  
    // Connect up WiFi
    while (WiFi.status() != WL_CONNECTED) {
      connectAP();
      if (WiFi.status() != WL_CONNECTED) {
        delay(60000);   // Wait 60 seconds before trying again
      }
    }
    // Connect up to MQTT Broker
    if (!client.connected()) {
      mqttConnect();
    }
    client.loop();

    // if set, send battery info
    if (batFlag) {
      batFlag = false;
      sendBttyVolts(client);
    }
    // Get Pool Temp
    temp = readDS18B20();

    // Send Pool temp via MQTT
    ttt = t1 + String(mqtt_client_name) + t2 + String(temp).c_str() + t3;
    ttt.toCharArray(t, ttt.length()+1);
    client.publish("temp/read", t);
   #ifdef DEBUG
    Serial.print("Pool Temp: ");
    Serial.println(temp);
   #endif
    delay(2000);  // Delay 2 seconds to make sure MQTT packet gets away.

    // Disconnect from the MQTT Broker
    client.disconnect();
    delay(250);  // wait for a bit to make sure it disconnects correctly.
    
    // Close the WiFi Connection
    WiFi.end();   // powers down the WiFi board...battery saving move #1
  }
   
  if (now - lastBat > 780000) {    // every 13 minutes, to catch the 15m xmit.
    lastBat = now;
    batFlag = true;
  }
}

//---------------------------------------------------------------------------------------------------------
//---------------------------------[             FUNCTIONS             ]-----------------------------------
//---------------------------------------------------------------------------------------------------------

//----------------------------------------------------
// Function: readDS18B20()
// Using the OneWire library, read the Temp & return.
//----------------------------------------------------
float readDS18B20() {
 #ifdef DEBUG
  Serial.println("->readDS18B20()");
 #endif 
  if ( !ds.search(addr)) {
   #ifdef DEBUG 
    Serial.println("Sensor Not Found!!");
    Serial.println();
   #endif 
    ds.reset_search();
    delay(250);
    return -1.0;
  }

 #ifdef DEBUG 
  Serial.print("ROM =");
  for( i = 0; i < 8; i++) {  // loop through all address bytes.
    Serial.write(' ');
    Serial.print(addr[i], HEX);
  }
 #endif 

  if (OneWire::crc8(addr, 7) != addr[7]) {
      Serial.println("CRC is not valid!");
      return -1.0;
  }
 #ifdef DEBUG 
  Serial.println();
 #endif
  
  // the first ROM byte indicates which chip
  switch (addr[0]) {
    case 0x10:
     #ifdef DEBUG
      Serial.println("  Chip = DS18S20");  // or old DS1820
     #endif 
      type_s = 1;
      break;
    case 0x28:
     #ifdef DEBUG
      Serial.println("  Chip = DS18B20");
     #endif 
      type_s = 0;
      break;
    case 0x22:
     #ifdef DEBUG
      Serial.println("  Chip = DS1822");
     #endif 
      type_s = 0;
      break;
    default:
     #ifdef DEBUG 
      Serial.println("Device is not a DS18x20 family device.");
     #endif 
      return -1.0;
  } 

  ds.reset();
  ds.select(addr);
  ds.write(0x44, 1);        // start conversion, with parasite power on at the end
  
  delay(1000);     // maybe 750ms is enough, maybe not
  // we might do a ds.depower() here, but the reset will take care of it.
  
  present = ds.reset();
  ds.select(addr);    
  ds.write(0xBE);         // Read Scratchpad

 #ifdef DEBUG
  Serial.print("  Data = ");
  Serial.print(present, HEX);
  Serial.print(" ");
 #endif 
  for ( i = 0; i < 9; i++) {           // we need 9 bytes
    data[i] = ds.read();
   #ifdef DEBUG 
    Serial.print(data[i], HEX);
    Serial.print(" ");
   #endif 
  }
 #ifdef DEBUG 
  Serial.print(" CRC=");
  Serial.print(OneWire::crc8(data, 8), HEX);
  Serial.println();
 #endif
 
  // Convert the data to actual temperature
  // because the result is a 16 bit signed integer, it should
  // be stored to an "int16_t" type, which is always 16 bits
  // even when compiled on a 32 bit processor.
  int16_t raw = (data[1] << 8) | data[0];
  if (type_s) {
    raw = raw << 3; // 9 bit resolution default
    if (data[7] == 0x10) {
      // "count remain" gives full 12 bit resolution
      raw = (raw & 0xFFF0) + 12 - data[6];
    }
  } else {
    byte cfg = (data[4] & 0x60);
    // at lower res, the low bits are undefined, so let's zero them
    if (cfg == 0x00) raw = raw & ~7;  // 9 bit resolution, 93.75 ms
    else if (cfg == 0x20) raw = raw & ~3; // 10 bit res, 187.5 ms
    else if (cfg == 0x40) raw = raw & ~1; // 11 bit res, 375 ms
    //// default is 12 bit resolution, 750 ms conversion time
  }
  celsius = (float)raw / 16.0;
  fahrenheit = celsius * 1.8 + 32.0;
 #ifdef DEBUG 
  Serial.print("  Temperature = ");
  Serial.print(celsius);
  Serial.print(" Celsius, ");
  Serial.print(fahrenheit);
  Serial.println(" Fahrenheit");
 #endif 

  // Send back results
  ds.reset_search();
  //delay(250);
  return fahrenheit;
}

//----------------------------------------------------
// Function: connectAP()
//  Connect to the defined WiFi AP
//----------------------------------------------------
bool connectAP()
{ 
 #ifdef DEBUG
  Serial.println("->connectAP()");
 #endif   
  bool out = false;  
  for(int i = 0; i < APlen; ++i) {    // loop through all our possible APs to connect to 
    // Attempt to connect to the AP
    WFstatus = WiFi.begin(APssid[i], APpass[i]);
    
    if (WFstatus == WL_CONNECTED) {
      long rssi = WiFi.RSSI();
      uint32_t ipAddress = WiFi.localIP(); 

      if (wincClient.connect(mqtt_server, 1883)) {  // Test the connection to the MQTT Broker.
        wincClient.stop();
        out = true;
      } else {
        WiFi.disconnect();
        WiFi.end();
        delay(250);
        out = false;    // Can't connect to server, go to next AP 
        continue; 
      }
     #ifdef DEBUG 
      Serial.print("My IP: ");
      Serial.println(ipToString(ipAddress));
      Serial.print("WiFi AP: ");
      Serial.println(APssid[i]);
      Serial.print("AP Signal Strength: ");
      Serial.println(rssi);
     #endif 
      break;
    }
    else
    {
     #ifdef DEBUG 
      // Display the error message
      Serial.print("AP = ");
      Serial.println(APssid[i]);
      Serial.print("Connection Error >> ");
      switch (WiFi.status())
      {
        case 1:
          Serial.println( "No SSID Available");
          break;
        case 2:
          Serial.println( "Scan Completed");
          break;
        case 3:
          Serial.println( "Connected");
          break;
        case 4:
          Serial.println( "Conneciton Failed");
          break;
        case 5:
          Serial.println( "Connection Lost");
          break;
        case 6:
          Serial.println( "Disconnected");
          break;
        case 7:
          Serial.println( "Access Point - Listening");
          break;
        case 8:
          Serial.println( "Access Point - Connected");
          break;
        case 9:
          Serial.println( "Access Point - Connection Failed");
          break;
        case 0:
          Serial.println( "Board Idle");
          break ;
        case 255:
          Serial.println( "No WiFi Shield Found");
          break;
      }
     #endif 
      // Return false to indicate that we received an error (available in feather.errno)
      out = false;
    } // if
  } // for
  return out;
}

//----------------------------------------------------
// Function: mqttConnect()
//  Connect to the defined MQTT Broker.
//----------------------------------------------------
void mqttConnect() {
 #ifdef DEBUG
  Serial.println("->mqttConnect()");
 #endif  
  if (client.state() == MQTT_DISCONNECTED) {
    client.setServer(mqtt_server, 1883);
  }
  // Loop until we're reconnected
  while (!client.connected()) {
    while (WiFi.status() != WL_CONNECTED) {
      connectAP();
      client.setServer(mqtt_server, 1883);
    }
   #ifdef DEBUG    
    Serial.print("Attempting MQTT connection...");
   #endif    
    // Attempt to connect
    if (client.connect(mqtt_client_name)) {
     #ifdef DEBUG      
      Serial.println("connected");
     #endif      
    } else {
     #ifdef DEBUG      
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      Serial.println("MQTT Error:");
      switch (client.state()) 
      {
        case -4:
          Serial.println("Timeout");
          break;
        case -3:
          Serial.println("Connection Lost");
          break;
        case -2:
          Serial.println("Conn Failed");
          break;
        case -1:
          Serial.println("Disconnect");
          break;
        case 1:
          Serial.println("Bad Proto");
          break;
        case 2:
          Serial.println("Bad Client ID");
          break;
        case 3:
          Serial.println("Svr Unavail");
          break;
        case 4:
          Serial.println("Bad Creds");
          break;
        case 5:
          Serial.println("Unauthorized");
          break;
        default:
          Serial.println("{Unkn Error}");  
      }
     #endif
      client.disconnect();
      WiFi.disconnect();
      WiFi.end();
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }  
}

String ipToString(IPAddress ip){
  String s="";
  for (int i=0; i<4; i++)
    s += i  ? "." + String(ip[i]) : String(ip[i]);
  return s;
}

void sendBttyVolts() {
  float measuredvbat = analogRead(VBATPIN);
  measuredvbat *= 2;    // we divided by 2, so multiply back
  measuredvbat *= 3.3;  // Multiply by 3.3V, our reference voltage
  measuredvbat /= 1024; // convert to voltage
  // Send Battery Reading via MQTT
  ttt = String(measuredvbat).c_str();
  ttt.toCharArray(t, ttt.length()+1);
  client.publish("info/volts/Pool", t);
}

