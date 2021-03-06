#include "sha1.h"
#include <mcp_can.h>
#include <SPI.h>
#include <OnewireKeypad.h>

// For keypad
char Keys[] = {
  '4','3','2','1',
  '8','7','6','5',
  'C','B','A','9',
  'D','E','#','*'
};

OnewireKeypad <Print, 16> KP(Serial, Keys, 4, 4, A0, 5000, 1000);

// CONSTANTS
const int SPI_CS_PIN = 9;
const uint8_t *MIL_KEY = (const uint8_t *)"mil key";
const uint8_t *DOOR_KEY = (const uint8_t *)"door key";
const uint8_t *ENGINE_KEY = (const uint8_t *)"engine key";
const uint8_t *TOGGLE_AUTH_KEY = (const uint8_t *)"auth key";
const int MIL_KEY_LEN = 7;
const int DOOR_KEY_LEN = 8;
const int ENGINE_KEY_LEN = 10;
const int TOGGLE_AUTH_KEY_LEN = 8;
const int MIL_ID = 0x7e0;
const int DOOR_ID = 0x7d0;
const int ENGINE_ID = 0x7a0;
const int TOGGLE_AUTH_ID = 0x700;
bool SHOULD_GENERATE_MESSAGES = false;
const int DELAY_TIME = 100; // milliseconds
bool SHOULD_AUTHENTICATE = true;

// Preset CAN messages (for IR remote)
typedef enum {
  LOCK_DOORS,
  UNLOCK_DOORS,
  MIL_ON,
  MIL_OFF,
  ENGINE_GREEN,
  ENGINE_RED,
  ENGINE_BLUE,
  ENGINE_PURPLE,
  ENGINE_CLEAR,
  CLEAR_ALL,
  TOGGLE_AUTH
} presetCANMessage;


// Variables
// For HMAC
uint8_t *hash;


MCP_CAN CAN(SPI_CS_PIN);



void setup()
{
    // Initialize the serial interface
    // with baud rate 115200
    Serial.begin(115200);

    // Intialize CAN Bus Shield
    while (CAN_OK != CAN.begin(CAN_500KBPS))
    {
        Serial.println("CAN BUS Shield init fail");
        Serial.println(" Init CAN BUS Shield again");
    }
    Serial.println("CAN BUS Shield init ok!");

    KP.SetHoldTime(1000);
}



