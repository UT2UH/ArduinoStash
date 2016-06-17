#include <SpritzCipher.h>
#include "can_constants.h"
#include "mcp_can.h"
#include <SPI.h>

MCP_CAN CAN0(9); // Set CS pin to 9 (only for Seeed Studio CAN Bus)

uint8_t MESSAGE[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
const uint16_t MESSAGE_LEN = sizeof(MESSAGE);
uint8_t CORRECT_KEY[3] = {0x00, 0x01, 0x02};
const uint16_t KEY_LEN = sizeof(CORRECT_KEY);
uint8_t BAD_KEY[3] = {0x0f, 0x0e, 0x0d};
const uint8_t HASH_LEN = 20;

uint8_t CORRECT_HASH[HASH_LEN];

void setup() {
  // put your setup code here, to run once:
  // Create the correct hash
  spritz_mac(&CORRECT_HASH[0], HASH_LEN, &MESSAGE[0], MESSAGE_LEN, &CORRECT_KEY[0], KEY_LEN);
  Serial.begin(9600);
  while(!Serial);
}

void loop() {
  // put your main code here, to run repeatedly:
  Serial.print("Sending Correct Message...");
  VerifyMessage(&MESSAGE[0], MESSAGE_LEN, &CORRECT_KEY[0], KEY_LEN);
  Serial.print("Sending Incorrect Message...");
  VerifyMessage(&MESSAGE[0], MESSAGE_LEN, &BAD_KEY[0], KEY_LEN);
  Serial.println("-------------------------------------------------------------");
  
  delay(10000);
}


//-----------------------------------------------------------------------------------------------


bool CompareHash(uint8_t *hash)
{
  for(int i = 0; i < HASH_LEN; i++)
  {
    if (hash[i] != CORRECT_HASH[i])
      return false;
  }
  return true;
}

bool VerifyMessage(uint8_t *msg, uint16_t msgLen, uint8_t* key, uint16_t keyLen)
{
  // Create hash based off given arguments
  uint8_t *hash = new uint8_t[HASH_LEN];
  spritz_mac(&hash[0], HASH_LEN, &msg[0], msgLen, &key[0], keyLen);

  if (CompareHash(&hash[0]))
  {
    Serial.println("SUCCESS!");
    return true;
  }
  else
  {
    Serial.println("FAILED");
    return false;
  }
}

void PrintMessage(int id, int dlc, byte* data)
{
  Serial.print(id, HEX);
  Serial.print("#0");
  Serial.print(dlc, HEX); 
  for(int i = 0; i < dlc; i++)
  {
    if (data[i] < 0x10)
      Serial.print("0");
    Serial.print(data[i], HEX);
  }
  Serial.print("\n");
}

void FillAuthMessages(uint8_t *hash, unsigned char *msg1, unsigned char *msg2, unsigned char *msg3)
{
  for (int i = 0; i < HASH_LEN; i++)
  {
    if (i < 8)
      msg1[i] = hash[i];
    else if (i < 16)
      msg2[i] = hash[i];
    else
      msg3[i] = hash[i];
    i++;
  }
}

void SendAuthMessages(int id, int dlc, uint8_t *auth_hash)
{
  unsigned char auth_msg1[8];
  unsigned char auth_msg2[8];
  unsigned char auth_msg3[4];
  // Divide the hash up among 3 messages (2 and a half really; the last 32-bits will be for timestamp
  FillAuthMessages(auth_hash, &auth_msg1[0], &auth_msg2[0], &auth_msg3[0]);
  CAN0.sendMsgBuf(id, CAN_EXTID, 8, auth_msg1);
  CAN0.sendMsgBuf(id, CAN_EXTID, 8, auth_msg2);
  CAN0.sendMsgBuf(id, CAN_EXTID, 4, auth_msg3);
}

