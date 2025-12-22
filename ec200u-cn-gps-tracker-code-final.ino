#include <HardwareSerial.h>

HardwareSerial GSM_Serial(1);

#define MODEM_RX 18
#define MODEM_TX 17
const int PWR_KEY_PIN = 10; // Use any available digital pin for the Power Key

// Network and Server Configuration (fill these in)
const char* APN = "airtelgprs.com"; // e.g., "jionet", "airtelgprs.com"
const char* URL = "https://schoolpro-tanstack-start.narayana-vid.workers.dev/api/gps/gps"; 
const char* API_KEY = "YOUR_API_KEY";
const char* van_id="10";

void setup() {

  pinMode(PWR_KEY_PIN, OUTPUT);
  Serial.begin(115200);       // PC Serial monitor (USB)
  GSM_Serial.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX); // Communication with the EC200U module

  digitalWrite(PWR_KEY_PIN, HIGH); // Ensure PWR_KEY is high initially (inactive)

  delay(1000);
  Serial.println("Starting EC200U GPS/HTTP POST Example");

  powerOnModule(); // Sequence to turn the module ON

  // Standard AT command initialization sequence
  sendAtCommand("AT", "OK", 1000);
  sendAtCommand("AT+CPIN?", "READY", 1000);
  sendAtCommand("AT+CREG?", "+CREG: 0,1", 3000); 

  // Initialize and configure GPS
  sendAtCommand("AT+QGPSEND", "OK", 1000); // End any existing GPS session
  sendAtCommand("AT+QGPS=1", "OK", 10000); // Turn on GPS

  // --- ADDED: HTTPS/SSL Configuration ---
  sendAtCommand("AT+QHTTPCFG=\"sslctxid\",1", "OK", 1000);
  sendAtCommand("AT+QSSLCFG=\"sslversion\",1,4", "OK", 1000); 
  sendAtCommand("AT+QSSLCFG=\"ciphersuite\",1,0xFFFF", "OK", 1000); 
  sendAtCommand("AT+QSSLCFG=\"seclevel\",1,0", "OK", 1000); 
  sendAtCommand("AT+QSSLCFG=\"sni\",1,1", "OK", 1000);

  // Configure PDP context for internet
  sendAtCommand("AT+QICSGP=1,1," + String(APN), "OK", 2000);
  sendAtCommand("AT+QIACT=1", "OK", 5000); // Activate context
}

void loop() {
  String gpsData = getGpsData();

  if (gpsData.length() > 0) {
    Serial.println("Got GPS Data: " + gpsData);
    sendHttpPost(gpsData);
  } else {
    Serial.println("Failed to get valid GPS data.");
  }
  delay(30000); // Send data every 30 seconds
}

void powerOnModule() {
  Serial.println("Powering on module...");
  // The EC200U requires a 1-second pulse on the PWR_KEY pin to boot up.
  digitalWrite(PWR_KEY_PIN, LOW);
  delay(1000);
  digitalWrite(PWR_KEY_PIN, HIGH);
  delay(5000); // Give the module time to boot up and register on the network briefly
  Serial.println("Module should be ON now.");
}

