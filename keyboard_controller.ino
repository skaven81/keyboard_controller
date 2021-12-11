// vim: syntax=c ts=4 sts=4 sw=4 expandtab

#include "ps2_Keyboard.h"
#include "ps2_AnsiTranslator.h"
#include "ps2_SimpleDiagnostics.h"

// unfortunately we can't use all of PORTD (digital pins 0-7) for the data bus,
// because pins 0 and 1 are used for serial, and pins 2 and 3 are used for
// interrupts.  So we put the low nibble at the top of PORTD and the high
// nibble at the bottom of PORTB
#define DATABUS_LOW PORTD & 0xf0
#define DATABUS0_PIN 4
#define DATABUS1_PIN 5
#define DATABUS2_PIN 6
#define DATABUS3_PIN 7
#define DATABUS_HIGH PORTB & 0x0f
#define DATABUS4_PIN 8
#define DATABUS5_PIN 9
#define DATABUS6_PIN 10
#define DATABUS7_PIN 11

// CPU interrupt -- pull low to signal an interrupt to the CPU
#define CPU_INT_PIN 12

// CPU clock -- so we can synchronize writes on clock edges. This
// pin might need interrupts, so we use pin 3.
#define CPU_CLK_PIN 3

// Enable -- high level indicates the CPU is reading or writing from the controller
#define ENABLE_PIN 13

// Write -- low level indicates the CPU wants to write; data is
// committed on the falling edge of CLK_PIN
#define WRITE_PIN A1

// Address -- support for 8 registers:
#define ADDR0_PIN       A3
#define ADDR1_PIN       A4
#define ADDR2_PIN       A5
#define ADDR_KEY        0x0     // last scanned key (read-only)
#define ADDR_KEYFLAGS   0x1     // flags from last scanned key (read-only)
#define ADDR_BUFLEN     0x2     // keystrokes remaining in the buffer (read-only)
#define ADDR_KBCTRL     0X3     // send commands to the keyboard (write-only)
#define ADDR_CONFIG     0x4     // configuration flags (read-write)

// ADDR_KEYFLAGS
#define KEYFLAG_MAKEBREAK   0x01 // MAKE=1, BREAK=0
#define KEYFLAG_SHIFT       0x02 // flags high if key is currently pressed
#define KEYFLAG_LCTRL       0x04
#define KEYFLAG_RCTRL       0x08
#define KEYFLAG_LALT        0x10
#define KEYFLAG_RALT        0x20
#define KEYFLAG_LSUPER      0x40
#define KEYFLAG_RSUPER      0x80

// ADDR_KBCTRL
#define KBCTRL_NUMLOCK_ON   0x01
#define KBCTRL_NUMLOCK_OFF  0x02
#define KBCTRL_CAPSLOCK_ON  0x03
#define KBCTRL_CAPSLOCK_OFF 0x04
#define KBCTRL_KB_RESET     0x05
#define KBCTRL_BUFCLEAR     0x06
#define KBCTRL_INTCLEAR     0x07

// ADDR_CONFIG
#define CONFIG_INTMAKE      0x01 // generate interrupt on key make events
#define CONFIG_INTBREAK     0x02 // generate interrupt on key break events
#define CONFIG_INTSPECIAL   0x04 // generate interrupts for shift/ctrl/alt/super
#define CONFIG_INTCLR_READ  0x08 // if set, CPU_INT_PIN clears upon reading ADDR_KEY if buffer is empty
                                 // if clear, CPU must send KBCTRL_INTCLEAR command
#define CONFIG_BUFFER       0x10 // enable the key buffer
#define CONFIG_REPEAT       0x20 // enable typematic key repeat
#define CONFIG_RAW          0x40 // send raw scancodes from the KB

// PS/2 Port
// Pin 2 must be used for the PS2 clock because it allows interrupts.
#define PS2_CLOCK_PIN 2
#define PS2_DATA_PIN A0

#define DEBUG 1
#if DEBUG
typedef ps2::SimpleDiagnostics<254> Diagnostics_;
#else
typedef ps2::NullDiagnostics Diagnostics_;
#endif
static Diagnostics_ diagnostics;

static ps2::AnsiTranslator<Diagnostics_> keyMapping(diagnostics);
//                   data         clock         bufsize, diagnostics-class
static ps2::Keyboard<PS2_DATA_PIN,PS2_CLOCK_PIN,1      , Diagnostics_> ps2Keyboard(diagnostics);
static ps2::KeyboardLeds lastLedSent = ps2::KeyboardLeds::none;

void setup() {
#if DEBUG
    Serial.begin(9600);
#endif
    ps2Keyboard.begin();
    keyMapping.setNumLock(true);
    ps2Keyboard.awaitStartup();

    // see the docs for awaitStartup - TL;DR <- when we reset the board but not the keyboard, awaitStartup
    //  records an error because it thinks the keyboard didn't power-up correctly.  When debugging, that's
    //  true - but only because it never powered down.
#if DEBUG
    diagnostics.reset();
#endif

    ps2Keyboard.sendLedStatus(ps2::KeyboardLeds::numLock);
    lastLedSent = ps2::KeyboardLeds::numLock;
}

void loop() {
    // Check for any new keypresses and update internal state accordingly
    ps2::KeyboardOutput scanCode = ps2Keyboard.readScanCode();
    if (scanCode == ps2::KeyboardOutput::garbled) {
        keyMapping.reset();
    }
    else if (scanCode != ps2::KeyboardOutput::none)
    {
        char buf[2];
        buf[1] = '\0';
        buf[0] = keyMapping.translatePs2Keycode(scanCode);
        if (buf[0] == '\r') {
            Serial.println();
        }
#if DEBUG
        else if (buf[0] == '\004') { // ctrl+D
            Serial.println();
            diagnostics.sendReport(Serial);
            Serial.println();
            diagnostics.reset();
        }
#endif
        else if (buf[0] >= ' ') { // Characters < ' ' are control-characters; this example isn't clever enough to do anything with them.
            Serial.write(buf);
        }

        ps2::KeyboardLeds newLeds =
              (keyMapping.getCapsLock() ? ps2::KeyboardLeds::capsLock : ps2::KeyboardLeds::none)
            | (keyMapping.getNumLock() ? ps2::KeyboardLeds::numLock : ps2::KeyboardLeds::none);
        if (newLeds != lastLedSent) {
            ps2Keyboard.sendLedStatus(newLeds);
            lastLedSent = newLeds;
        }
    }

    // If a read is in progress, 
}