void loop()
{
    char Key;
    byte KState = KP.Key_State();
    
    // If user inputted data via serial interface...
    if (Serial.available())
    {
        // Get the sections of the message and the key as strings
        String sid = Serial.readStringUntil('#');
        String message = Serial.readStringUntil(' ');
        String skey;
        if (SHOULD_AUTHENTICATE)
          skey = Serial.readString();
  
        // Parse and convert them appropriately
        uint32_t id = 0;
        id += (uint32_t)(CharToUInt8(sid.charAt(0))) << 8;
        id += (uint32_t)(CharToUInt8(sid.charAt(1))) << 4;
        id += (uint32_t)CharToUInt8(sid.charAt(2));
        uint8_t dlc = CharToUInt8(message.charAt(1));
        String sdata = message.substring(2, 2+(dlc*2));
        uint8_t *data = new uint8_t[8];
        for(int i = 0, j = 0; i < dlc*2; i+=2, j++)
        {
            data[j] = CharToUInt8(sdata.charAt(i)) << 4;
            data[j] += CharToUInt8(sdata.charAt(i+1));
        }

        // Send the original message, followed by the 
        // authentication messages
        CAN.sendMsgBuf(id, 0, dlc, data);
        if (SHOULD_AUTHENTICATE)
            SendAuthMessagesByKey(id, dlc, data, skey);
        
        if (id == TOGGLE_AUTH_ID)
            if (SHOULD_AUTHENTICATE && skey == "auth key")
                SHOULD_AUTHENTICATE = !SHOULD_AUTHENTICATE;
            else if (!SHOULD_AUTHENTICATE)
                SHOULD_AUTHENTICATE = !SHOULD_AUTHENTICATE;
                

        // Print the original message to serial interface
        PrintMessage(id, dlc, &data[0]);
    }
    else if (KState == PRESSED)
    {
        if (Key = KP.Getkey())
        {
            Serial << "Pressed: " << Key << "\n";
            switch(Key)
            {
                case '1':
                  SendPresetMessage(UNLOCK_DOORS);
                  break;
                case '2':
                  SendPresetMessage(LOCK_DOORS);
                  break;
                case '3':
                  SendPresetMessage(MIL_ON);
                  break;
                case '4':
                  SendPresetMessage(MIL_OFF);
                  break;
                case '5':
                  SendPresetMessage(ENGINE_GREEN);
                  break;
                case '6':
                  SendPresetMessage(ENGINE_RED);
                  break;
                case '7':
                  SendPresetMessage(ENGINE_BLUE);
                  break;
                case '8':
                  SendPresetMessage(ENGINE_PURPLE);
                  break;
                case '*':
                  SHOULD_GENERATE_MESSAGES = !SHOULD_GENERATE_MESSAGES;
                  break;
                case '#':
                  SendPresetMessage(CLEAR_ALL);
                  break;
                case 'E':
                  SendPresetMessage(TOGGLE_AUTH);
                  SHOULD_AUTHENTICATE = !SHOULD_AUTHENTICATE;
                  break;
                default:
                  break;
            }
        }
    }
    else if (SHOULD_GENERATE_MESSAGES)
    {
        // Otherwise, if the user didn't input a message, 
        // generate a random, valid CAN message
        uint32_t id = 0;
        uint8_t dlc = 0;
        unsigned char *stmp = new unsigned char[8];
        GenerateMessage(id, dlc, &stmp[0]);

        // Send the original message, followed
        // by the authentication messages
        CAN.sendMsgBuf(id, 0, dlc, stmp);
        if (SHOULD_AUTHENTICATE)
          SendAuthMessages(id, dlc, stmp);
    
        // Display Message on Serial interface
        PrintMessage(id, dlc, &stmp[0]);
    }

    // Flush the serial buffer
    Serial.flush();
    if (SHOULD_GENERATE_MESSAGES)
      delay(DELAY_TIME);    // delay to actually be able to see serial output
}


//----------------------------------------------------------------------------
// Print a CAN message to the serial interface.
//----------------------------------------------------------------------------
void PrintMessage(uint32_t id, uint8_t dlc, uint8_t *stmp)
{
    Serial.print("ID: ");
    Serial.println(id, HEX);
    Serial.print("DLC: ");
    Serial.println(dlc, HEX);
    Serial.print("Data: ");
    for (int i = 0; i < dlc; i++)
    {
      if (stmp[i] < 0x10)
          Serial.print('0');
      Serial.print(stmp[i], HEX);
      Serial.print(' ');
    }
    Serial.print("\n\r-------------------------------------------\n\r");
}


//----------------------------------------------------------------------------
// Fill the next four bytes of a buffer with the current timestamp
//----------------------------------------------------------------------------
void StampTime(uint8_t *buf, unsigned long ms)
{
    // Store the timestamp
    buf[0] = (int)((ms >> 24) & 0xFF);
    buf[1] = (int)((ms >> 16) & 0xFF);
    buf[2] = (int)((ms >> 8) & 0xFF);
    buf[3] = (int)((ms & 0xFF));
}


