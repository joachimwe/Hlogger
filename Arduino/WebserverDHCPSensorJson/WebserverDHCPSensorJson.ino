// Arduino demo sketch for testing the DHCP client code
//
// Original author: Andrew Lindsay
// Major rewrite and API overhaul by jcw, 2011-06-07
//
// Copyright: GPL V2
// See http://www.gnu.org/licenses/gpl.html


// ethernet shield en28j60
#include <EtherCard.h>

/********************************************************************/
// First we include the libraries
#include <OneWire.h>
#include <DallasTemperature.h>
/********************************************************************/
// Data wire is plugged into pin 2 on the Arduino
#define ONE_WIRE_BUS 2
/********************************************************************/
// Setup a oneWire instance to communicate with any OneWire devices
// (not just Maxim/Dallas temperature ICs)
OneWire oneWire(ONE_WIRE_BUS);
/********************************************************************/
// Pass our oneWire reference to Dallas Temperature.
DallasTemperature sensors(&oneWire);
/********************************************************************/

static byte mymac[] = { 0x74, 0x69, 0x69, 0x2D, 0x30, 0x31 };

BufferFiller bfill;
#define STR_BUFFER_SIZE 64
static char strbuf[STR_BUFFER_SIZE + 1];
#define STR_SADDRESS_SIZE 18
static char strAddress[STR_SADDRESS_SIZE + 1];
int packetID = 0;


byte Ethernet::buffer[1000];
/*
const char page[] PROGMEM =
  "HTTP/1.0 503 Service Unavailable\r\n"
  "Content-Type: text/html\r\n"
  "Retry-After: 600\r\n"
  "\r\n"
  "<html>"
  "<head><title>"
  "Hello World!"
  "</title></head>"
  "<body>"
  "<h3>Hello World! This is your Arduino speaking!</h3>"
  "</body>"
  "</html>";
*/

const char http_OK[] PROGMEM =
   "HTTP/1.0 200 OK\r\n"
   "Content-Type: text/html\r\n"
   "Pragma: no-cache\r\n\r\n";

const char http_Found[] PROGMEM =
   "HTTP/1.0 302 Found\r\n"
   "Location: /\r\n\r\n";

const char http_Unauthorized[] PROGMEM =
   "HTTP/1.0 401 Unauthorized\r\n"
   "Content-Type: text/html\r\n\r\n"
   "<h1>401 Unauthorized</h1>";

byte count = 0;
DeviceAddress* sensor;
float* lastTemp;
float delta = 0.3f;
float delta_work = delta;

void setup () {
  Serial.begin(9600);

  // DS 2820
  sensors.begin();
  // locate devices on the bus
  Serial.print("Locating devices...");
  Serial.print("Found ");
  count = sensors.getDeviceCount();
  Serial.print(count, DEC);
  sensor = new DeviceAddress [count];
  lastTemp = new float [count];
  Serial.println(" devices.");

  for (int i = 0; i < count; i++)
  {
    lastTemp[i] = 0.f;
    if (!sensors.getAddress(sensor[i], i))
      Serial.println("Unable to find address for Device" + i);
    else
      printAddress(sensor[i]);
  }

  // Ethernet
  Serial.println(F("\n[testDHCP]"));

  Serial.print("MAC: ");
  for (byte i = 0; i < 6; ++i) {
    Serial.print(mymac[i], HEX);
    if (i < 5)
      Serial.print(':');
  }
  Serial.println();

  if (ether.begin(sizeof Ethernet::buffer, mymac) == 0)
    Serial.println(F("Failed to access Ethernet controller"));

  Serial.println(F("Setting up DHCP"));
  if (!ether.dhcpSetup())
    Serial.println(F("DHCP failed"));

  ether.printIp("My IP: ", ether.myip);
  ether.printIp("Netmask: ", ether.netmask);
  ether.printIp("GW IP: ", ether.gwip);
  ether.printIp("DNS IP: ", ether.dnsip);
}

