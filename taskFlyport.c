#include "taskFlyport.h"
#include "grovelib.h"
#include "cJSON.h"
#include "rgb.h"

//#define APP_DEBUG

// Grove devices variables
int i;
const int ledNumber = 4;
BOOL passageCatch = FALSE;

// TCP Client Socket variables
#define tcpBuffSize 2000
// Please, be sure your TCPClient Socket inside openPicus IDE wizard is greater than 2000 BYTE
char tcpReply [tcpBuffSize];
unsigned int tcpRxLength = 0;
BOOL tcpClConn = FALSE;
BOOL clconnClient = FALSE;
TCP_SOCKET tcpSockCl = INVALID_SOCKET;
char hostName[] = "free.worldweatheronline.com"; //"217.174.250.32";//"free.worldweatheronline.com";
char hostPort[] = "80";
const char weatherReq_01[] = "GET /feed/weather.ashx?q=";
const char weatherReq_02[] = "&format=json&num_of_days=1&key=";
const char weatherReq_03[] = " HTTP/1.1\r\nHost: free.worldweatheronline.com\r\n\r\n";
const char weatherKey[] = "PUT_YOUR_KEY_HERE";
const char cityNation[] = "Rome,Italy"; // Change City and Nation
char weatherRequest[250];

int globalWeatherCode = 0;
int currWeatherVal = 0;
int weatherVal = 0;
float globalPrecipMM = 0.0;
float currVal = 0.0;
float precipVal = 0.0;

BOOL weatherUpdated = FALSE;

void ParseWeather();
void SendWeatherRequest();

DWORD tick1;
DWORD tick2;