//----------------------------------------------------------------------------
// Send three authentication messages for a given CAN message.
// The first 20 bytes are an HMAC, and the last 4 bytes are timestamp.
// This function should only be used for generated messages.
//----------------------------------------------------------------------------
void SendAuthMessages(uint32_t id, uint8_t dlc, uint8_t *buf)
{
    // Get the correct key for this ID, and its length
    const uint8_t *theKey;
    theKey = GetKey(id);
    int keyLen = GetKeyLen(id);

    // Convert the data to a string. Otherwise, incoming data 
    // will be slightly different than actual data for the HMAC creation
    String message = "";
    for(int i = 0; i < dlc; i++)
    {
        if (buf[i] < 0x10)
            message += '0';
        message += String(buf[i], HEX);
    }
    message.toLowerCase(); // for consistency

    // Serial output for convenience
    Serial.print("Hashing: ");
    Serial.println(message);
    Serial.print("Key Used: ");
    Serial.println((const char *)theKey);

    // Create the 3 auth messages
    uint8_t msg1[8];
    uint8_t msg2[8];
    uint8_t msg3[8];

    unsigned long ms = millis(); 

    // Fill msg1 first 4 bytes with timestamp
    StampTime(&msg1[0], ms);

    // Initialize the HMAC, write the string data, and create the HMAC
    Sha1.initHmac(theKey, keyLen);
    WriteBytes((const uint8_t *)message.c_str(), message.length()); // hash the message
    Sha1.write(msg1[0]); // include the timestamp in the hash
    Sha1.write(msg1[1]);
    Sha1.write(msg1[2]);
    Sha1.write(msg1[3]);
//    Serial << "Timestamp used in hash: " << ms << "\n";
    hash = Sha1.resultHmac();

    // Store the hash in the 3 auth messages
    for(int i = 4; i < 8; i++) msg1[i] = hash[i-4];
    for(int i = 8; i < 16; i++) msg2[i - 8] = hash[i-4];
    for(int i = 16; i < 24; i++) msg3[i - 16] = hash[i-4];

    // Send the auth messages
    CAN.sendMsgBuf(id, 0, 8, msg1);
    CAN.sendMsgBuf(id, 0, 8, msg2);
    CAN.sendMsgBuf(id, 0, 8, msg3);

    // Serial output
    Serial.print("Sending Digest: ");
    PrintHash(hash);
}


//----------------------------------------------------------------------------
// Send three authentication messages for a given CAN message and key.
// The first 20 bytes are an HMAC, and the last 4 bytes are timestamp.
// This function should only be used for user-inputted messages.
//----------------------------------------------------------------------------
void SendAuthMessagesByKey(uint32_t id, uint8_t dlc, uint8_t *buf, String sKey)
{
    // Convert the key to uint8_t * and get the length
    const uint8_t *theKey = (const uint8_t *)sKey.c_str();
    int keyLen = sKey.length();

    // Convert the data to a string. Otherwise, incoming data 
    // will be slightly different than actual data for the HMAC creation 
    String message = "";
    for(int i = 0; i < dlc; i++)
    {
        if (buf[i] < 0x10)
            message += '0';
        message += String(buf[i], HEX);
    }
    message.toLowerCase(); // for consistency

    // Serial output
//    Serial.print("Hashing: ");
//    Serial.println(message);
    Serial.print("Key: ");
    Serial.println((const char *)theKey);

    // Create the 3 auth messages
    uint8_t msg1[8];
    uint8_t msg2[8];
    uint8_t msg3[8];

    unsigned long ms = millis(); 

    // Fill msg1 first 4 bytes with timestamp
    StampTime(&msg1[0], ms);

    // Create the HMAC
    Sha1.initHmac(theKey, keyLen);
    WriteBytes((const uint8_t *)message.c_str(), message.length());
    Sha1.write(msg1[0]); // include the timestamp in the hash
    Sha1.write(msg1[1]);
    Sha1.write(msg1[2]);
    Sha1.write(msg1[3]);
    hash = Sha1.resultHmac();

    // Fill the 3 auth messages with the hash
    for(int i = 4; i < 8; i++) msg1[i] = hash[i-4];
    for(int i = 8; i < 16; i++) msg2[i - 8] = hash[i-4];
    for(int i = 16; i < 24; i++) msg3[i - 16] = hash[i-4];

    // Send the auth messages
    CAN.sendMsgBuf(id, 0, 8, msg1);
    CAN.sendMsgBuf(id, 0, 8, msg2);
    CAN.sendMsgBuf(id, 0, 8, msg3);

    // Serial output
    Serial.print("Sending Digest: ");
    PrintHash(hash);
}


