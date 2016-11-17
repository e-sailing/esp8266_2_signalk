/*
SignalK node
 */
#include <FS.h>                   //this needs to be first, or it all crashes and burns...

#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager
#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson

#include <WiFiUdp.h>
#include <DateTime.h>
#include <time.h>
#include <utime.h>
#include <dht11.h>

char OP_mmsi[40];

//flag for saving data
bool shouldSaveConfig = false;

//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

dht11 DHT;
#define DHT11_PIN 4
char ssid[] = "OpenPlotter";
char pass[] = "12345678";

String humidity_skt = "environment.outside.humidity";
String h_temp_skt = "environment.outside.temperature";

unsigned int localPort = 2390;
IPAddress timeServerIP;
const int NTP_PACKET_SIZE = 48;
time_t lastsync;
byte packetBuffer[NTP_PACKET_SIZE];
WiFiUDP udp;

unsigned long secsSince1900 = 0;

void setup()
{
  // put your setup code here, to run once:
  Serial.begin(115200);
  Serial.println();

  //clean FS, for testing
  //SPIFFS.format();

  //read configuration from FS json
  Serial.println("mounting FS...");

  if (SPIFFS.begin()) {
    Serial.println("mounted file system");
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      Serial.println("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        Serial.println("json start");
        json.printTo(Serial);
        if (json.success()) {
          Serial.println("\nparsed json");

          strcpy(OP_mmsi, json["OP_mmsi"]);
          Serial.println("MMSI=");
          Serial.println(OP_mmsi);
          
        } else {
          Serial.println("failed to load json config");
        }
      }
    } else {
      Serial.println("/config.json doesn't exist");
    }
  } else {
    Serial.println("failed to mount FS");
  }
  //end read



  // The extra parameters to be configured (can be either global or just in the setup)
  // After connecting, parameter.getValue() will get you the configured value
  // id/name placeholder/prompt default length
  WiFiManagerParameter custom_mmsi("mmsi", "mmsi", OP_mmsi, 40);

  //WiFiManager
  //Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;

  //set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  //set static ip
  //wifiManager.setSTAStaticIPConfig(IPAddress(10,0,1,99), IPAddress(10,0,1,1), IPAddress(255,255,255,0));
  
  //add all your parameters here
  wifiManager.addParameter(&custom_mmsi);

  //reset settings - for testing
  //wifiManager.resetSettings();

  //set minimu quality of signal so it ignores AP's under that quality
  //defaults to 8%
  //wifiManager.setMinimumSignalQuality();
  
  //sets timeout until configuration portal gets turned off
  //useful to make it all retry or go to sleep
  //in seconds
  //wifiManager.setTimeout(120);

  //fetches ssid and pass and tries to connect
  //if it does not connect it starts an access point with the specified name
  //here  "AutoConnectAP"
  //and goes into a blocking loop awaiting configuration
  if (!wifiManager.autoConnect("AutoConnectAP", "password")) {
    Serial.println("failed to connect and hit timeout");
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(5000);
  }

  //if you get here you have connected to the WiFi
  Serial.println("connected...yeey :)");

  //read updated parameters
  strcpy(OP_mmsi, custom_mmsi.getValue());

  //save the custom parameters to FS
  if (shouldSaveConfig) {
    Serial.println("saving config");
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["OP_mmsi"] = OP_mmsi;

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

    json.printTo(Serial);
    json.printTo(configFile);
    configFile.close();
    //end save
    
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      Serial.println("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        Serial.println("json start");
        json.printTo(Serial);
        configFile.close();
      }
    }
  }

  Serial.print("local ip:");
  Serial.print(WiFi.localIP());

  
  Serial.print("  to server ip:");
  timeServerIP = WiFi.dnsIP();
  Serial.println(timeServerIP);
	Serial.print("Starting UDP on ");
	udp.begin(localPort);
	Serial.print("Local port: ");
	Serial.println(udp.localPort());

  DateTime.sync(86400);
  DateTime.now();

  int i = 0;
  while ( DateTime.now() <= 86400 && i<10) {
    get_ntp();
    delay(400);
    i++;
  };
}