// Function to get GPS data and format it into a JSON string
String getGpsData() {
  // We request location data first
  String response = sendAtCommand("AT+QGPSLOC=2", "+QGPSLOC:", 15000); 

  if (response.indexOf("+QGPSLOC:") != -1) {
    // Clean up the response to just the data part (remove "+QGPSLOC: " and trailing "\r\nOK")
    response = response.substring(response.indexOf("+QGPSLOC:") + 10);
    response.trim(); // Removes leading/trailing whitespace/OK

    // Use String manipulation to find specific fields
    int commaIndex = -1;
    int prevCommaIndex = -1;
    String latitude, longitude, speed, heading;

     // --- Time Zone Calculation for IST (UTC+5:30) ---
    // Extract UTC Time components first
    String utcTimeStr = response.substring(0, response.indexOf(','));
    utcTimeStr.trim();
    int hourUTC = utcTimeStr.substring(0, 2).toInt(); 
    int minuteUTC = utcTimeStr.substring(2, 4).toInt();

    int totalMinutesUTC = (hourUTC * 60) + minuteUTC;
    int totalMinutesIST = totalMinutesUTC + (5 * 60) + 30; // Add 5 hours 30 mins offset
    int localMinutesToday = totalMinutesIST % 1440; // Ensure time stays within one day
    int localHourIST = localMinutesToday / 60;
    int localMinuteIST = localMinutesToday % 60;

    // --- Determine the Trip ID based on LOCAL IST time ---
    String tripId = "'NA'"; // Default or Not Applicable

    // Convert local time to a comparable integer (HHMM format, e.g., 0830)
    int localTimeHHMM = (localHourIST * 100) + localMinuteIST;

   // Morning Logic
    if (localTimeHHMM <= 1200) { 
        if (localTimeHHMM < 830) {
            tripId = "\"morning_trip_1\""; // Use escaped double quotes \"
        } else {
            tripId = "\"morning_trip_2\""; 
        }
    }
    // Evening Logic
    else { 
        if (localTimeHHMM < 1630) {
            tripId = "\"evening_trip_1\""; 
        } else {
            tripId = "\"evening_trip_2\""; 
        }
    }


    Serial.println("Local IST Time (HHMM): " + String(localTimeHHMM));
    Serial.println("Determined Trip ID: " + tripId);

    // Iterate through the commas to find the correct positions
    for (int i = 0; i < 7; i++) {
        prevCommaIndex = commaIndex;
        commaIndex = response.indexOf(',', commaIndex + 1);
        
        if (commaIndex == -1 && i < 6) {
            Serial.println("Error: Could not parse all GPS fields.");
            return ""; // Exit if string format is wrong
        }

        // Extract Latitude (Field 2/Index 1) and Longitude (Field 3/Index 2)
        if (i == 1) latitude = response.substring(prevCommaIndex + 1, commaIndex);
        if (i == 2) longitude = response.substring(prevCommaIndex + 1, commaIndex);
        
        // Extract Speed (Field 6/Index 5)
        if (i == 5) speed = response.substring(prevCommaIndex + 1, commaIndex);

        // Extract Heading (Field 7/Index 6)
        if (i == 6) {
            // Heading is the last relevant field before the date starts
            heading = response.substring(prevCommaIndex + 1, commaIndex);
        }
    }

        float latitudeVal = latitude.toFloat();
        float longitudeVal = longitude.toFloat();
        int speedVal = speed.toFloat();
        float headingVal = heading.toFloat();
    
    Serial.println("Parsed Speed: " + speed + " Km/h");
    Serial.println("Parsed Heading: " + heading + " degrees");

    // Format data as JSON payload
    String jsonPayload = "{\"api_key\":\"" + String(API_KEY) + "\",";
        jsonPayload += "\"latitude\":" + String(latitudeVal) + ",";
        jsonPayload += "\"longitude\":" + String(longitudeVal) + ",";
        jsonPayload += "\"speed\":" + String(speedVal) + ",";
        jsonPayload += "\"heading\":" + String(headingVal) + ","; // Will now be "0" or "0.0"
        jsonPayload += "\"van_id\":" + String(van_id) + ",";
        jsonPayload += "\"trip_id\":" + tripId + "}"; 
    
    return jsonPayload;

  }
  return ""; // Return empty string if failed
}

// Function to handle the HTTP POST request logic
void sendHttpPost(String payload) {
  int dataLength = payload.length();
  
  // Build the AT+QHTTPURL command entirely within the Arduino String class
  String urlCommand = "AT+QHTTPURL=";
  urlCommand += String(URL).length();
  urlCommand += ",60";

  sendAtCommand(urlCommand, "CONNECT", 1000);
  sendAtCommand(URL, "OK", 1000);

  // Set Content-Type header
  sendAtCommand("AT+QHTTPCFG=\"header\",\"Content-Type: application/json\"", "OK", 1000);

  // Build the AT+QHTTPPOST command
  String postCommand = "AT+QHTTPPOST=";
  postCommand += dataLength;
  postCommand += ",80,80";

   Serial.println("DEBUG PAYLOAD: " + payload);
  
  sendAtCommand(postCommand, "CONNECT", 2000);
  String response = sendAtCommand(payload, "+QHTTPPOST:", 10000); // Send the actual data

  if (response.indexOf("+QHTTPPOST: 0,") != -1) {
    Serial.println("POST request sent successfully. Reading response...");
    // Read the server response
    String readResponse = sendAtCommand("AT+QHTTPREAD=1000", "+QHTTPREAD:", 10000); 
    Serial.println("Server Response:");
    Serial.println(readResponse);
  } else {
    Serial.println("HTTP POST failed or timed out.");
  }
}

// Helper function to send AT commands and wait for a specific response
String sendAtCommand(String command, String expectedResponse, unsigned long timeout) {
  GSM_Serial.println(command);
  long int time = millis();
  String response = "";
  
  while ((time + timeout) > millis()) {
    while (GSM_Serial.available()) {
      char c = GSM_Serial.read();
      response += c;
    }
    if (response.indexOf(expectedResponse) != -1 || response.indexOf("ERROR") != -1) {
      break;
    }
  }
  Serial.println("SENT: " + command);
  Serial.println("RCVD: " + response);
  return response;
}