// vim: syntax=c ts=4 sts=4 sw=4 expandtab

#include "ps2_Keyboard.h"
#include "ps2_SimpleDiagnostics.h"
#include "ps2_ScanCodeSet.h"
#include "ps2_KeyboardOutput.h"
#include "keyboard_controller.h"

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

// PS/2 Port
// Pin 2 must be used for the PS2 clock because it allows interrupts.
#define PS2_CLOCK_PIN 2
#define PS2_DATA_PIN A0

// DEBUG 0: no serial debugging output
// DEBUG 1: Just the captured events
// DEBUG 2: Everything
#define DEBUG 1
#if DEBUG
typedef ps2::SimpleDiagnostics<254> Diagnostics_;
#else
typedef ps2::NullDiagnostics Diagnostics_;
#endif
static Diagnostics_ diagnostics;

//                   data         clock         bufsize, diagnostics-class
static ps2::Keyboard<PS2_DATA_PIN,PS2_CLOCK_PIN,16     , Diagnostics_> ps2Keyboard(diagnostics);

static char keybuf[16];
static char *key;
static char keyflagsbuf[16];
static char *keyflags;
static char buflen;
static char config;


// These need to be globals because even though they are only used in loop(),
// they get reinitialized each time loop() is called
uint8_t current_keyflags = 0x00;
char current_key = '\0';
ps2::KeyboardLeds current_leds;
bool numlock_on;
bool capslock_on;
bool scrolllock_on;

void setup() {
#if DEBUG
    Serial.begin(9600);
#endif
    // Set up the non-keyboard I/O pins. The uC defaults
    // all pins to INPUT by default, so this is just for clarity
    pinMode(DATABUS0_PIN, INPUT);
    pinMode(DATABUS1_PIN, INPUT);
    pinMode(DATABUS2_PIN, INPUT);
    pinMode(DATABUS3_PIN, INPUT);
    pinMode(DATABUS4_PIN, INPUT);
    pinMode(DATABUS5_PIN, INPUT);
    pinMode(DATABUS6_PIN, INPUT);
    pinMode(DATABUS7_PIN, INPUT);
    pinMode(CPU_INT_PIN, INPUT);
    pinMode(CPU_CLK_PIN, INPUT);
    pinMode(ENABLE_PIN, INPUT);
    pinMode(WRITE_PIN, INPUT);
    pinMode(ADDR0_PIN, INPUT);
    pinMode(ADDR1_PIN, INPUT);
    pinMode(ADDR2_PIN, INPUT);
    // All of the pins noted above are connected to concrete
    // nets and will not float, so no pullups are required.
    digitalWrite(DATABUS0_PIN, LOW);
    digitalWrite(DATABUS1_PIN, LOW);
    digitalWrite(DATABUS2_PIN, LOW);
    digitalWrite(DATABUS3_PIN, LOW);
    digitalWrite(DATABUS4_PIN, LOW);
    digitalWrite(DATABUS5_PIN, LOW);
    digitalWrite(DATABUS6_PIN, LOW);
    digitalWrite(DATABUS7_PIN, LOW);
    digitalWrite(CPU_INT_PIN, LOW);
    digitalWrite(CPU_CLK_PIN, LOW);
    digitalWrite(ENABLE_PIN, LOW);
    digitalWrite(WRITE_PIN, LOW);
    digitalWrite(ADDR0_PIN, LOW);
    digitalWrite(ADDR1_PIN, LOW);
    digitalWrite(ADDR2_PIN, LOW);
    
    // Initialize internal registers
    key = keybuf;
    keyflags = keyflagsbuf;
    buflen = 0;
    for(int i=0; i<16; i++) {
        keybuf[i] = '\0';
        keyflagsbuf[i] = '\0';
    }
    config = (CONFIG_INTCLR_READ | CONFIG_BUFFER);

    // Initialize keyboard
    ps2Keyboard.begin();
    //keyMapping.setNumLock(true);
    ps2Keyboard.awaitStartup();

#if DEBUG
    // see the docs for awaitStartup - TL;DR <- when we reset the board but not the keyboard, awaitStartup
    //  records an error because it thinks the keyboard didn't power-up correctly.  When debugging, that's
    //  true - but only because it never powered down.
    diagnostics.reset();
#endif

    // Enable numlock by default
    numlock_on = true;
    capslock_on = false;
    scrolllock_on = false;
    current_leds = ps2::KeyboardLeds::numLock;
    ps2Keyboard.sendLedStatus(current_leds);

    // Switch to PS2 mode (scancode set 3) so we have more control
    // over the keyboard behavior
    ps2Keyboard.setScanCodeSet(ps2::ScanCodeSet::ps2);

    // Enable break codes for all keys
    ps2Keyboard.enableBreakAndTypematic();

    // Disable typematic behavior for modifier keys
    uint8_t disable_typematic_keys[] = {
        (uint8_t)ps2::KeyboardOutput::sc3_leftShift,
        (uint8_t)ps2::KeyboardOutput::sc3_rightShift,
        (uint8_t)ps2::KeyboardOutput::sc3_leftCtrl,
        (uint8_t)ps2::KeyboardOutput::sc3_rightCtrl,
        (uint8_t)ps2::KeyboardOutput::sc3_leftAlt,
        (uint8_t)ps2::KeyboardOutput::sc3_rightAlt,
        (uint8_t)ps2::KeyboardOutput::sc3_leftWindows,
        (uint8_t)ps2::KeyboardOutput::sc3_rightWindows,
        (uint8_t)ps2::KeyboardOutput::sc3_menu,
    };
    ps2Keyboard.disableTypematic(disable_typematic_keys, sizeof(disable_typematic_keys));
    ps2Keyboard.enable();

    // initialize numlock on, capslock off -- note that the keyboard
    // itself doesn't know or care what mode it's in, we have to track
    // this ourselves.
    ps2Keyboard.sendLedStatus(ps2::KeyboardLeds::numLock);

#if DEBUG
    Serial.println("Ready");
#endif
}