void get_ntp()
{
	//get a random server from the pool
	sendNTPpacket(timeServerIP); // send an NTP packet to a time server
								 // wait to see if a reply is available
	int cb = udp.parsePacket();
	if (!cb) {
		Serial.println("no packet yet");
	}
	else {
		Serial.print("packet received, length=");
		Serial.println(cb);
		// We've received a packet, read the data from it
		udp.read(packetBuffer, NTP_PACKET_SIZE); // read the packet into the buffer

												 //the timestamp starts at byte 40 of the received packet and is four bytes,
												 // or two words, long. First, esxtract the two words:

		unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
		unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
		// combine the four bytes (two words) into a long integer
		// this is NTP time (seconds since Jan 1 1900):
		secsSince1900 = highWord << 16 | lowWord;
		Serial.print("Seconds since Jan 1 1900 = ");
		Serial.println(secsSince1900);
		// now convert NTP time into everyday time:
		Serial.print("Unix time = ");
		// Unix time starts on Jan 1 1970. In seconds, that's 2208988800:
		const unsigned long seventyYears = 2208988800UL;
		// subtract seventy years:
		time_t epoch = secsSince1900 - seventyYears;
    Serial.println(epoch);
    DateTime.sync(epoch);
    DateTime.available();
    lastsync = DateTime.now();
    
		char buffer[80];
		sprintf(buffer, "%04d-%02d-%02dT%02d:%02d:%02d.000Z", DateTime.Year+1900, DateTime.Month+1, DateTime.Day, DateTime.Hour, DateTime.Minute, DateTime.Second );
		Serial.println(buffer);
	}
}

void loop()
{
  time_t now_t = DateTime.now();
  if (lastsync+3600<now_t){
    get_ntp();
  }
	
	int chk;
	chk = DHT.read(DHT11_PIN);    // READ DATA
	switch (chk) {
	case DHTLIB_OK:
		//Serial.print("OK,\t");
		break;
	case DHTLIB_ERROR_CHECKSUM:
		Serial.print("Checksum error,\t");
		break;
	case DHTLIB_ERROR_TIMEOUT:
		Serial.print("Time out error,\t");
		break;
	default:
		Serial.print("Unknown error,\t");
		break;
	}

  DateTime.available();
  char timebuf[30];
  sprintf(timebuf, "%04d-%02d-%02dT%02d:%02d:%02d.000Z", DateTime.Year+1900, DateTime.Month+1, DateTime.Day, DateTime.Hour, DateTime.Minute, DateTime.Second );
  String timestamp(timebuf);
  String strOP_mmsi(OP_mmsi);
	String Erg = "";
	Erg += "{\"path\":\"" + humidity_skt + "\",\"value\":" + String(DHT.humidity) + "},";
	Erg += "{\"path\":\"" + h_temp_skt + "\",\"value\":" + String(DHT.temperature + 273.15) + "}]}]}\n";


	String SignalK = "{\"context\": \"vessels.urn:mrn:imo:mmsi:" + strOP_mmsi + "\",\"updates\":[{\"source\":{\"type\": \"ESP\",\"src\":\"DHT11\"},\"timestamp\":\"" + timestamp + "\",\"values\":[" + Erg;
  const char *SignalKc = SignalK.c_str();

	Serial.print(SignalK);
	
	udp.beginPacket(timeServerIP, 55557);
	udp.write(SignalKc);
	udp.endPacket();

	delay(1000);
}

unsigned long sendNTPpacket(IPAddress& address)
{
	Serial.println("sending NTP packet...");
	// set all bytes in the buffer to 0
	memset(packetBuffer, 0, NTP_PACKET_SIZE);
	// Initialize values needed to form NTP request
	// (see URL above for details on the packets)
	packetBuffer[0] = 0b11100011;   // LI, Version, Mode
	packetBuffer[1] = 0;     // Stratum, or type of clock
	packetBuffer[2] = 6;     // Polling Interval
	packetBuffer[3] = 0xEC;  // Peer Clock Precision
							 // 8 bytes of zero for Root Delay & Root Dispersion
	packetBuffer[12] = 49;
	packetBuffer[13] = 0x4E;
	packetBuffer[14] = 49;
	packetBuffer[15] = 52;

	// all NTP fields have been given values, now
	// you can send a packet requesting a timestamp:
	udp.beginPacket(address, 123); //NTP requests are to port 123
	udp.write(packetBuffer, NTP_PACKET_SIZE);
	udp.endPacket();
}
