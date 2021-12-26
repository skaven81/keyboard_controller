#ifndef ADDR_KEY

// Addresses
#define ADDR_KEY            0x00 // last scanned key (read-only)
#define ADDR_KEYFLAGS       0x01 // flags from last scanned key (read-only)
#define ADDR_BUFLEN         0x02 // keystrokes remaining in the buffer (read-only) [not implemented]
#define ADDR_KBCTRL         0X03 // send commands to the keyboard (write-only)
#define ADDR_CONFIG         0x04 // configuration flags (read-write)
#define config registers[ADDR_CONFIG]

// ADDR_KEYFLAGS
#define KEYFLAG_MAKEBREAK   0x01 // MAKE=1, BREAK=0
#define KEYFLAG_SHIFT       0x02 // flags high if key is currently pressed
#define KEYFLAG_CTRL        0x04
#define KEYFLAG_ALT         0x08
#define KEYFLAG_SUPER       0x10
#define KEYFLAG_GUI         0x20
#define KEYFLAG_FUNCTION    0x40

// ADDR_KBCTRL
#define KBCTRL_NONE         0x00
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
#define CONFIG_INTCLR_READ  0x08 // if set, CPU_INT_PIN clears upon reading ADDR_KEY
                                 // if clear, CPU must send KBCTRL_INTCLEAR command
#define CONFIG_BUFFER       0x10 // enable the key buffer [not implemented]

// unfortunately we can't use all of PORTD (digital pins 0-7) for the data bus,
// because pins 0 and 1 are used for serial, and pins 2 and 3 are used for
// interrupts.  So we put the low nibble at the bottom of PORTB and the high
// nibble at the top of PORTD
#define DATABUS_LOW_PORT PORTB
#define DATABUS_LOW_DIRS DDRB
#define DATABUS_LOW_PINS PINB
#define DATABUS_LOW_MASK 0x0f // pins 8-11
#define DATABUS_HIGH_PORT PORTD
#define DATABUS_HIGH_PINS PIND
#define DATABUS_HIGH_DIRS DDRD
#define DATABUS_HIGH_MASK 0xf0 // pins 4-7

// CPU interrupt -- pull low to signal an interrupt to the CPU
#define CPU_INT_PORT PORTB
#define CPU_INT_PINS PINB
#define CPU_INT_DIRS DDRB
#define CPU_INT_MASK 0x10       // pin 12

// CPU clock -- so we can synchronize writes on clock edges.
#define CPU_CLK_PINS PINB
#define CPU_CLK_MASK 0x20       // pin 13

// Enable -- low level indicates the CPU is reading or writing from the controller
// When this pin transitions from high to low it triggers an interrupt where the CPU
// is provided I/O service.
#define ENABLE_PIN_PORT PORTD
#define ENABLE_PIN_PINS PIND
#define ENABLE_PIN_DIRS DDRD
#define ENABLE_PIN_MASK 0x08    // pin 3

// Address -- support for 8 registers
#define ADDR_PORT       PORTC
#define ADDR_PINS       PINC
#define ADDR_PINS_MASK  0x07    // pins A0-2

// Write -- low level indicates the CPU wants to write; data is
// committed on the falling edge of CLK_PIN
#define WRITE_PORT      PORTC
#define WRITE_PINS      PINC
#define WRITE_PINS_MASK 0x08    // pin A3

// PS/2 Port
// Pin 2 must be used for the PS2 clock because it allows interrupts.
#define PS2_CLOCK_PIN 2
#define PS2_DATA_PIN A4

// DEBUG 0: no serial debugging output
// DEBUG 1: Just the captured events
// DEBUG 2: Everything
#define DEBUG 1