void FlyportTask()
{	
	// Update tick1...
	tick1 = TickGetDiv64K();
	
	vTaskDelay(100);
	UARTWrite(1,"Umbrella Stand Hack by Simone Marra!\r\n");

	// GROVE board
	void *board = new(GroveNest);

	// GROVE devices	
	// Digital Input
	void *button = new(Dig_io, IN);
	attachToBoard(board, button, DIG1);
	
	// Initialize PIR Motion sensor input
	void *PIRmotion = new(Dig_io, IN); // PIR Motion Sensor (SENS32357P)
	attachToBoard(board, PIRmotion, DIG2);
	
	// Initialize RGB leds
	void *rgb = new(Rgb, ledNumber);
 
	// Attach devices
	attachToBoard(board, rgb, DIG4);
 
	// Initialize devices
	for(i = 0; i < ledNumber; i++)
	{
		configure(rgb, (BYTE)(i+1), 0, 0, 0);
	}
	set(rgb, ON);
	
	// Connection to Network
	#if defined (FLYPORT)
	// Open Wi-Fi connection
	if( WFCustomExist() == TRUE )
	{
		WFConnect(WF_CUSTOM);
	}
	else
	{
		WFConnect(WF_DEFAULT);
	}
	// Wait for connection completed:
	while( WFStatus != CONNECTED );
	// wait for DHCP...
	while(!DHCPAssigned)
	{
		UARTWriteCh(1, '.');
		vTaskDelay(10);
	}
	#endif
	#if defined (FLYPORTETH)
	while(!MACLinked);
	#endif
	vTaskDelay(150);
	UARTWrite(1, "Flyport connected to network!\r\nReady for weather feeds...\r\n");

	// Prepare HTTP Request
	sprintf(weatherRequest, "%s", weatherReq_01);
	strcat(weatherRequest, cityNation);
	strcat(weatherRequest, weatherReq_02);
	strcat(weatherRequest, weatherKey);
	strcat(weatherRequest, weatherReq_03);
	#ifdef APP_DEBUG
	UARTWrite(1, "Request:\r\n");
	UARTWrite(1, weatherRequest);
	UARTWrite(1, "\r\n");
	#endif
	
	// Try to open TCP Client Connection:
	tcpSockCl = TCPClientOpen(hostName, hostPort);
	// wait a while before to send a request
	vTaskDelay(100);
	// Check TCP Connection:
	tcpClConn = TCPisConn(tcpSockCl);
	if( tcpClConn == TRUE )
	{
		if( clconnClient == FALSE )
		{
			clconnClient = TRUE;
			// Send the request at startup
			UARTWrite(1, "sending request...");
			SendWeatherRequest();
		}
	}
	
	while(1)
	{
		// Catch and store the passage only once at time
		if( passageCatch == FALSE )
		{
			if( get(PIRmotion) == TRUE )
			{
				passageCatch = TRUE;
			}
		}		
		
		if (passageCatch == TRUE)
		{
			passageCatch = FALSE;
			IOPut(p21, on);
			set(rgb, ON);
		}
		else
		{
			IOPut(p21, off);
			set(rgb, OFF);
		}
	
		// Send a new request every 15 minutes...
		tick2 = TickGetDiv64K();
		if( (tick2 - tick1) > 900) // 900 ticks is about 900 seconds, or 15 minutes... not accurate calculation, but right enough for application purposes
		{
			// Update tick1...
			tick1 = TickGetDiv64K();
			vTaskDelay(1);
			UARTWrite(1, "sending request...");
			SendWeatherRequest();
		}
		
		// Check TCP Connection:
		tcpClConn = TCPisConn(tcpSockCl);
		if( tcpClConn == TRUE )
		{
			if( clconnClient == FALSE )
			{
				clconnClient = TRUE;
				//tcpSockCl = TCPClientOpen(hostName, hostPort);
			}
		}
		else
		{
			if( clconnClient == TRUE )
			{
				clconnClient = FALSE;
				TCPClientClose(tcpSockCl);
				tcpSockCl = INVALID_SOCKET;
			}
		}
		
		// Check TCPClient RX buffer:
		if( tcpSockCl == INVALID_SOCKET )
		{
			tcpSockCl = TCPClientOpen(hostName, hostPort);
		}
		else
		{
			tcpRxLength = TCPRxLen(tcpSockCl);
			if(tcpRxLength > 0)
			{
				UARTWrite(1, "receiving...\r\n");
				// Reset char array...
				int s;
				for (s=0; s < tcpBuffSize; s++)
					tcpReply[s] = '\0';
				int lenCount = 0;
				
				do
				{
					char tmp[251];
					tcpRxLength = TCPRxLen(tcpSockCl);
					if( tcpRxLength > 250)
						tcpRxLength = 250;
					TCPRead(tcpSockCl, tmp, tcpRxLength);
					lenCount += tcpRxLength;
					if(lenCount > tcpBuffSize)
						break;
					vTaskDelay(15);
					strcat(tcpReply, tmp);
				} while( TCPRxLen(tcpSockCl) > 0 );
				tcpReply[lenCount] = '\0';
				
				// Parse JSON string
				ParseWeather();
			}
		}
		
		// Force Request on button pressure:
		if( get(button) != 0 )
		{
			vTaskDelay(5);
			if( get(button) != 0 )
			{
				UARTWrite(1, "Button forced request...");
				UARTFlush(1);
				SendWeatherRequest();
				vTaskDelay(50);
			}
		}

		
		if (weatherUpdated == TRUE)
		{
			weatherUpdated = FALSE;
			
			// Now store the new value for globalWeatherCode...
			if( weatherVal > currWeatherVal)
			{
				globalWeatherCode = weatherVal;
			}
			else
			{
				globalWeatherCode = currWeatherVal;
			}
			//  ...and the new one for globalPrecipMM
			if( precipVal > currVal )
			{
				globalPrecipMM = precipVal;
			}
			else
			{
				globalPrecipMM = currVal;
			}
			// Now update LEDs status/color
			//int i;
			/*	We could set a weatherCode upper limits as:
				0 - 118 	->	no need umbrella (clear/sunny to cloudy)
				119 - 283 	->	needs umbrella and / or freezing warning
				284 - 395   ->  Heavy rain or snow... prepare yourself, it could be lot of traffic :(
			*/
			// clear/sunny to cloudy
			if( globalWeatherCode < 119 )
			{
				// Initialize devices
				for(i = 0; i < ledNumber; i++)
				{
					configure(rgb, (BYTE)(i+1), 0, 255, 0);
				}
			}
			// needs umbrella but not heavy rain
			else if( globalWeatherCode < 284 )
			{
				// Initialize devices
				for(i = 0; i < ledNumber; i++)
				{
					configure(rgb, (BYTE)(i+1), 0, 0, 255);
				}
			}
			// Heavy rain or snow
			else
			{
				// Initialize devices
				for(i = 0; i < ledNumber; i++)
				{
					configure(rgb, (BYTE)(i+1), 255, 0, 0);
				}
			}			
		}
	}
}


