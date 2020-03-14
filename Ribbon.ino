#include <MIDIUSB.h>
#include <CreateTimer.h>

int ribbon = A0;
int minVal = 20;
int maxVal = 1000;
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
int ccSwitch = 25;

// feature - latch after 100 loops of being pressed
byte voiceAReleaseCC = 0x3C; // DDRM  CC#60 for VCA Env Release Voice A
int voiceBReleaseCC = 0X57;  // Same as above for Voice B CC#87
bool holding = false;
int shouldRelease = false;
int timer, holdTicks;
unsigned int holdPWValue;
// I'd call it my death but I'll only fade alway
int vol = 0x7F;
//feedback the tx LEDs are built in constants
int RXLED = 13;
// chord feature
const int len = 5;
int chord[len] = {26, 38, 45, 50, 54};
int chordShift = 12;
int chordVol = 127;
bool vivace = true;
bool notesSent;

// timers
CreateTimer midiReadTimer;
CreateTimer tapIntervalTimer;
CreateTimer holdTimer;
CreateTimer sustainDuration;

bool shouldRead = true;

int mapCalibrated(int value)
{
    value = constrain(value, minVal, maxVal);   // make sure that the analog value is between the minimum and maximum value
    return map(value, minVal, maxVal, 0, 1023); // map the value from [minimumValue, maximumValue] to [0, 1023]
}

void setup()
{
    timer = 0;
    holdTicks = 0;
    holdPWValue = 0;
    // Set MIDI baud rate:
    Serial.begin(115200);
    Serial1.begin(9600);
    clearPitchBend();
    digitalWrite(13, HIGH);
}

