/*
	MicrochipLoRaModem.cpp - SmartLiving.io LoRa Arduino library 
*/

#include "MicrochipLoRaModem.h"
#include "StringLiterals.h"
#include "utils.h"

#define PORT 0x01

#define PACKET_TIME_OUT 45000


typedef struct StringEnumPair
{
	const char* stringValue;
	uint8_t enumValue;
} StringEnumPair;

unsigned char microchipSendBuffer[DEFAULT_PAYLOAD_SIZE];

MicrochipLoRaModem::MicrochipLoRaModem(Stream* stream)
{
	_stream = stream;
}

void MicrochipLoRaModem::Stop()
{
	Serial.println("[resetDevice]");

	_stream->print(STR_CMD_RESET);
	_stream->print(CRLF);

	expectString(STR_DEVICE_TYPE);
}

void MicrochipLoRaModem::SetLoRaWan(bool adr)
{
	//lorawan should be on by default (no private supported)
	setMacParam(STR_ADR, BOOL_TO_ONOFF(adr));			//set to adaptive variable rate transmission
}

unsigned int MicrochipLoRaModem::getDefaultBaudRate() 
{ 
	return 57600; 
};

void MicrochipLoRaModem::SetDevAddress(unsigned char* devAddress)
{
	Serial.println("Setting the DevAddr");
	setMacParam(STR_DEV_ADDR, devAddress, 4); 
}

void MicrochipLoRaModem::SetAppKey(unsigned char* appKey)
{
	Serial.println("Setting the AppSKey");   
	setMacParam(STR_APP_SESSION_KEY, appKey, 16);
}

void MicrochipLoRaModem::SetNWKSKey(unsigned char*  nwksKey)
{
	Serial.println("Setting the NwkSKey");
	setMacParam(STR_NETWORK_SESSION_KEY, nwksKey, 16);
}

void MicrochipLoRaModem::Start()
{
	Serial.println("Sending the network start commands");
		
	_stream->print(STR_CMD_JOIN);
	_stream->print(STR_ABP);
	_stream->print(CRLF);
	
	Serial.print(STR_CMD_JOIN);
	Serial.print(STR_ABP);
	Serial.print(CRLF);

	if(expectOK() && expectString(STR_ACCEPTED))
		Serial.println("success");
	else
		Serial.println("failure");
}

bool MicrochipLoRaModem::Send(LoraPacket* packet, bool ack)
{
	unsigned char length = packet->Write(microchipSendBuffer);
	Serial.println("Sending payload: ");
	for (unsigned char i = 0; i < length; i++) {
		printHex(microchipSendBuffer[i]);
	}
	Serial.println();
		
	unsigned char result;
	if(ack == true)
	{
		if (!setMacParam(STR_RETRIES, MAX_SEND_RETRIES))		// not a fatal error -just show a debug message
			Serial.println("[send] Non-fatal error: setting number of retries failed.");
		result = macTransmit(STR_CONFIRMED, microchipSendBuffer, length) == NoError;
	}
	else
		result = macTransmit(STR_UNCONFIRMED, microchipSendBuffer, length) == NoError;
	if(result)
		Serial.println("Successfully sent packet");
	else
		Serial.println("Failed to send packet");
	return result;
}


unsigned char MicrochipLoRaModem::macTransmit(const char* type, const unsigned char* payload, unsigned char size)
{
	_stream->print(STR_CMD_MAC_TX);
	_stream->print(type);
	_stream->print(PORT);
	_stream->print(" ");
	
	Serial.print(STR_CMD_MAC_TX);
	Serial.print(type);
	Serial.print(PORT);
	Serial.print(" ");

	for (int i = 0; i < size; ++i)
	{
		_stream->print(static_cast<char>(NIBBLE_TO_HEX_CHAR(HIGH_NIBBLE(payload[i]))));
		_stream->print(static_cast<char>(NIBBLE_TO_HEX_CHAR(LOW_NIBBLE(payload[i]))));
		
		Serial.print(static_cast<char>(NIBBLE_TO_HEX_CHAR(HIGH_NIBBLE(payload[i]))));
		Serial.print(static_cast<char>(NIBBLE_TO_HEX_CHAR(LOW_NIBBLE(payload[i]))));
	}
	_stream->print(CRLF);
	
	Serial.print(CRLF);

	// TODO lookup error
	if (!expectOK())
		return TransmissionFailure;

	Serial.println("Waiting for server response");
	unsigned long timeout = millis() + RECEIVE_TIMEOUT;
	while (millis() < timeout)
	{
		Serial.print(".");
		if (readLn() > 0)
		{
			Serial.print(".");
			Serial.print("(");
			Serial.print(this->inputBuffer);
			Serial.print(")");

			if (strstr(this->inputBuffer, " ") != NULL) // to avoid double delimiter search 
			{
				// there is a splittable line -only case known is mac_rx
				Serial.println("Splittable response found");
				onMacRX();
				return NoError; // TODO remove
			}
			else if (strstr(this->inputBuffer, STR_RESULT_MAC_TX_OK))
			{
				// done
				Serial.println("Received mac_tx_ok");
				return NoError;
			}
			else
			{
				Serial.println("Some other string received (error)");
				return lookupMacTransmitError(this->inputBuffer);
			}
		}
	}
	Serial.println("Timed-out waiting for a response!");
	return Timeout;
}

