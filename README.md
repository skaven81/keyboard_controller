Arduino Keyboard Controller
===========================

Arduino Uno based PS/2 keyboard controller for my homebrew CPU.

Pin assignments
---------------

### PS/2 port

* Clock: D2
* Data: A4

### Data bus

* High nibble pins D7-D4 (port D high nibble)
* Low nibble pins D11-D8 (port B low nibble)

Inputs by default, outputs when enable=0 and write=1

### Interrupt: D12

Input by default (with no pullup).  Output with level=0
when interrupt is set.

### Clock: D13

Not used

### Enable: D3

Input.  Pull low to enable CPU communication.

### Write: A3

Input.  High=read, Low=write

### Address: A2-A0

Input. 

* `ADDR_KEY      0x00` - last scanned key (read-only)
* `ADDR_KEYFLAGS 0x01` - flags from last scanned key (read-only)
* `ADDR_BUFLEN   0x02` - keystrokes remaining in the buffer (read-only) [not implemented]
* `ADDR_KBCTRL   0X03` - send commands to the keyboard (write-only)
* `ADDR_CONFIG   0x04` - configuration flags (read-write)

Usage
-----

### Startup

The controller takes quite some time to initialize, and during initialization,
writing to its control registers will not work.  So the controller will signal
the CPU that it has finished initializing by triggering an interrupt.

The CPU is responsible for clearing the interrupt before using the controller,
generally by writing `KBCTRL_INTCLEAR` to `ADDR_KBCTRL`.

### Idle state

When the `ENABLE` pin is set high, the controller goes into an idle state, and
cedes control of the data bus.

While in idle state, the controller is monitoring the keyboard for events.

The most recent keystroke is immediately stored in `ADDR_KEY`, and the most
recent flags for that keystroke in `ADDR_KEYFLAGS`.  New keystrokes overwrite
old ones.

### Read registers

When `ENABLE=0` and `WRITE=1`, the controller is in read mode.
The contents of the register referenced by `ADDR[2:0]` are presented to the
data bus, until `ENABLE=1`.

### Write registers

When `ENABLE=0` and `WRITE=0` is low, the controller will write 
the data on the data bus to the register referenced by `ADDR[2:0]`.
At this stage it behaves like a transparent latch.

The data is committed/latched when `ENABLE=1`.

### CPU Interrupts

The controller can be configured to interrupt the CPU upon various events.
These events are configured in the `CONFIG` register (address `0x04`).

* `0x01 - CONFIG_INTMAKE` - if set, an interrupt will be signaled when a keyboard key is pressed
* `0x02 - CONFIG_INTBREAK` - if set, an interrupt will be signaled when a keyboard key is released
* `0x04 - CONFIG_INTSPECIAL` - if set, an interrupt will be signaled even for special keys

If `CONFIG_INTSPECIAL` is clear, make/break events for special keys do not
generate an interrupt.  Set this bit to enable this behavior.

Once an interrupt is signaled, clearing the interrupt can be done in one
of three ways:

* If `CONFIG_INTCLR_READ (0x08)` is set, then the interrupt is cleared
  as soon as a read operation to the `ADDR_KEY` register (address `0x00`)
  completes.
* The `KBCTRL_INTCLEAR` command (`0x07`) is written to the `KBCTRL` register
  (address `0x03`).
* The `KBCTRL_BUFCLEAR` command (`0x06`) is written to the `KBCTRL` register
  (address `0x03`). (not implemented)

### Reading key events: `KEY` and `KEYFLAGS` registers (`0x00` and `0x01`)

The `KEY` register is considered to have been "read" if `ENABLE=0`,
`WRITE=1`, `ADDR[2:0]=0x00`, and `ENABLE` transitions back to 1.

When a read of `KEY` is completed, `KEY` and `KEYFLAGS` registers are
reset to `0x00`.  Be sure to read `KEYFLAGS` _before_ `KEY`!

When there is no active keystroke, the `KEY` and `KEYFLAGS` registers will
contain `0x00`.

The `KEY` register will contain ASCII representations of the key that was
pressed (or released, if `CONFIG_INTBREAK` is set).

For example, to have the CPU detect that Ctrl+c was pressed, only the
`CONFIG_INTMAKE` need be set.  When the user presses Ctrl, no interrupt
will be generated (as `CONFIG_INTSPECIAL` is not set).  Then when the
user also presses `c`, an interrupt is generated as the key is pressed.

During the interrupt handler, the CPU can read from `KEYFLAGS` and will
observe that `KEYFLAG_CTRL` is set, as well as `KEYFLAG_MAKEBREAK`.
The CPU then reads `KEY` and gets back `c`, confirming that Ctrl+c was
pressed.

The read of `KEY` automatically clears the interrupt (if `CONFIG_INTCLR_READ`
is set).

### Key flags

The `KEYFLAGS` register may have the following bits set:

* `KEYFLAG_MAKEBREAK   0x01` - make=1, break=0
* `KEYFLAG_SHIFT       0x02`
* `KEYFLAG_CTRL        0x04`
* `KEYFLAG_ALT         0x08`
* `KEYFLAG_SUPER       0x10`
* `KEYFLAG_GUI         0x20`
* `KEYFLAG_FUNCTION    0x40` - One of the F-keys was pressed

A set bit indicates that the key is currently pressed.

* Function keys F1-F9 are indicated by an ASCII 1-9 with `KEYFLAG_FUNCTION` set.
* Function key F10 is indicated by an ASCII 0 with `KEYFLAG_FUNCTION` set.
* Function keys F11-12 are indicated by ASCII `!` and `@` with `KEYFLAG_FUNCTION` set.


### Configuration flags

#### `CONFIG_INTMAKE (0x01)`

default: 0 (off)

Generate an interrupt when a key is pressed. Does not apply to Shift, Ctrl, Alt, or Super,
unless `CONFIG_INTSPECIAL` is set.

#### `CONFIG_INTBREAK (0x02)`

default: 0 (off)

Generate an interrupt when a key is released. Does not apply to Shift, Ctrl, Alt, or Super,
unless `CONFIG_INTSPECIAL` is set.

#### `CONFIG_INTSPECIAL (0x04)`

default: 0 (off)

Generate interrupts when Shift, Ctrl, Alt, or Super keys are pressed or released (depending on
the value of `CONFIG_INTMAKE` and `CONFIG_INTBREAK`.

#### `CONFIG_INTCLR_READ (0x08)`

default: 1 (on)

Interrupts are cleared automatically on the falling clock edge if a read operation occurs on
the `KEY` register and the buffer is empty (or disabled).

#### `CONFIG_BUFFER (0x10)`

Not implemented

### Keyboard control commands

The following bytes can be written to `ADDR_KBCTRL` to make the controller execute certain commands.
After the command is completed, the register resets to `0x00`.

* `KBCTRL_NONE         0x00`
* `KBCTRL_NUMLOCK_ON   0x01`
* `KBCTRL_NUMLOCK_OFF  0x02`
* `KBCTRL_CAPSLOCK_ON  0x03`
* `KBCTRL_CAPSLOCK_OFF 0x04`
* `KBCTRL_KB_RESET     0x05`
* `KBCTRL_BUFCLEAR     0x06` - not implemented
* `KBCTRL_INTCLEAR     0x07` - clears interrupt, for use when `CONFIG_INTCLR_READ` is not set

