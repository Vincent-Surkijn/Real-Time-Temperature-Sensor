#define configUSE_IDLE_HOOK 1;

#include <Arduino_FreeRTOS.h>
#include <semphr.h>
#include <EEPROM.h>
#include <LoRa.h>
#include <avr/sleep.h>
#include <avr/power.h>

#define SCK     15
#define MISO    14
#define MOSI    16
#define SS      8
#define RST     4
#define DI0     7
#define BAND    869300000
#define PABOOST true

SemaphoreHandle_t mutex;

TaskHandle_t xHandleReadInput;

TaskHandle_t xHandleListen;

TaskHandle_t xHandleStore;

TaskHandle_t xHandleSend;

bool ultraLowPower = false;
bool done1 = false;
bool done2 = false;
int messages = 0;
int temp2send = 0;
int seconds2store = 0;

void setup() {
  Serial.begin(9600);
  while(!Serial){;}

  int val = EEPROM.read(0);
  if (val == 255){    // If 255 stored: slot is empty so no temperatures recorded yet
    EEPROM.put(0,3);  // Set first free address to 3 because int with first free address takes first 2 addresses and bool cycle takes 1 address
    EEPROM.put(2,false);  // Set cycle to false
  }
  
  mutex = xSemaphoreCreateMutex();
  if (mutex == NULL) {
    Serial.println("Mutex creation failed");
  }

  LoRa.setPins(SS,RST,DI0);
  if(!LoRa.begin(BAND, PABOOST)){
    Serial.println("Starting LoRa failed!");
    while(1);
  }

  xTaskCreate(TaskReadInput,"ReadInput",128,NULL,2,&xHandleReadInput);
  xTaskCreate(TaskListen,"Listen",128,NULL,2,&xHandleListen);

}

void loop() {
  if(ultraLowPower){
    saveEnergy();  
  }
}

void vApplicationIdleHook( void )
{
    //Serial.print("sleeping");
    LoRa.sleep();
    power_adc_disable();
    power_twi_disable();
}

void TaskReadInput(void *pvParameters){
    (void) pvParameters;
    for(;;){
      char c;    
      if(Serial.available()>0){
         c = Serial.read();
         if(c == '1'){  // Print last received value
            if (xSemaphoreTake(mutex, 10) == pdTRUE){
              //Serial.println("that was 1");
              printLastValue();
              xSemaphoreGive(mutex);
            }  
         }
         else if(c == '2'){   // Print all values
            if (xSemaphoreTake(mutex, 10) == pdTRUE){
              //Serial.println("that was 2");
              printValues();
              xSemaphoreGive(mutex);
            }
         }
         else if(c == '3'){   // Turn on low power mode
            if (xSemaphoreTake(mutex, 10) == pdTRUE){
              //Serial.println("that was 3");
              Serial.println("Low power mode");
              vTaskSuspend(xHandleListen);
              ultraLowPower = true;
              saveEnergy();
              xSemaphoreGive(mutex);
            }
         }
         else if(c == 'd'){
            Serial.println("that was d");   // Debug: start listening again
            vTaskResume(xHandleListen);
         }
         else if(c == 'q'){
            Serial.println("that was q");   // Debug: stop listening
            vTaskSuspend(xHandleListen);
         }
         else{
           Serial.print("Read: ");
           Serial.println(c);
         }
      }     
    }
}

void printLastValue(){
  Serial.println("");
  Serial.println("Printing last value (if nothing is stored yet this will be an invalid value)"); 
  int firstFree = EEPROM.read(0);
  int temp;
  int next;

  EEPROM.get(firstFree-sizeof(next),next);  // seconds are stored last
  
  EEPROM.get(firstFree-sizeof(next)-sizeof(temp),temp); // temp is stored first

  Serial.println("");
  Serial.print("Last temp: ");
  Serial.println(temp);
  Serial.print("Last next: ");
  Serial.println(next);
}

void printValues(){
  Serial.println("");
  Serial.println("Printing");
  int firstFree = EEPROM.read(0); 
  bool cycle = EEPROM.read(2);
  if(cycle) firstFree = EEPROM.length();  // if EEPROM already cycled all values will be read
  int j = 3;    
  while(j <= firstFree - 1){
  // First read the temperature
    int temp;
    EEPROM.get(j,temp);
    Serial.println(""); 
    //Serial.println( (j/(2*sizeof(int))) + 1);
    Serial.print("temp: ");
    Serial.println(temp);  

    j = j + sizeof(temp);

  // Now read the seconds until the next message
    int next;
    EEPROM.get(j,next);
    Serial.print("Next: ");
    Serial.println(next);  

    j = j + sizeof(next);
  }  
}