void SendWeatherRequest()
{
	if( clconnClient == TRUE)
	{
		TCPWrite(tcpSockCl, weatherRequest, strlen(weatherRequest));
		UARTWrite(1, "sent!\r\n");
	}
}

void ParseWeather()
{
	// JSON variable
	cJSON *weatherJSON;

	if( TCPRxLen(tcpSockCl) == 0)
	{
		// prepare parsed JSON
		char *json2parse = strchr(tcpReply, '{');
					
		#ifdef APP_DEBUG
			UARTWrite(1, "\r\n***json2parse:\r\n");
			UARTWrite(1, json2parse);
			UARTWrite(1, "\r\n***\r\n");
		#endif
		
		weatherJSON = cJSON_Parse(json2parse);
		
		if(weatherJSON == 0)
		{
			UARTWrite(1, "unable to parse....\r\n");
		}
		else
		{
			UARTWrite(1, "JSON Parsed!\r\n");
			
			cJSON *data, *current_condition, *cloudcover, *currentPrecipMM;
			cJSON *weather, *precipMM, *weatherCode;

			char dbgMsg[80];
				
			// First point to JSON->"data";
			data = weatherJSON->child;
			if(data != NULL)
			{
				// data has an array: "current_condition"
				current_condition = cJSON_GetObjectItem(data, "current_condition");
			}
			else
			{
				UARTWrite(1, "unable to create data\r\n");
			}
			
			if(current_condition != NULL)
			{
				// After point to "cloudcover" element...
				cJSON *item0;
				item0 = cJSON_GetArrayItem(current_condition, 0);
				
				if(item0 != NULL)
				{
					cloudcover = item0->child;
				}
				else
					UARTWrite(1, "unable to create item1...\r\n");
			}
			else
			{
				UARTWrite(1, "unable to create current_condition\r\n");
			}
			
			if(cloudcover != NULL)
			{
				currentPrecipMM = cloudcover->next->next->next;
			}
			else
			{
				UARTWrite(1, "unable to create cloudcover\r\n");
			}
			
			if(currentPrecipMM != NULL)
			{
				char* tmpVal;
				tmpVal = cJSON_Print(currentPrecipMM);
				currVal = atof(tmpVal+1);
				cJSON *currWeatherCode;
				currWeatherCode = currentPrecipMM->next->next->next->next->next;
				char* currWeatherStr;
				currWeatherStr = cJSON_Print(currWeatherCode);
				currWeatherVal = atoi(currWeatherStr+1);
				
				sprintf(dbgMsg, "currentvalues:\r\n> precipMM: %.2f - weatherCode: %d\r\n", 
						(double)currVal, (int)currWeatherVal);
				UARTWrite(1, dbgMsg);
			}
			else
			{
				UARTWrite(1, "unable to create currentPrecipMM\r\n");
			}
			
			weather = current_condition->next->next;
			cJSON *item0 = cJSON_GetArrayItem(weather, 0);
			precipMM = item0->child->next;
			char* tmpPrecipVal;
			tmpPrecipVal = cJSON_Print(precipMM);
			precipVal = atof(tmpPrecipVal+1);
			weatherCode = precipMM->next->next->next->next->next;
			char* tmpWeatherVal;
			tmpWeatherVal = cJSON_Print(weatherCode);
			weatherVal = atoi(tmpWeatherVal+1);
			
			sprintf(dbgMsg, "daily values:\r\n> precipMM: %.2f - weatherCode: %d\r\n", 
					(double)precipVal, (int)weatherVal);
			UARTWrite(1, dbgMsg);
			
			weatherUpdated = TRUE;
			
			tcpReply[0] = '\0';
			
			// Delete JSON structure from memory
			cJSON_Delete(weatherJSON);
		}
	}
}