void loop()
{
    if (!ispressed && shouldRead && checkTimers())
    {
        readMidi();
    }

    curVal = mapCalibrated(analogRead(ribbon));

    if (curVal > minVal) // somebody is touching the ribbon
    {
        // Serial.println(curVal);
        if (!ispressed) // if this is their first touch, set flag, starting value & calculate scaling
        {
            Serial.println("first touch");
            shouldRead = false;
            if (tapIntervalTimer.While())
            {
                shouldRead = vivace;
                chordMode(!vivace);
            }
            tapIntervalTimer.Start(500UL);
            baseVal = curVal;                                                // set starting point
            prevVal = curVal;                                                // save prev loop value
            maxTravel = curVal > midVal ? curVal - minVal : maxVal - curVal; // if we are above midpoint, use max travel as down, else max travel is up (0 to 1023)
            // we need to map our max travel to be 0x2000
            tFloat1 = maxTravel;
            scaleFactor = maxBend / tFloat1; // scale factor will convert our max possible movement to 0x2000
            bool wasHolding = holding;
            if (holding)
            {
                shouldRelease = true;
            }
            sendPitchBend(512);
            ispressed = true;

            if (vivace && !notesSent && !wasHolding)
            {
                wasHolding = false;
                holdTimer.Enable();
                holdTimer.Start(10000UL);
                presto();
            }
        }
        else // this is continued touch
        {
            Serial.println("continuing");
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
        if (vivace && (holding || holdTimer.Once()))
        {
            Serial.println("holding");
            if (!holding)
            {
                //preserve the bend position
                holdPWValue = newPWHex;
                holding = true;
                honsc(true);
                sustainDuration.Start(30000UL);
            }
            //reset first byte
            sendPitchBend(holdPWValue);
            fadeOut();

            if (vol == 0 || sustainDuration.Once())
            {
                shouldRelease = true;
                clearPitchBend();
            }
            // don't reset until I say
            ispressed = false;
        }

        if (ispressed) // if it was pressed last time through, reset pitch bend
        {
            ispressed = false;
            clearPitchBend();
        }
    }

    MidiUSB.flush();
    delay(20);
}

void clearPitchBend()
{
    Serial.println("clearPitchBend");
    reprise();
    holdTimer.Disable();
    if (shouldRelease)
    {
        holdTicks = 0;
        holdPWValue = 0;
        holding = false;
    }
    // turn the volume down}
    if (vivace)
    {
        sendControlChange(0x7, 0x0, 0x0);
        MidiUSB.flush();
    }
    sendPitchBend(512);
    MidiUSB.flush();

    // turn the volume back up
    if (vivace)
    {
        delay(150);
        sendControlChange(7, 127, 1);
        MidiUSB.flush();
    }
    

    timer = 0;
    ispressed = false; // always reset flag meh can be moved here

    //splitting this up avoids hearing the ribbon snap back.
    if (shouldRelease)
    {
        shouldRelease = false;
        //turn it back up
        resetVolume();
    }
    shouldRead = true;
}

// this uses continuous status (no leading byte required)
void sendPitchBend(int i)
{
    Serial.print("bending");
    Serial.println(i);
    unsigned int amt = i;
    if (i == 512)
    {
        amt = 8199;
    }
    unsigned char low = amt & 0x7F;         // Low 7 bits
    unsigned char high = (amt >> 7) & 0x7F; // High 7 bits

    midiEventPacket_t pb = {0x0E, 0xE0 | 0x0, low, high};
    Serial.print("low: ");
    Serial.print(low);
    Serial.print(" high: ");
    Serial.println(high);
    MidiUSB.sendMIDI(pb);
    // MSB
    timer++;

    if (holding)
    {
        holdTicks++;
    }
}

void readMidi()
{
    midiEventPacket_t rx = MidiUSB.read();
    // clearPitchBend();
    rx = MidiUSB.read();

    Serial.println(rx.header);

    if (rx.header != 0)
    {
        Serial.print("Received: ");
        Serial.print(rx.header, HEX);
        Serial.print("-");
        Serial.print(rx.byte1, HEX);
        Serial.print("-");
        Serial.print(rx.byte2, HEX);
        Serial.print("-");
        Serial.println(rx.byte3, HEX);
    }

    if (rx.header == ccSwitch)
    {
        chordMode(rx.byte3 == 63);
        clearPitchBend();
    }

    MidiUSB.sendMIDI(rx);
    MidiUSB.flush();

    shouldRead = false;
    midiReadTimer.Start(100UL);
}

void chordMode(bool on)
{

    if (on)
    {
        Serial.println("Chord Mode on");

        digitalWrite(RXLED, HIGH);
    }
    else
    {
        Serial.println("Chord mode off");
        TXLED1;
    }
    vivace = on;
}

void honsc(bool on)
{
    byte val = on ? 0x7F : 0x28;
    sendControlChange(voiceAReleaseCC, val, 0);
    sendControlChange(voiceBReleaseCC, val, 0);
    MidiUSB.flush();
}

void fadeOut()
{
    Serial.println("fadeOut");
    if (holdTicks % 5 == 0 || vol < 5)
    {
        sendControlChange(7, vol--, 1);
    }
}

void resetVolume()
{
    Serial.println("resetVolume");
    sendControlChange(voiceAReleaseCC, 0x0, 0x0);
    MidiUSB.flush();
    sendControlChange(voiceBReleaseCC, 0x0, 0x0); //we dont want the ribbon snap back ringing due to sustain is the pedal is pressed
    MidiUSB.flush();
    // only if we faded out
    if (vol == 0)
    {
        reprise();
        MidiUSB.flush();
        sendControlChange(0x40, 0x0, 0x0);
        MidiUSB.flush();
    }
    vol = 127;
    MidiUSB.flush();
    // flashOff();
    sendControlChange(0x7, vol, 0x0);
    honsc(false);
    MidiUSB.flush();
    chordMode(true);
    MidiUSB.flush();
}

void sendControlChange(byte control, byte value, byte channel)
{
    Serial.println("sendControlChange");
    channel = 0x0; // it's always channel 1 anyway...
    midiEventPacket_t event = {0x0B, 0xB0 | channel, control, value};
    MidiUSB.sendMIDI(event);
}

void noteOn(byte channel, byte pitch, byte velocity)
{
    midiEventPacket_t noteOn = {0x09, 0x90 | channel, pitch, velocity};
    Serial.print("sending: ");
    Serial.print(noteOn.header, HEX);
    Serial.print("-");
    Serial.print(noteOn.byte1, HEX);
    Serial.print("-");
    Serial.print(noteOn.byte2, HEX);
    Serial.print("-");
    Serial.println(noteOn.byte3, HEX);
    MidiUSB.sendMIDI(noteOn);
}

void noteOff(byte channel, byte pitch, byte velocity)
{
    midiEventPacket_t noteOff = {0x08, 0x80 | channel, pitch, velocity};
    MidiUSB.sendMIDI(noteOff);
}

void presto()
{
    if (!vivace)
        return;
    Serial.println("Presto");
    notesSent = true;
    for (int i = 0; i < len; i++)
    {
        noteOn(0, chord[i] + chordShift, chordVol);
    }
    sendControlChange(0x40, 0x7F, 0x0);

    MidiUSB.flush();
}

void reprise()
{
    if (!vivace)
        return;
    Serial.println("reprise");
    notesSent = false;
    for (int i = 0; i < len; i++)
    {
        noteOff(0, chord[i] + chordShift, chordVol);
    }
    sendControlChange(0x40, 0x0, 0x0);
    MidiUSB.flush();
}

boolean checkTimers()
{
    if (midiReadTimer.While())
    {
        return false;
    }
    return true;
}
// void flashOn()
// {
//     for (int i = 0; i < 10; i++)
//     {
//         digitalWrite(RXLED, HIGH);
//         delay(20);
//         digitalWrite(RXLED, LOW);
//         delay(20);
//     }
// }

// void flashOff()
// {
//     for (int i = 0; i < 3; i++)
//     {
//         digitalWrite(RXLED, HIGH);
//         delay(300);
//         digitalWrite(RXLED, LOW);
//         delay(300);
//     }
//     digitalWrite(RXLED, LOW);
// }