uint8_t MicrochipLoRaModem::lookupMacTransmitError(const char* error)
{
	Serial.print("[lookupMacTransmitError]: ");
	Serial.println(error);

	if (error[0] == 0)
	{
		Serial.println("[lookupMacTransmitError]: The string is empty!");
		return NoResponse;
	}

	StringEnumPair errorTable[] =
	{
		{ STR_RESULT_OK, NoError },
		{ STR_RESULT_INVALID_PARAM, TransmissionFailure },
		{ STR_RESULT_NOT_JOINED, TransmissionFailure },
		{ STR_RESULT_NO_FREE_CHANNEL, TransmissionFailure },
		{ STR_RESULT_SILENT, TransmissionFailure },
		{ STR_RESULT_FRAME_COUNTER_ERROR, TransmissionFailure },
		{ STR_RESULT_BUSY, TransmissionFailure },
		{ STR_RESULT_MAC_PAUSED, TransmissionFailure },
		{ STR_RESULT_INVALID_DATA_LEN, TransmissionFailure },
		{ STR_RESULT_MAC_ERROR, TransmissionFailure },
		{ STR_RESULT_MAC_TX_OK, NoError }
	};

	for (StringEnumPair* p = errorTable; p->stringValue != NULL; ++p) 
	{
		if (strcmp(p->stringValue, error) == 0)
		{
			Serial.print("[lookupMacTransmitError]: found ");
			Serial.println(p->enumValue);

			return p->enumValue;
		}
	}

	Serial.println("[lookupMacTransmitError]: not found!");
	return NoResponse;
}


// waits for string, if str is found returns ok, if other string is found returns false, if timeout returns false
bool MicrochipLoRaModem::expectString(const char* str, unsigned short timeout)
{
	Serial.print("[expectString] expecting ");
	Serial.println(str);

	unsigned long start = millis();
	while (millis() < start + timeout)
	{
		Serial.print(".");

		if (readLn() > 0)
		{
			Serial.print("(");
			Serial.print(this->inputBuffer);
			Serial.print(")");

			// TODO make more strict?
			if (strstr(this->inputBuffer, str) != NULL)
			{
				Serial.println(" found a match!");
				return true;
			}
			return false;
		}
	}

	return false;
}

unsigned short MicrochipLoRaModem::readLn(char* buffer, unsigned short size, unsigned short start)
{
	int len = _stream->readBytesUntil('\n', buffer + start, size);
	if(len > 0)
		this->inputBuffer[start + len - 1] = 0; // bytes until \n always end with \r, so get rid of it (-1)
	else
		this->inputBuffer[start] = 0;

	return len;
}

bool MicrochipLoRaModem::expectOK()
{
	return expectString(STR_RESULT_OK);
}

// paramName should include the trailing space
bool MicrochipLoRaModem::setMacParam(const char* paramName, const unsigned char* paramValue, unsigned short size)
{
	Serial.print("[setMacParam] ");
	Serial.print(paramName);
	Serial.print("= [array]");

	_stream->print(STR_CMD_SET);
	_stream->print(paramName);
	Serial.print(STR_CMD_SET);
	Serial.print(paramName);

	for (unsigned short i = 0; i < size; ++i)
	{
		_stream->print(static_cast<char>(NIBBLE_TO_HEX_CHAR(HIGH_NIBBLE(paramValue[i]))));
		_stream->print(static_cast<char>(NIBBLE_TO_HEX_CHAR(LOW_NIBBLE(paramValue[i]))));
		
		Serial.print(static_cast<char>(NIBBLE_TO_HEX_CHAR(HIGH_NIBBLE(paramValue[i]))));
		Serial.print(static_cast<char>(NIBBLE_TO_HEX_CHAR(LOW_NIBBLE(paramValue[i]))));
	}
	
	_stream->print(CRLF);
	Serial.print(CRLF);

	return expectOK();
}

// paramName should include the trailing space
bool MicrochipLoRaModem::setMacParam(const char* paramName, uint8_t paramValue)
{
	Serial.print("[setMacParam] ");
	Serial.print(paramName);
	Serial.print("= ");
	Serial.println(paramValue);

	_stream->print(STR_CMD_SET);
	_stream->print(paramName);
	_stream->print(paramValue);
	_stream->print(CRLF);
	
	Serial.print(STR_CMD_SET);
	Serial.print(paramName);
	Serial.print(paramValue);
	Serial.print(CRLF);

	return expectOK();
}

// paramName should include the trailing space
bool MicrochipLoRaModem::setMacParam(const char* paramName, const char* paramValue)
{
	//Serial.print("[setMacParam] ");
	//Serial.print(paramName);
	//Serial.print("= ");
	//Serial.println(paramValue);

	_stream->print(STR_CMD_SET);
	_stream->print(paramName);
	_stream->print(paramValue);
	_stream->print(CRLF);
	
	Serial.print(STR_CMD_SET);
	Serial.print(paramName);
	Serial.print(paramValue);
	Serial.print(CRLF);
	
	return expectOK();
}


//process any incoming packets from the modem
 void MicrochipLoRaModem::ProcessIncoming()
 {
	readLn();
 }

void MicrochipLoRaModem::printHex(unsigned char hex)
{
  char hexTable[16] = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F' };
  Serial.print(hexTable[hex /16]);
  Serial.print(hexTable[hex % 16]);
  Serial.print(' ');
}

unsigned char MicrochipLoRaModem::onMacRX()
{

	// parse inputbuffer, put payload into packet buffer
	char* token = strtok(this->inputBuffer, " ");

	// sanity check
	if (strcmp(token, STR_RESULT_MAC_RX) != 0){
	Serial.println("no mac_rx found in result");
		return NoResponse; // TODO create more appropriate error codes
	}

	// port
	token = strtok(NULL, " ");

	// payload
	token = strtok(NULL, " "); // until end of string
	memcpy(this->receivedPayloadBuffer, token, strlen(token) + 1); // TODO check for buffer limit

	return NoError;
}