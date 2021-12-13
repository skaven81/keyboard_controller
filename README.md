Arduino Keyboard Controller
===========================

Arduino Uno based PS/2 keyboard controller for my homebrew CPU.

Pin assignments
---------------

TBD

Truth Table
-----------

| ENABLE | CPU_CLK | /WRITE | ADDR[2:0] | DATA[7:0] | Note |
| ------ | ------- | ------ | --------- | --------- | ---- |
|   0    |    X    |    X   |     X     |   float   | All outputs disabled |
|   1    |  rise   |    0   |    0-7    |   input   | Prepare to write DATA to ADDR |
|   1    |    1    |    0   |    0-7    |   input   | Prepare to write DATA to ADDR |
|   1    |  fall   |    0   |    0-7    |   input   | Commit DATA to ADDR |
|   1    |    0    |    0   |    0-7    |   input   | Prepare to write DATA to ADDR |
|   1    |    X    |    1   |    0-7    |   output  | Present ADDR to DATA |


Usage
-----

### Idle state

When the `ENABLE` pin is set low, the controller goes into an idle state, and
cedes control of the data bus.

While in idle state, the controller is monitoring the keyboard for events.

### Read registers

When the `ENABLE` and `/WRITE` pins are high, the controller is in read mode.
The contents of the register referenced by `ADDR[2:0]` are presented to the
data bus.

### Write registers

When the `ENABLE` pin is high and `/WRITE` is low, the controller will begin
preparing to write the data on the data bus to the register referenced by
`ADDR[2:0]`.

When `CPU_CLK` transitions from high to low, the data is committed.

### CPU Interrupts

The controller can be configured to interrupt the CPU upon various events.
These events are configured in the `CONFIG` register (address `0x04`).

* `0x01 - CONFIG_INTMAKE` - if set, an interrupt will be signaled when a
  keyboard key is pressed
* `0x02 - CONFIG_INTBREAK` - if set, an interrupt will be signaled when a
  keyboard key is released
* `0x04 - CONFIG_INTSPECIAL` - if set, an interrupt will be signaled when
  Shift, Ctrl, Alt, or Super are pressed.

Once an interrupt is signaled, clearing the interrupt can be done in one
of three ways:

* If `CONFIG_INTCLR_READ (0x08)` is set, then the interrupt is cleared
  as soon as a read operation to the `KEY` register (address `0x00`)
  completes, resulting in an empty buffer.
* The `KBCTRL_INTCLEAR` command (`0x07`) is written to the `KBCTRL` register
  (address `0x03`).
* The `KBCTRL_BUFCLEAR` command (`0x06`) is written to the `KBCTRL` register
  (address `0x03`).

### Reading key events: `KEY` and `KEYFLAGS` registers (`0x00` and `0x01`)

The `KEY` register is considered to have been "read" if `/WRITE` is high,
`ENABLE` is high, `ADDR[2:0]` is `0x00`, and `CPU_CLK` transitions from
high to low.

When a read of `KEY` is completed, the next key in the buffer (if any)
is shifted to the `KEY` register.

When there is no active keystroke, the `KEY` register will contain `0x00`.

The `KEY` register will contain ASCII representations of the key that was
pressed (or released, if `CONFIG_INTBREAK` is set).

The `KEYFLAGS` register is buffered in parallel with `KEY`, and allows
the CPU to determine what specific event caused the interrupt (make vs
break) and also to determine whether any modifier keys were pressed with
the key.

For example, to have the CPU detect that Ctrl+c was pressed, only the
`CONFIG_INTMAKE` need be set.  When the user presses Ctrl, no interrupt
will be generated (as `CONFIG_INTSPECIAL` is not set).  Then when the
user also presses `c`, an interrupt is generated as the key is pressed.

During the interrupt handler, the CPU can read from `KEYFLAGS` and will
observe that `KEYFLAG_LCTRL` is set, as well as `KEYFLAG_MAKEBREAK`.
The CPU then reads `KEY` and gets back `c`, confirming that Ctrl+c was
pressed.

The read of `KEY` automatically clears the interrupt (if `CONFIG_INTCLR_READ`
is set).

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

default: 1 (on)

Up to 16 keystrokes are queued in the controller for the CPU to read.  Each read operation to
the `KEY` register shifts the buffer.  If the buffer gets full, the oldest keystrokes are
silently discarded to make room for the newer ones.

When interrupts are used to capture keystrokes, the buffer acts as a way to prevent missing
a keystroke that might come in while the interrupt handler is running, by keeping the interrupt
line held low after the read of `KEY`.  The next keystroke is shifted into `KEY` and the CPU
loops back into the interrupt handler, handling the next keystroke.

If the buffer is disabled, `KEY` and `KEYFLAGS` only contain the most recent unread keystroke.
If another keystroke comes in before the current one is read from `KEY`, it replaces the older
keystroke and the older one is lost.