void TaskListen(void *pvParameters){
    (void) pvParameters;
    
    for(;;){
      int packetSize = LoRa.parsePacket();
      if(packetSize){ // Received a packet
        vTaskSuspend(xHandleReadInput);   // once first message is received UI stops
        Serial.println("");
        messages++; // Increment amount of messages
        Serial.print(messages);
        Serial.println(" messages received");

        // read packet
        char GW[4];        
        int i = 0;
        while(i<4){ // read gateway ID
          GW[i]=(char)LoRa.read();      
          i++;
        }   
        GW[4] = '\0';
        int seconds = LoRa.parseInt();  // Read seconds until next package
        LoRa.idle();
        Serial.print("Gateway: ");
        Serial.println(GW);
        Serial.print("Time until next: ");
        Serial.println(seconds);

        int temp = ReadTemp();  // read temperature from sensor

        temp2send = temp;
        seconds2store = seconds;
        done1 = false;
        done2 = false;
        
        if(messages == 1){  // if this is the first time a message is received, the tasks still need to be created
          xTaskCreate(TaskSend,"Send",128,NULL,2,&xHandleSend);
          xTaskCreate(TaskStore,"Store",128,NULL,2,&xHandleStore);
        }
        else{   // tasks are already created
          vTaskResume(xHandleStore);
          vTaskResume(xHandleSend);
        }
        // Can stop listening after 20 messages
        if(messages>=20){
          while(!done1 || !done2) {vTaskDelay(1);}   // wait for store and send tasks to finish before entering deep sleep
          ultraLowPower = true;
          saveEnergy();
          vTaskSuspend(NULL);
        }
        
        // Go back to idle(low power) now
        seconds = seconds * 1000; // convert to ms
        seconds = seconds - 50;  // give 50ms spare time to go to sleep, wake up, turn on LoRa module...
        vTaskDelay(seconds/portTICK_PERIOD_MS); // go to idle for ms/period ticks
       
        //Serial.println("Awake");
      }
    }
}

void TaskSend(void *pvParameters){
    (void) pvParameters;
    
    for(;;){
      //Serial.print("sending");
      LoRa.beginPacket();
      LoRa.print(temp2send);
      int res = LoRa.endPacket();
      if(!res) Serial.println("Packet couldn't be sent");
      done2 = true;
      vTaskSuspend(NULL);
   }
}

void TaskStore(void *pvParameters){
    (void) pvParameters;
    
    for(;;){
      //Serial.print("storing");
      if (xSemaphoreTake(mutex, 10) == pdTRUE){
        storeValue(temp2send, true);
        storeValue(seconds2store, false);
        xSemaphoreGive(mutex);
      }
      done1 = true;
      vTaskSuspend(NULL);
    }
}

void storeValue( int value, bool temp ){   
    // First free address is stored in address 0 of the EEPROM
    int firstFree = EEPROM.read(0);
    if( (EEPROM.length()-firstFree) < 2*(sizeof(int)) && temp ){
      firstFree = sizeof(int);  // if not enough space to store temperature and seconds then store in first address after value of first address 
      EEPROM.put(2,true);
    }

    // Store the value in the first free address
    EEPROM.put(firstFree, value);

    // Change first free address to used address + size 
    int lastUsed = firstFree + sizeof(value);
    EEPROM.put(0, lastUsed);

    // Value was stored
    //Serial.print("Stored: ");
    //Serial.println(value);
}

int ReadTemp(){
 
  int t;

  ADCSRA |= (1<<ADPS2) | (1<<ADPS1) | (1<<ADEN);  // enable ADC and set prescaler to 64
  ADMUX = 0b11000111; // 11 -> int ref, 0 -> Right Adjust
                      // 00111 -> select temp sensor 
  ADCSRB |= (1 << MUX5); // select temp sensor
  
  delay(5); // wait for voltages to become stable.

  ADCSRA |= (1<<ADSC);  // Perform Dummy Conversion to complete ADC init

  while ((ADCSRA & (1<<ADSC)) != 0);  // wait for conversion to complete
  
  ADCSRA |= (1<<ADSC); // Start the ADC
  
  // Detect end-of-conversion
  while (bit_is_set(ADCSRA,ADSC));
  byte low = ADCL;
  byte high = ADCH;
  
  t = (high << 8) |low;
  t = (t - 257) / 1.22; // Calibration

  Serial.print("Current temperature: ");
  Serial.println(t);

  return t;
}

void saveEnergy(){
  Serial.println("Entering ultra low power mode");
  noInterrupts();
  LoRa.sleep();
  power_all_disable();
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);   
  wdt_disable();
  sleep_enable();
  sleep_cpu();
}
