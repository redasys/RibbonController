#include <MIDI.h>;
MIDI_CREATE_DEFAULT_INSTANCE();

int minVal = 10;
int maxVal = 1024 - minVal;
int midVal = 512;
int prevVal = 0;
int baseVal = 0;
int curVal = 0;
int diffVal = 0;
int msblsb = 0;
int maxTravel = 0;
float maxBend = 0x2000; // this is 1/2 of a max 14 bit number
unsigned int newPWHex = 0;
float newPWValue = 0;
float scaleFactor = 0;
float tempFloat = 0;
float tFloat1, tfloat2;
float midPoint = maxBend;
bool ispressed = false;

// feature - latch after 100 loops of being pressed
int voiceAReleaseCC = 60; // DDRM  CC for VCA Env Release Voice A
int voiceBReleaseCC = 87; // Same as above for Voice B
bool holding = false;
int shouldRelease = false;
int timer, holdTicks;
unsigned int holdPWValue;

void setup()
{
  timer = 0;
  holdTicks = 0;
  holdPWValue = 0;
  // Set MIDI baud rate:
  Serial.begin(31250);
  clearPitchBend();
  MIDI.begin(1);
}

void loop()
{
  curVal = analogRead(A0); // 0 to 1023

  if (curVal > minVal) // somebody is touching the ribbon
  {
    if (!ispressed) // if this is their first touch, set flag, starting value & calculate scaling
    {
      baseVal = curVal;                                                // set starting point
      prevVal = curVal;                                                // save prev loop value
      maxTravel = curVal > midVal ? curVal - minVal : maxVal - curVal; // if we are above midpoint, use max travel as down, else max travel is up (0 to 1023)
      // we need to map our max travel to be 0x2000
      tFloat1 = maxTravel;
      scaleFactor = maxBend / tFloat1; // scale factor will convert our max possible movement to 0x2000
      if(holding){
        shouldRelease = true;
      }
      clearPitchBend();
      ispressed = true;
    }
    else // this is continued touch
    {
      if (curVal != prevVal) // only do crap if we moved from last time through
      {
        prevVal = curVal;
        diffVal = curVal - baseVal;        // figure out h0w far we strayed from start point (+ = up, - = down) -1023 to 1023
        tempFloat = scaleFactor * diffVal; // scale it to -2000h to +2000h using a float
        newPWValue = midPoint + tempFloat; // this should add in midpoint bias to get 0 to 0x4000h
        newPWHex = int(newPWValue);        // convert that back to an unsigned INT
        sendPitchBend(newPWHex);           // send it
      }
    }
  }
  else // the ribbon is not pressed this loop
  {
    if (holding || timer > 100)
    {
      if (!holding)
      {
        //preserve the bend position
        holdPWValue = newPWHex;
        holding = true;
        honsc(true);
      }
      //reset first byte
      Serial.write(0xE0);
      sendPitchBend(holdPWValue);

      if (holdTicks > 2000)
      {
        shouldRelease = true;
        clearPitchBend();
      }
      // don't reset until I say
      ispressed = false;
    }

    if (ispressed) // if it was pressed last time through, reset pitch bend
    {
      clearPitchBend();
    }    
  }
  delay(20);
}

void clearPitchBend()
{
  if (shouldRelease)
  {
    holdTicks = 0;
    holdPWValue = 0;
    holding = false;
    shouldRelease = false;

    honsc(false);
  }

  // 2000h is zero point = 0010 0000 0000 0000 but sent as a 14 bit value split into 2 x 7 =  --> 1000000 0000000 (0x20h, 0x00h)
  Serial.write(0xE0); // 1110 0000
  Serial.write(0x00); // LSB clear
  Serial.write(0x40); // MSB = 1/2 way of 7 bits
  timer = 0;
  ispressed = false; // always reset flag meh can be moved here
}

// this uses continuous status (no leading byte required)
void sendPitchBend(unsigned int pwAmount)
{
  unsigned char low = pwAmount & 0x7F;         // Low 7 bits
  unsigned char high = (pwAmount >> 7) & 0x7F; // High 7 bits
  Serial.write(low);                           // LSB
  Serial.write(high);                          // MSB
  timer++;

  if (holding)
  {
    holdTicks++;
  }
}
void honsc(bool on)
{
  int val = on ? 127 : 40;
  MIDI.sendControlChange(voiceAReleaseCC, val, 1);
  MIDI.sendControlChange(voiceBReleaseCC, val, 1);
}