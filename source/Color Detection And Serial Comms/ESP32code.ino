#define RXD2 16
#define TXD2 17

// --- VISION CONSTANTS ---
// Based on 320px width (QVGA)
const int LEFT_BOUNDARY = 106;
const int RIGHT_BOUNDARY = 214;

// IMPORTANT: You MUST tune this value based on your physical robot!
// How big does the blob need to be before the robot decides it is "close"?
const int CLOSE_PROXIMITY_AREA = 2500; 

void setup() {
  Serial.begin(115200);
  Serial2.begin(115200, SERIAL_8N1, RXD2, TXD2);
  Serial.println("ESP32 Ready. Processing Zones and Proximity...");
}

void loop() {
  if (Serial2.available()) {
    String incomingData = Serial2.readStringUntil('\n');
    
    if (incomingData.startsWith("R") || incomingData.startsWith("G")) {
      
      // Parse the string: "Color,X,Y,Area"
      int firstComma = incomingData.indexOf(',');
      int secondComma = incomingData.indexOf(',', firstComma + 1);
      int thirdComma = incomingData.indexOf(',', secondComma + 1);
      
      String color = incomingData.substring(0, firstComma);
      int x_pos = incomingData.substring(firstComma + 1, secondComma).toInt();
      int y_pos = incomingData.substring(secondComma + 1, thirdComma).toInt();
      int area  = incomingData.substring(thirdComma + 1).toInt();

      // --- 1. DETERMINE ZONE ---
      String zone = "";
      if (x_pos < LEFT_BOUNDARY) {
        zone = "LEFT";
      } else if (x_pos > RIGHT_BOUNDARY) {
        zone = "RIGHT";
      } else {
        zone = "CENTER";
      }

      // --- 2. DETERMINE PROXIMITY ---
      bool isClose = (area > CLOSE_PROXIMITY_AREA);

      // Print for debugging
      Serial.print(color == "R" ? "RED Block " : "GREEN Block ");
      Serial.print(" | Zone: ");
      Serial.print(zone);
      Serial.print(" | Area: ");
      Serial.print(area);
      Serial.println(isClose ? " [CLOSE!]" : " [FAR]");

      // --- 3. STEERING LOGIC ---
      if (isClose) {
        if (color == "R" && zone == "CENTER") {
          Serial.println(">>> ACTION: RED IN CENTER. DODGING LEFT! <<<");
          // TODO: Write code here to set left motor slower, right motor faster
        } 
        else if (color == "G" && zone == "CENTER") {
          Serial.println(">>> ACTION: GREEN IN CENTER. DODGING RIGHT! <<<");
          // TODO: Write code here to set right motor slower, left motor faster
        }
        else if (color == "R" && zone == "RIGHT") {
          Serial.println(">>> ACTION: RED IS ON RIGHT. SAFE TO GO STRAIGHT. <<<");
          // TODO: Keep driving straight
        }
        else if (color == "G" && zone == "LEFT") {
          Serial.println(">>> ACTION: GREEN IS ON LEFT. SAFE TO GO STRAIGHT. <<<");
          // TODO: Keep driving straight
        }
      }
    }
  }
}