//----------------------------------------------------------------------------
// Takes a hash as uint8_t* and prints it in HEX to serial output.
// Assumes the hash is 160-bit (20-bytes)
//----------------------------------------------------------------------------
void PrintHash(uint8_t *hash)
{
    for (int i = 0; i < 20; i++)
    {
        if (hash[i] < 0x10)
            Serial.print('0');
        Serial.print(hash[i], HEX);
    }
    Serial.println();
}


//----------------------------------------------------------------------------
// Takes a valid CAN frame ID as uint32_t and return 
// the correct key as uint8_t*
//----------------------------------------------------------------------------
const uint8_t * GetKey(uint32_t id)
{
    switch(id)
    {
        case MIL_ID:
            return MIL_KEY;
        case DOOR_ID:
            return DOOR_KEY;
        case ENGINE_ID:
            return ENGINE_KEY;
        case TOGGLE_AUTH_ID:
            return TOGGLE_AUTH_KEY;
        default:
            return 0;
    }
}


//----------------------------------------------------------------------------
// Takes a valid CAN frame ID as uint32_t and return 
// the length of the key as an int
//----------------------------------------------------------------------------
int GetKeyLen(uint32_t id)
{
    switch(id)
    {
        case MIL_ID:
            return MIL_KEY_LEN;
        case DOOR_ID:
            return DOOR_KEY_LEN;
        case ENGINE_ID:
            return ENGINE_KEY_LEN;
        case TOGGLE_AUTH_ID:
            return TOGGLE_AUTH_KEY_LEN;
        default:
            return -1;
    }
}


//----------------------------------------------------------------------------
// Generate a pseudo-random CAN message. Takes frame ID (uint32_t), 
// data length code (uint8_t), and data buffer (unsigned char *).
//----------------------------------------------------------------------------
void GenerateMessage(uint32_t &id, uint8_t &dlc, unsigned char *data)
{
    int selector = random(3);
    id = SelectMessage(selector);
    dlc = random(9);
    for(int i = 0; i < dlc; i++)
    {
      data[i] = random(0x100); // each byte is random char between 0..0xFF
    }
    for(int i = dlc; i < 8; i++)
    {
      data[i] = 0;
    }
}



// Take a 'presetCANMessage' and
// send the appropriate message
void SendPresetMessage(presetCANMessage msg)
{
    uint32_t id;
    uint8_t dlc;
    uint8_t* data = new uint8_t[8];

    switch(msg)
    {
        case LOCK_DOORS:
          id = DOOR_ID;
          dlc = 1;
          data[0] = 1;
          break;
        case UNLOCK_DOORS:
          id = DOOR_ID;
          dlc = 1;
          data[0] = 0;
          break;
        case MIL_ON:
          id = MIL_ID;
          dlc = 1;
          data[0] = 1;
          break;
        case MIL_OFF:
          id = MIL_ID;
          dlc = 1;
          data[0] = 0;
          break;
        case ENGINE_GREEN:
          id = ENGINE_ID;
          dlc = 3;
          data[0] = 0x10;
          data[1] = 0xF0;
          data[2] = 0x10;
          break;
        case ENGINE_RED:
          id = ENGINE_ID;
          dlc = 3;
          data[0] = 0xB0;
          data[1] = 0x00;
          data[2] = 0x00;
          break;
        case ENGINE_BLUE:
          id = ENGINE_ID;
          dlc = 3;
          data[0] = 0x10;
          data[1] = 0x10;
          data[2] = 0xF0;
          break;
        case ENGINE_PURPLE:
          id = ENGINE_ID;
          dlc = 3;
          data[0] = 0xF0;
          data[1] = 0x21;
          data[2] = 0xE9;
          break;
        case ENGINE_CLEAR:
          id = ENGINE_ID;
          dlc = 3;
          data[0] = 0;
          data[1] = 0;
          data[2] = 0;
          break;
        case TOGGLE_AUTH:
          id = TOGGLE_AUTH_ID;
          dlc = 0;
          break;
        case CLEAR_ALL:
          // Clear ALL
          SendPresetMessage(MIL_OFF);
          delay(20);
          SendPresetMessage(UNLOCK_DOORS);
          delay(30);
          SendPresetMessage(ENGINE_CLEAR);
          delay(30);
          return;
    }

    // Send the message
    CAN.sendMsgBuf(id, 0, dlc, data);
    if (SHOULD_AUTHENTICATE)
      SendAuthMessages(id, dlc, data);
    
    // Display Message on Serial interface
    PrintMessage(id, dlc, &data[0]);
}