void loop() {
    // These don't need to be maintained across calls to loop()
    char print[32];
    ps2::KeyboardOutput scanCode;
    bool is_break = false;

    // Wait for a complete keypress (or release) and update internal state accordingly.
    // We don't count every scancode event as a "keypress". For example, numlock and
    // capslock events don't count as a "keypress".  Modifier keys are not counted as
    // keypresses unless CONFIG_INTSPECIAL is set.  We exit the do {} loop when a complete
    // keypress has been registered (whether make or break).  It's up to the logic outside
    // the loop to decide what to do with the keypress (e.g. whether or not to raise an
    // interrupt, how to manage the key event buffer, etc.
    do {
        // TODO: handle port I/O events

        scanCode = ps2Keyboard.readScanCode();
        if (scanCode == ps2::KeyboardOutput::garbled) {
#if DEBUG
            Serial.println("Garbled, reset");
#endif
            is_break = false;
            continue;
        }
        if(scanCode == ps2::KeyboardOutput::none) continue;

#if DEBUG >= 2
        sprintf(print, "scloop[%02x] flags[", (uint8_t)scanCode);
        Serial.write(print);
        if((current_keyflags & KEYFLAG_SHIFT) > 0)    Serial.write("SHIFT,");
        if((current_keyflags & KEYFLAG_CTRL) > 0)     Serial.write("CTRL,");
        if((current_keyflags & KEYFLAG_ALT) > 0)      Serial.write("ALT,");
        if((current_keyflags & KEYFLAG_SUPER) > 0)    Serial.write("SUPER,");
        if((current_keyflags & KEYFLAG_GUI) > 0)      Serial.write("GUI,");
        if((current_keyflags & KEYFLAG_FUNCTION) > 0) Serial.write("FN,");
        Serial.println("]");
#endif

        if(scanCode == ps2::KeyboardOutput::unmake) {
            // If this is a break code, flag this and loop back
            // to wait for the next code, which will be the key
            // that was released.  The scan code 3 set is way
            // easier as all codes are just 1 (make) or 2 (break)
            // bytes in length.
            is_break = true;
            continue;
        }

        // we have pulled a scancode that isn't the break code,
        // so we are going to process it.  Set the keyflags so
        // we know if this is a make or break event
        if(is_break) {
            current_keyflags &= ~KEYFLAG_MAKEBREAK;
        }
        else {
            current_keyflags |= KEYFLAG_MAKEBREAK;
        }

        // handle caps, num, scroll lock keys
        if(!is_break && scanCode == ps2::KeyboardOutput::sc3_numLock) {
            numlock_on = !numlock_on;
            current_leds = (capslock_on ? ps2::KeyboardLeds::capsLock : ps2::KeyboardLeds::none)
                         | (numlock_on ? ps2::KeyboardLeds::numLock : ps2::KeyboardLeds::none)
                         | (scrolllock_on ? ps2::KeyboardLeds::scrollLock : ps2::KeyboardLeds::none);
            ps2Keyboard.sendLedStatus(current_leds);
            // Set is_break to false so that next loop iteration assumes a make code
            is_break = false;
            continue;
        }
        else if(!is_break && scanCode == ps2::KeyboardOutput::sc3_capsLock) {
            capslock_on = !capslock_on;
            current_leds = (capslock_on ? ps2::KeyboardLeds::capsLock : ps2::KeyboardLeds::none)
                         | (numlock_on ? ps2::KeyboardLeds::numLock : ps2::KeyboardLeds::none)
                         | (scrolllock_on ? ps2::KeyboardLeds::scrollLock : ps2::KeyboardLeds::none);
            ps2Keyboard.sendLedStatus(current_leds);
            // Set is_break to false so that next loop iteration assumes a make code
            is_break = false;
            continue;
        }
        else if(!is_break && scanCode == ps2::KeyboardOutput::sc3_scrollLock) {
            scrolllock_on = !scrolllock_on;
            current_leds = (capslock_on ? ps2::KeyboardLeds::capsLock : ps2::KeyboardLeds::none)
                         | (numlock_on ? ps2::KeyboardLeds::numLock : ps2::KeyboardLeds::none)
                         | (scrolllock_on ? ps2::KeyboardLeds::scrollLock : ps2::KeyboardLeds::none);
            ps2Keyboard.sendLedStatus(current_leds);
            // Set is_break to false so that next loop iteration assumes a make code
            is_break = false;
            continue;
        }
        else if(is_break && (scanCode == ps2::KeyboardOutput::sc3_numLock
                          || scanCode == ps2::KeyboardOutput::sc3_capsLock
                          || scanCode == ps2::KeyboardOutput::sc3_scrollLock)) {
            // Set is_break to false so that next loop iteration assumes a make code
            is_break = false;
            continue;
        }

        // handle modifier keys - this updates `current_keyflags` but we don't
        // actually count it as a "keypress" by exiting the loop, unless
        // CONFIG_INTSPECIAL is set.
        if(scanCode == ps2::KeyboardOutput::sc3_leftShift || scanCode == ps2::KeyboardOutput::sc3_rightShift) {
            if(is_break) current_keyflags &= ~KEYFLAG_SHIFT;
            else         current_keyflags |= KEYFLAG_SHIFT;
#if DEBUG >= 2
            Serial.println("scloop[handle SHIFT]");
#endif
            // Set is_break to false so that next loop iteration assumes a make code
            is_break = false;
            if((config & CONFIG_INTSPECIAL) == 0) continue;
        }
        else if(scanCode == ps2::KeyboardOutput::sc3_leftCtrl || scanCode == ps2::KeyboardOutput::sc3_rightCtrl) {
            if(is_break) current_keyflags &= ~KEYFLAG_CTRL;
            else         current_keyflags |= KEYFLAG_CTRL;
#if DEBUG >= 2
            Serial.println("scloop[handle CTRL]");
#endif
            // Set is_break to false so that next loop iteration assumes a make code
            is_break = false;
            if((config & CONFIG_INTSPECIAL) == 0) continue;
        }
        else if(scanCode == ps2::KeyboardOutput::sc3_leftAlt || scanCode == ps2::KeyboardOutput::sc3_rightAlt) {
            if(is_break) current_keyflags &= ~KEYFLAG_ALT;
            else         current_keyflags |= KEYFLAG_ALT;
#if DEBUG >= 2
            Serial.println("scloop[handle ALT]");
#endif
            // Set is_break to false so that next loop iteration assumes a make code
            is_break = false;
            if((config & CONFIG_INTSPECIAL) == 0) continue;
        }
        else if(scanCode == ps2::KeyboardOutput::sc3_leftWindows || scanCode == ps2::KeyboardOutput::sc3_rightWindows) {
            if(is_break) current_keyflags &= ~KEYFLAG_SUPER;
            else         current_keyflags |= KEYFLAG_SUPER;
#if DEBUG >= 2
            Serial.println("scloop[handle SUPER]");
#endif
            // Set is_break to false so that next loop iteration assumes a make code
            is_break = false;
            if((config & CONFIG_INTSPECIAL) == 0) continue;
        }
        else if(scanCode == ps2::KeyboardOutput::sc3_menu) {
            if(is_break) current_keyflags &= ~KEYFLAG_GUI;
            else         current_keyflags |= KEYFLAG_GUI;
#if DEBUG >= 2
            Serial.println("scloop[handle GUI]");
#endif
            // Set is_break to false so that next loop iteration assumes a make code
            is_break = false;
            if((config & CONFIG_INTSPECIAL) == 0) continue;
        }

		// We handle numlock by trapping scancodes for the keys that are affected by
        // numlock, and remapping their scancodes to the equivalent non-keypad code.
        if(numlock_on) {
            switch((uint8_t)scanCode) {
                case 0x71: scanCode = ps2::KeyboardOutput::sc3_period;  break;
                case 0x70: scanCode = ps2::KeyboardOutput::sc3_0;       break;
                case 0x69: scanCode = ps2::KeyboardOutput::sc3_1;       break;
                case 0x72: scanCode = ps2::KeyboardOutput::sc3_2;       break;
                case 0x7a: scanCode = ps2::KeyboardOutput::sc3_3;       break;
                case 0x6b: scanCode = ps2::KeyboardOutput::sc3_4;       break;
                case 0x73: scanCode = ps2::KeyboardOutput::sc3_5;       break;
                case 0x74: scanCode = ps2::KeyboardOutput::sc3_6;       break;
                case 0x6c: scanCode = ps2::KeyboardOutput::sc3_7;       break;
                case 0x75: scanCode = ps2::KeyboardOutput::sc3_8;       break;
                case 0x7d: scanCode = ps2::KeyboardOutput::sc3_9;       break;
            }
        }
        else {
            switch((uint8_t)scanCode) {
                case 0x71: scanCode = ps2::KeyboardOutput::sc3_delete;      break;
                case 0x70: scanCode = ps2::KeyboardOutput::sc3_insert;      break;
                case 0x69: scanCode = ps2::KeyboardOutput::sc3_end;         break;
                case 0x72: scanCode = ps2::KeyboardOutput::sc3_downArrow;   break;
                case 0x7a: scanCode = ps2::KeyboardOutput::sc3_pageDown;    break;
                case 0x6b: scanCode = ps2::KeyboardOutput::sc3_leftArrow;   break;
                case 0x73: scanCode = ps2::KeyboardOutput::sc3_5;           break;
                case 0x74: scanCode = ps2::KeyboardOutput::sc3_rightArrow;  break;
                case 0x6c: scanCode = ps2::KeyboardOutput::sc3_home;        break;
                case 0x75: scanCode = ps2::KeyboardOutput::sc3_upArrow;     break;
                case 0x7d: scanCode = ps2::KeyboardOutput::sc3_pageUp;      break;
            }
        }

        // if we get to here, we're going to treat this as a normal
        // keypress (or release), so we need to set current_key to
        // the ASCII version of the key that was pressed.
        switch(scanCode) {
            // Function keys are special; we set KEYFLAG_FUNCTION
            // and the translated key is just the number of the F
            // key (0 for 10, and '!' for 11 and '@' for 12)
            case ps2::KeyboardOutput::sc3_f1:
                if(is_break) current_keyflags &= ~KEYFLAG_FUNCTION;
                else         current_keyflags |= KEYFLAG_FUNCTION;
                current_key = '1';
                break;
            case ps2::KeyboardOutput::sc3_f2:
                if(is_break) current_keyflags &= ~KEYFLAG_FUNCTION;
                else         current_keyflags |= KEYFLAG_FUNCTION;
                current_key = '2';
                break;
            case ps2::KeyboardOutput::sc3_f3:
                if(is_break) current_keyflags &= ~KEYFLAG_FUNCTION;
                else         current_keyflags |= KEYFLAG_FUNCTION;
                current_key = '3';
                break;
            case ps2::KeyboardOutput::sc3_f4:
                if(is_break) current_keyflags &= ~KEYFLAG_FUNCTION;
                else         current_keyflags |= KEYFLAG_FUNCTION;
                current_key = '4';
                break;
            case ps2::KeyboardOutput::sc3_f5:
                if(is_break) current_keyflags &= ~KEYFLAG_FUNCTION;
                else         current_keyflags |= KEYFLAG_FUNCTION;
                current_key = '5';
                break;
            case ps2::KeyboardOutput::sc3_f6:
                if(is_break) current_keyflags &= ~KEYFLAG_FUNCTION;
                else         current_keyflags |= KEYFLAG_FUNCTION;
                current_key = '6';
                break;
            case ps2::KeyboardOutput::sc3_f7:
                if(is_break) current_keyflags &= ~KEYFLAG_FUNCTION;
                else         current_keyflags |= KEYFLAG_FUNCTION;
                current_key = '7';
                break;
            case ps2::KeyboardOutput::sc3_f8:
                if(is_break) current_keyflags &= ~KEYFLAG_FUNCTION;
                else         current_keyflags |= KEYFLAG_FUNCTION;
                current_key = '8';
                break;
            case ps2::KeyboardOutput::sc3_f9:
                if(is_break) current_keyflags &= ~KEYFLAG_FUNCTION;
                else         current_keyflags |= KEYFLAG_FUNCTION;
                current_key = '9';
                break;
            case ps2::KeyboardOutput::sc3_f10:
                if(is_break) current_keyflags &= ~KEYFLAG_FUNCTION;
                else         current_keyflags |= KEYFLAG_FUNCTION;
                current_key = '0';
                break;
            case ps2::KeyboardOutput::sc3_f11:
                if(is_break) current_keyflags &= ~KEYFLAG_FUNCTION;
                else         current_keyflags |= KEYFLAG_FUNCTION;
                current_key = '!';
                break;
            case ps2::KeyboardOutput::sc3_f12:
                if(is_break) current_keyflags &= ~KEYFLAG_FUNCTION;
                else         current_keyflags |= KEYFLAG_FUNCTION;
                current_key = '@';
                break;
            // All other keys we use the translator
            default:
                current_key = (char)pgm_read_byte(scancode_to_ascii + (uint8_t)scanCode);
                break;
        }

        // Toggle alpha uppercase based on capslock and shift settings
        //
        // | Shift | Caps | Result
        // |   0   |  0   | lowercase
        // |   0   |  1   | uppercase
        // |   1   |  0   | uppercase
        // |   1   |  1   | lowercase
        if(current_key >= 'a' && current_key < 'z') {
            if(((current_keyflags & KEYFLAG_SHIFT) == 0 && capslock_on) ||
               ((current_keyflags & KEYFLAG_SHIFT) > 0 && !capslock_on)) {
                current_key = current_key - 'a' + 'A';
            }
        }
        // Toggle number key uppercase based on shift setting
        else if((current_keyflags & KEYFLAG_SHIFT) > 0) {
            switch (current_key) {
				case '`': current_key = '~'; break;
				case '1': current_key = '!'; break;
				case '2': current_key = '@'; break;
				case '3': current_key = '#'; break;
				case '4': current_key = '$'; break;
				case '5': current_key = '%'; break;
				case '6': current_key = '^'; break;
				case '7': current_key = '&'; break;
				case '8': current_key = '*'; break;
				case '9': current_key = '('; break;
				case '0': current_key = ')'; break;
				case '-': current_key = '_'; break;
				case '=': current_key = '+'; break;
				case '[': current_key = '{'; break;
				case ']': current_key = '}'; break;
				case ';': current_key = ':'; break;
				case '\'': current_key = '"'; break;
				case ',': current_key = '<'; break;
				case '.': current_key = '>'; break;
				case '/': current_key = '?'; break;
				case '\\': current_key = '|'; break;
            }
        }

        // Set is_break to false so that next loop iteration assumes a make code
        is_break = false;

        // Break out of the loop; we have registered a key press (or release).
        break;
    } while(1);

#if DEBUG
    if((current_keyflags & KEYFLAG_MAKEBREAK) > 0)
        sprintf(print, "Make: raw[%02x] translated[", (uint8_t)scanCode);
    else
        sprintf(print, "Break: raw[%02x] translated[", (uint8_t)scanCode);
    Serial.write(print);
    if     (current_key == (char)0x1b) Serial.write("ESC");
    else if(current_key == (char)0x0c) Serial.write("PrintScr");
    else if(current_key == (char)0x17) Serial.write("Pause");
    else if(current_key == '\b')       Serial.write("Backspace");
    else if(current_key == (char)0x0f) Serial.write("Insert");
    else if(current_key == (char)0x02) Serial.write("Home");
    else if(current_key == (char)0x1c) Serial.write("PgUp");
    else if(current_key == '\t')       Serial.write("Tab");
    else if(current_key == (char)0x7f) Serial.write("Delete");
    else if(current_key == (char)0x03) Serial.write("End");
    else if(current_key == (char)0x1d) Serial.write("PgDown");
    else if(current_key == (char)0x11) Serial.write("Left");
    else if(current_key == (char)0x12) Serial.write("Right");
    else if(current_key == (char)0x13) Serial.write("Up");
    else if(current_key == (char)0x14) Serial.write("Down");
    else if(current_key == '\r')       Serial.write("Enter");
    else if(current_key >= ' ') {
        sprintf(print, "%c", (uint8_t)current_key);
        Serial.write(print);
    }
    else {
        sprintf(print, "%02x", (uint8_t)current_key);
        Serial.write(print);
    }
    Serial.write("] flags=[");
    if((current_keyflags & KEYFLAG_SHIFT) > 0)    Serial.write("SHIFT,");
    if((current_keyflags & KEYFLAG_CTRL) > 0)     Serial.write("CTRL,");
    if((current_keyflags & KEYFLAG_ALT) > 0)      Serial.write("ALT,");
    if((current_keyflags & KEYFLAG_SUPER) > 0)    Serial.write("SUPER,");
    if((current_keyflags & KEYFLAG_GUI) > 0)      Serial.write("GUI,");
    if((current_keyflags & KEYFLAG_FUNCTION) > 0) Serial.write("FN,");
    Serial.println("]");
#endif

    // TODO: push the new key and flags into the buffer

}