const char scancode_to_ascii[] PROGMEM = {
        '\0', // [00] unused
        '\0', // [01] unused
        '\0', // [02] unused
        '\0', // [03] unused
        '\0', // [04] unused
        '\0', // [05] unused
        '\0', // [06] unused
        '\0', // [07] unused
        (char)0x08, // [08] Escape
        '\0', // [09] unused
        '\0', // [0a] unused
        '\0', // [0b] unused
        '\0', // [0c] unused
        '\t', // [0d] Tab
        '`',  // [0e] ` ~
        '=',  // [0f] Keypad =
        '\0', // [10] F14
        '\0', // [11] Left Alt
        '\0', // [12] Left Shift
        '\0', // [13] unused
        '\0', // [14] Left Control
        'q',  // [15] q Q
        '1',  // [16] 1 !
        '\0', // [17] unused
        '\0', // [18] F15
        '\0', // [19] unused
        'z',  // [1a] z Z
        's',  // [1b] s S
        'a',  // [1c] a A
        'w',  // [1d] w W
        '2',  // [1e] 2 @
        '\0', // [1f] unused
        '\0', // [20] F16
        'c',  // [21] c C
        'x',  // [22] x X
        'd',  // [23] d D
        'e',  // [24] e E
        '4',  // [25] 4 $
        '3',  // [26] 3 #
        '\0', // [27] unused
        '\0', // [28] F17
        ' ',  // [29] Space
        'v',  // [2a] v V
        'f',  // [2b] f F
        't',  // [2c] t T
        'r',  // [2d] r R
        '5',  // [2e] 5 %
        '\0', // [2f] unused
        '\0', // [30] F18
        'n',  // [31] n N
        'b',  // [32] b B
        'h',  // [33] h H
        'g',  // [34] g G
        'y',  // [35] y Y
        '6',  // [36] 6 ^
        '\0', // [37] unused
        '\0', // [38] F19
        '\0', // [39] unused
        'm',  // [3a] m M
        'j',  // [3b] j J
        'u',  // [3c] u U
        '7',  // [3d] 7 &
        '8',  // [3e] 8 *
        '\0', // [3f] unused
        '\0', // [40] F20
        ',',  // [41] , <
        'k',  // [42] k K
        'i',  // [43] i I
        'o',  // [44] o O
        '0',  // [45] 0 )
        '9',  // [46] 9 (
        '\0', // [47] unused
        '\0', // [48] F21
        '.',  // [49] . >
        '/',  // [4a] / ?
        'l',  // [4b] l L
        ';',  // [4c] ; :
        'p',  // [4d] p P
        '-',  // [4e] - _
        '\0', // [4f] unused
        '\0', // [50] F22
        '\0', // [51] unused
        '\'', // [52] ' "
        '\0', // [53] unused
        '[',  // [54] [ {
        '=',  // [55] = +
        '\0', // [56] unused
        (char)0x0c, // [57] PrintScr
        '\0', // [58] Caps Lock
        '\0', // [59] Right Shift
        '\r', // [5a] Return
        ']',  // [5b] ] }
        '\0', // [5c] unused
        '\\', // [5d] \ |
        '\0', // [5e] unused
        '\0', // [5f] F24
        (char)0x14, // [60] Down arrow
        (char)0x11, // [61] Left arrow
        (char)0x17, // [62] Pause/Break
        (char)0x13, // [63] Up arrow
        (char)0x7f, // [64] Delete
        (char)0x03, // [65] End
        '\b', // [66] Backspace
        (char)0x0f, // [67] Insert
        '\0', // [68] unused
        '1',  // [69] Keypad 1 End
        (char)0x12, // [6a] Right arrow
        '4',  // [6b] Keypad 4 Left
        '7',  // [6c] Keypad Home (numlock off)
        (char)0x1d, // [6d] PgDown
        (char)0x02, // [6e] Home
        (char)0x1c, // [6f] PgUp
        '0',  // [70] Keypad 0 Insert
        '.',  // [71] Keypad . Delete
        '2',  // [72] Keypad 2 Down
        '5',  // [73] Keypad 5
        '6',  // [74] Keypad 6 Right
        '8',  // [75] Keypad 8 Up
        (char)0x1b, // [76] Escape
        '/', // [77] Keypad /
        '\0', // [78] F11
        '\r', // [79] Keypad Enter
        '3',  // [7a] Keypad 3 PageDn
        '-',  // [7b] Keypad -
        '+',  // [7c] Keypad +
        '9',  // [7d] Keypad 9 PageUp
        '*',  // [7e] Keypad *
        '\0', // [7f] unused
        '\0', // [80] unused
        '\0', // [81] unused
        '\0', // [82] unused
        '\0', // [83] unused
        '-',  // [84] Keypad -
    };

#endif