//----------------------------------------------------------------------------
// Takes an integer and returns a valid CAN frame ID.
// If selector is invalid, returns -1.
//----------------------------------------------------------------------------
int SelectMessage(int selector)
{
    switch(selector)
    {
        case 0: return MIL_ID;
        case 1: return DOOR_ID;
        case 2: return ENGINE_ID;
        default: return -1;
    }
}


//----------------------------------------------------------------------------
// Writes, or updates, the SHA-1 hash with a data buffer (const uint8_t *).
//----------------------------------------------------------------------------
void WriteBytes(const uint8_t *data, int len)
{
    for(int i = 0; i < len; i++)
    {
        Sha1.write(data[i]);
    }
}


//----------------------------------------------------------------------------
// Converts a C string to uint8_t *
//----------------------------------------------------------------------------
void CStringHexToUInt8(char *str, uint8_t *buf)
{
    for(int i = 0, j = 0; i < 40; i+=2, j++)
    {
        buf[j] = CharByteToUInt8(str[i], str[i+1]);
        PrintHexByte(buf[j]);
        Serial.print(' ');
    }
    Serial.println();
}


//----------------------------------------------------------------------------
// Print a uint8_t value as HEX
//----------------------------------------------------------------------------
void PrintHexByte(uint8_t val)
{
    if (val < 0x10)
        Serial.print('0');
    Serial.print(val, HEX);
}


//----------------------------------------------------------------------------
// Takes a C string of hex characters and converts it to uint8_t *
//----------------------------------------------------------------------------
uint8_t *HexToUInt8(char *hexStr, uint8_t &len)
{
    len = strlen(hexStr);
    uint8_t *buf = new uint8_t[len];
    int bufIndex = 0;

    // Convert each pair of chars (a byte)
    for (int i = 0; i < len; i+=2)
    {
        buf[bufIndex] = CharToUInt8(hexStr[i]) * 16;
        buf[bufIndex] += CharToUInt8(hexStr[i+1]);
        bufIndex++;
    }
    len = len / 2;
    
    return buf;
}


//----------------------------------------------------------------------------
// Takes a C string of integers and returns it as uint8_t *
//----------------------------------------------------------------------------
uint8_t *CStringToUInt8(char *str, uint8_t &len)
{
    len = strlen(str);
    uint8_t *buf = new uint8_t[len];

    for(int i = 0; i < len; i++)
    {
        // store and convert from ASCII to integer value
        buf[i] = (uint8_t)str[i];
    }

    return buf;
}


//----------------------------------------------------------------------------
// Takes a HEX char and returns its integer value as uint8_t
//----------------------------------------------------------------------------
uint8_t CharToUInt8(char ch)
{
    uint8_t value = ch;
    if (ch >= '0' && ch <= '9')
        value -= '0';
    else if (ch >= 'a' && ch <= 'f')
        value = (ch - 'a') + 10;
    else
        value = (ch - 'A') + 10;
    return value;
}


//----------------------------------------------------------------------------
// Takes two characters (a pair, resulting in a byte)
// and returns their byte value as uint8_t
//----------------------------------------------------------------------------
uint8_t CharByteToUInt8(char ch1, char ch2)
{
    uint8_t value = CharToUInt8(ch1);
    value *= 16;
    value += CharToUInt8(ch2);
    return value;
}