void loop () {
  // wait for an incoming TCP packet, but ignore its contents
 

  word len = ether.packetReceive();
  word pos = ether.packetLoop(len);

  if (pos)  // check if valid tcp data is received
  {
      bfill = ether.tcpOffset();     
      char *data = (char *) Ethernet::buffer + pos;
      if (strncmp("GET /", data, 5) != 0) {
           // Unsupported HTTP request
           // 304 or 501 response would be more appropriate
           Serial.println(F("http_Unauthorized"));
           bfill.emit_p(http_Unauthorized);
       }
       else {
           data += 5;
           
           if (data[0] == ' ') {
               // Return home page
               Serial.println(F("http_normal"));
               rqstSensors(true);
           }
           else if (strncmp("?q=all ", data, 7) == 0) {
               // Set ledStatus true and redirect to home page
               Serial.println(F("http_all"));
               delta_work = -10.f;
               rqstSensors(false);
               delta_work = delta;
           }
           else if (strncmp("?delta=", data, 7) == 0) {
              Serial.println(F("http_delta"));           
              char number[15];
              for(int i = 0; i < 5; i++)
              {
                if (data[i] != ' ')
                  number[i] = data[i+7];
                else
                {
                  number[i] = '\0';
                  break;
                }
              }
              int iDelta = atoi(&number[0]);     
              delta = ((float) iDelta)/10;    
              delta_work = delta;    

              // response
              rqstDelta();
           }
   //   rqstSensors();                              // send json data
       }
      ether.httpServerReply(bfill.position());    // send http response
  }
  
}

static void rqstDelta() { 
  bfill.emit_p(PSTR(                              
    "HTTP/1.0 200 OK\r\n"
    "Content-Type: application/json\r\n"
    "Pragma: no-cache\r\n"
    "\r\n"
    "{\"measure\":"));

    sprintf(strbuf, "{\"delta\":%2d.%02d}", (int)delta, (int)(abs(delta)*100)%100 );
     bfill.emit_raw(strbuf, strlen(strbuf));
    bfill.emit_p(PSTR("}"));    
}  


static void rqstSensors(bool updateTemp) { 
  bfill.emit_p(PSTR(                              
    "HTTP/1.0 200 OK\r\n"
    "Content-Type: application/json\r\n"
    "Pragma: no-cache\r\n"
    "\r\n"
    "{\"measure\":{\"packetID\":$D,\"sensors\": [" )
    ,packetID);
    packetID++;
    packetID%=100;   

  sensors.requestTemperatures(); // Send the command to get temperature readings
  //count = 10;
  
  for (int i = 0; i < count; i++) {
    printData(sensor[i]);

    float t =  sensors.getTempC(sensor[i]);
   
    printAddressString(sensor[i], &strAddress[0]);

    // dummy
    //t = 13.2f;
    //strcpy(strAddress, "asdfölkjasdfölkj"); 

    if (abs(lastTemp[i] - t)>delta_work)
    {
      if(updateTemp)
         lastTemp[i] = t;
      // sensor object
      bfill.emit_p(PSTR("{\"type\":\"DS1820\",\"address\":"));    
      float frac = abs(t);
      sprintf(strbuf, "\"%s\",\"value\":%2d.%02d,\"unit\":\"°C\"", strAddress, (int)t, (int)(frac*100)%100 );
      bfill.emit_raw(strbuf, strlen(strbuf));
      bfill.emit_p(PSTR("}"));
      if (i < count-1) 
         bfill.emit_p(PSTR(","));
    }
  }
  bfill.emit_p(PSTR("]}}"));  
                                 
}



// function to print a device address
void printAddress(DeviceAddress deviceAddress)
{
  for (uint8_t i = 0; i < 8; i++)
  {
    // zero pad the address if necessary
    if (deviceAddress[i] < 16) Serial.print("0");
    Serial.print(deviceAddress[i], HEX);
  }
}

// function to print a device address
void printAddressString(DeviceAddress deviceAddress, char* str)
{
  for (uint8_t i = 0; i < 8; i++)
  {
    // zero pad the address if necessary
    //if (deviceAddress[i] < 16) str[i] = "0";
    //Serial.print(deviceAddress[i], HEX);
    sprintf(&str[2*i], "%02X", deviceAddress[i]);
  }
}

// function to print the temperature for a device
void printTemperature(DeviceAddress deviceAddress)
{
  float tempC = sensors.getTempC(deviceAddress);
  Serial.print("Temp C: ");
  Serial.print(tempC);
  Serial.print(" Temp F: ");
  Serial.print(DallasTemperature::toFahrenheit(tempC));
}

// main function to print information about a device
void printData(DeviceAddress deviceAddress)
{
  Serial.print("Device Address: ");
  printAddress(deviceAddress);
  Serial.print(" ");
  printTemperature(deviceAddress);
  Serial.println();
}
