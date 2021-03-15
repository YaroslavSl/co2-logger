// TODO: Wifi manager https://randomnerdtutorials.com/wifimanager-with-esp8266-autoconnect-custom-parameter-and-manage-your-ssid-and-password/
#if defined(ESP32)
#include <WiFiMulti.h>
WiFiMulti wifiMulti;
#define DEVICE "ESP32"
#elif defined(ESP8266)
#include <ESP8266WiFiMulti.h>
ESP8266WiFiMulti wifiMulti;
#define DEVICE "ESP8266"
#endif

// #define UART_DEBUG
//or 
#define MH_Z19B                   // CO2 sensor support

#include <InfluxDbClient.h>
#include <InfluxDbCloud.h>

// WiFi AP SSID
#define WIFI_SSID "TP-LINK_DF37"
// WiFi password
#define WIFI_PASSWORD "45972378"
// InfluxDB v2 server url, e.g. https://eu-central-1-1.aws.cloud2.influxdata.com (Use: InfluxDB UI -> Load Data -> Client Libraries)
#define INFLUXDB_URL "http://192.168.0.111:8086"
// InfluxDB v2 server or cloud API authentication token (Use: InfluxDB UI -> Data -> Tokens -> <select token>)
#define INFLUXDB_TOKEN "ArPvmcuBK4zEclSkZACsX5XKkwX_lohE3PKHhSHYeR_dppTqAT80VT7iuuCUqp0Fo5VmIC_takij4husi7dfZQ=="
// InfluxDB v2 organization id (Use: InfluxDB UI -> User -> About -> Common Ids )
#define INFLUXDB_ORG "WOT"
// InfluxDB v2 bucket name (Use: InfluxDB UI ->  Data -> Buckets)
#define INFLUXDB_BUCKET "local.influxDb"

// Set timezone string according to https://www.gnu.org/software/libc/manual/html_node/TZ-Variable.html
// Examples:
//  Pacific Time: "PST8PDT"
//  Eastern: "EST5EDT"
//  Japanesse: "JST-9"
//  Central Europe: "CET-1CEST,M3.5.0,M10.5.0/3"
#define TZ_INFO "CET-1CEST,M3.5.0,M10.5.0/3"

// InfluxDB client instance with preconfigured InfluxCloud certificate
InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN, InfluxDbCloud2CACert);

// Data point
Point sensor("wifi_status");


/*
@name         ReadMhZ19BValue(unsigned int *wRecValue)
@brief        Read a sensor CO2 value from a MhZ19B sensor through a serial interface
@details      e.g. "FF8603AA0000000000"
@param[out]   wRecValue - received sensor value
@return       0 - read data, 
              1 - NoData, 
              2 - 1st byte is wrong, 
              3 - 2nd byte is wrong
*/
unsigned char ReadMhZ19BValue(unsigned int *wRecValue) {
  // Read CO2 concentration
  // send "FF0186000000000079" into co2 sensor
  unsigned char cBuf[9] = { 0xFF, 0x01, 0x86, 0x00, 0x00, 0x00, 0x00, 0x00, 0x79 };
  Serial.write(cBuf, 9);

  int incomingByte = 0;
  unsigned char cRecWrong = 1;  // 1 - NoData, 2 - 1st byte is wrong, 3 - 2nd byte is wrong

  // wait for an answer
  delay(500);

  if (Serial.available() > 0) {  // if here are data available
    incomingByte = Serial.read();

    // Should be received "0xFF 0x86 HIGH LOW - - - - Checksum"
    if (incomingByte == 0xFF) {
      incomingByte = Serial.read();
      if (incomingByte == 0x86) {
        *wRecValue =  ((unsigned char)Serial.read()) << 8;
        *wRecValue |= ((unsigned char)Serial.read());
        cRecWrong = 0;

        while (Serial.available() > 0) {
          incomingByte = Serial.read();
        }
      } else {
        cRecWrong = 3;
      }
    } else {
      cRecWrong = 2;
    }
  }

  return cRecWrong;
}

void setup() {
   //Init uart interface for debug (or sensor)
  #ifdef UART_DEBUG
    Serial.begin(115200);
  #endif//UART_DEBUG

  //Init uart interface for sensor (or )
  #ifdef MH_Z19B
    Serial.begin(9600);
  #endif

  // Setup wifi
  WiFi.mode(WIFI_STA);
  wifiMulti.addAP(WIFI_SSID, WIFI_PASSWORD);

  #ifdef UART_DEBUG
  Serial.print("Connecting to wifi");
  #endif
  while (wifiMulti.run() != WL_CONNECTED) {
    #ifdef UART_DEBUG
    Serial.print(".");
    #endif
    delay(100);
  }
  #ifdef UART_DEBUG
  Serial.println();
  #endif


  // Add tags
  sensor.addTag("device", DEVICE);
  sensor.addTag("SSID", WiFi.SSID());

  // Accurate time is necessary for certificate validation and writing in batches
  // For the fastest time sync find NTP servers in your area: https://www.pool.ntp.org/zone/
  // Syncing progress and the time will be printed to Serial.
  timeSync(TZ_INFO, "pool.ntp.org", "time.nis.gov");

  // Check server connection
  #ifdef UART_DEBUG
  if (client.validateConnection()) {
    Serial.print("Connected to InfluxDB: ");
    Serial.println(client.getServerUrl());
  } else {
    Serial.print("InfluxDB connection failed: ");
    Serial.println(client.getLastErrorMessage());
  }
  #endif
  #ifdef MH_Z19B
  client.validateConnection();
  #endif
}

void loop() {
  // Clear fields for reusing the point. Tags will remain untouched
  sensor.clearFields();

  // Store measured value into point
  // Report RSSI of currently connected network
  sensor.addField("rssi", WiFi.RSSI());

  #ifdef MH_Z19B
  String payload = "{\"d\":{\"Name\":\"";

  //  Clear buffer
  while (Serial.available() > 0) Serial.read();

  //  Read CO2 concentration
    unsigned int wRecValue;
    unsigned char cRecWrong;
    cRecWrong = ReadMhZ19BValue(&wRecValue);

    if (cRecWrong == 0) {
      sensor.addField("co2", wRecValue);
    } else {
      sensor.addField("co2error", cRecWrong);
    }
  #endif

  // Print what are we exactly writing
  #ifdef UART_DEBUG
  Serial.print("Writing: ");
  Serial.println(sensor.toLineProtocol());
  #endif

  // If no Wifi signal, try to reconnect it
  if ((WiFi.RSSI() == 0) && (wifiMulti.run() != WL_CONNECTED)) {
    #ifdef UART_DEBUG
    Serial.println("Wifi connection lost");
    #endif
  }

  // Write point
  if (!client.writePoint(sensor)) {
    #ifdef UART_DEBUG
    Serial.print("InfluxDB write failed: ");
    Serial.println(client.getLastErrorMessage());
    #endif
  }

  // Wait 10s
  #ifdef UART_DEBUG
  Serial.println("Wait 1m");
  #endif

  delay(60000);
}