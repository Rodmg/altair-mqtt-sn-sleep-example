#include <MQTTSNClient.h>
#include <WSNetwork.h>
#include <VirtualTimer.h>
#include <LowPower.h>
#include <Mesh.h>

/*
  MQTT-SN Client example with sleep support.
  - It subscribes to "client/subscribe" and publishes to "client/publish"
  - Each 32 seconds, it publishes "Hello world" to "client/publish"
  - Any other time, it will be sleeping (checks for pending messages every 16 seconds).
  - If there is a publication to "client/subscribe", the message will be received when
  the device wakes up (every 16 seconds).
  - When it wakes up, it waits 5 seconds before going to sleep again, for giving enough time
  for any pending network tasks.
  - If we are in pair mode or where never connected, it doesn't go to sleep.
*/

using namespace MQTTSN;

WSNetwork network;
Client client(network, 1000);

// Topic strings
const char lastWill[] = "lastwill";
const char publishTopic[] = "client/publish";
const char subscribeTopic[] = "client/subscribe";

// Timer for publishing example topic repeatedly
VirtualTimer timer;

// Pair support
#define PAIR_BTN 33
#define PAIR_LED 14

void setupPair()
{
  pinMode(PAIR_BTN, INPUT);
  pinMode(PAIR_LED, OUTPUT);
  digitalWrite(PAIR_LED, HIGH);
}

void attendPairToggle()
{
  if(!digitalRead(PAIR_BTN))
  {
    if(network.inPairMode())
    {
      network.enterNormalMode();
      digitalWrite(PAIR_LED, HIGH);
    }
    else
    {
      network.enterPairMode();
      digitalWrite(PAIR_LED, LOW);
    }
    delay(1000);
  }
  // Manage indicator LED state
  if(network.inPairMode()) digitalWrite(PAIR_LED, LOW);
  else digitalWrite(PAIR_LED, HIGH);
}

// Sleep support
#define SLEEP_DURATION 16
#define SLEEP_WAIT 5000 // time to wait before going to sleep (ms)
#define PUBLISH_INTERVAL 32000

bool shouldSleep = false;
VirtualTimer sleepTimer; // Controls the time to wait before going to sleep

void sleep()
{
  client.disconnect(SLEEP_DURATION);
  network.sleep();
  // Sleep for ~16 seconds
  for(uint8_t i = 0; i < 2; i++)
  {
    LowPower.powerDown(SLEEP_8S, ADC_OFF, BOD_ON);
    // (Hack) Adjust publish timer (millis doesn't work when sleeping, so the timer won't update by itself)
    unsigned long newTimerVal = 0;
    if(timer.left_ms() >= 8000) newTimerVal = timer.left_ms() - 8000;
    timer.countdown_ms(newTimerVal);
    // (end Hack)
  }
  Mesh.wakeup(); // TODO: add a network.wakeup() function to WSNetwork
  client.awake(); // MQTT-SN "awake" state, not fully connected, only checks for pending messages
}

void wakeup()
{
  // For a full wakeup (e.g. for publishing), we need to fully reconnect
  if(!client.isConnected()) connectMqtt();
  shouldSleep = false;
  sleepTimer.countdown_ms(SLEEP_WAIT);
}


// Function that handles the "client/subscribe" topic
void subscribeHandler(struct MQTTSN::MessageData &msg)
{
  Serial.print("Got message, Topic: ");
  Serial.println(subscribeTopic);
  Serial.print("Message: ");
  for(uint8_t i = 0; i < msg.message.payloadlen; i++)
  {
    Serial.print(((char*)msg.message.payload)[i]);
  }
  Serial.println();
}

// Function for connecting with gateway
bool connectMqtt()
{
  Serial.println("Connecting MQTT...");
  // Set MQTT last will topic and blank message
  client.setWill(lastWill, NULL, 0);
  // Setup MQTT connection
  static MQTTSNPacket_connectData options = MQTTSNPacket_connectData_initializer;
  options.duration = 10; // Keep alive interval, Seconds
  options.cleansession = true;
  options.willFlag = true;
  int status = client.connect(options);

  if(status != SUCCESS) return false;
  // Clear any previous subscriptions (useful if we are reconnecting)
  client.clearSubscriptions();
  client.clearRegistrations();
  // We need to first register the topics that we may publish (a diference between this MQTT-SN implementation and MQTT)
  client.registerTopic(publishTopic, strlen(publishTopic));
  // Subscribe a function handler to a topic
  client.subscribe(subscribeTopic, QOS1, subscribeHandler);

  return true;
}

void yield()
{
  network.yield();
}

void setup()
{
  Serial.begin(57600);
  // Start with automatic address given by Aquila Mesh
  //char pass[16] = {1,2,3,4,5,6,7,8,9,0,1,2,3,4,5,6};
  //network.begin(0x07, 12, pass);
  network.begin();
  // Start timer for publishing "client/publish" every 5 seconds
  timer.countdown_ms(PUBLISH_INTERVAL);
  sleepTimer.countdown_ms(SLEEP_WAIT);
  setupPair();
}

void loop()
{
  // Attend network tasks
  yield();
  attendPairToggle();
  if(network.inPairMode())
  {
    network.loop();
    return;
  }

  if(!shouldSleep)
  {
    client.loop();
    if(!client.isConnected())
    {
      Serial.println("MQTT disconnected, trying to reconnect...");
      if(!connectMqtt()) return;
      shouldSleep = true;
    }
  }
  else
  {
    // Go to sleep for 16 seconds
    sleep();
  }

  // Attend timer
  if(timer.expired())
  {
    wakeup();
    char payload[] = "Hello world";
    bool retained = false;
    // Publish "client/publish"
    client.publish(publishTopic, payload, strlen(payload), QOS1, retained);
    // Restart timer
    timer.countdown_ms(PUBLISH_INTERVAL);
    Serial.println("Published message");
  }

  // Mark to go to sleep after sleepTimer expires
  if(sleepTimer.expired())
  {
    shouldSleep = true;
  }


}
