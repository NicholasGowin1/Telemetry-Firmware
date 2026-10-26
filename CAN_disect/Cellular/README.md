# Cellular (Telemetry String Logger)

**Formats decoded CAN lines into a pipe-delimited log and writes them to a file through FatFS.**

This module is the start of the telemetry path. It takes the human-readable lines printed by the C++ monitor, splits them into fields, and appends them to `CAN_Write.can`. Sending that data over a cellular modem is **not implemented yet** (see [Status](#status)).

---

## Data flow

```text
Monitor line (text)
        ↓
StringWriter::separateString()   split into date, ID, message name, name/value pairs
        ↓
StringWriter::writeFile()        serialize to one pipe-delimited line
        ↓
FatFS (f_mount / f_open / f_write / f_close)
        ↓
text_files/CAN_Write.can
```

---

## Directory layout

```text
Cellular/
├── String_Write.h
├── String_Write.cpp
├── fatfs/                    # ChaN FatFs R0.16 (ff.h, ff.c, ffconf.h, diskio.c). Do not edit.
├── text_files/               # sample CAN lines and the generated CAN_Write.can
└── test/
    ├── test_string_write.cpp # host-side test
    └── mock/
        └── ff.h              # fake FatFS, used only by the test build
```

---

## Input format

One line per decoded CAN message. Fields are separated by **two spaces**. A **single space** stays inside a field.

```text
2026-10-23 10:14:26.974  0x22  INV_FR_Cmd_SetACCurrent  ACCurrent=-87.9A
```

| Field         | Example                   | Notes                                                      |
| :------------ | :------------------------ | :--------------------------------------------------------- |
| Date and time | `2026-10-23 10:14:26.974` | One field. The single space between date and time is kept. |
| ID            | `0x22`                    | CAN ID                                                     |
| Message name  | `INV_FR_Cmd_SetACCurrent` | Stored in `location` in the code                           |
| Pairs         | `ACCurrent=-87.9A`        | One or more `name=value`, each separated by two spaces     |

Assumptions the parser makes:

- Every pair contains an `=`.
- No field is separated by three or more spaces.
- The line has no trailing newline or `\r`. Strip them before calling `writeFile` (reading with `getline` already does this on macOS/Linux).

Values may contain a single space, for example `FaultCode=NO FAULTS`.

---

## Output format

Each call to `writeFile` appends one line, with `|` between every field:

```text
2026-10-23 10:14:26.974|0x22|INV_FR_Cmd_SetACCurrent|ACCurrent|-87.9A
2026-10-23 10:14:26.975|0x443|INV_RL_General3_Temps_Fault|FaultCode|NO FAULTS|MotorTemp|97.3degC|ControllerTemp|83.6degC
```

Pattern: `date time|ID|message name|name|value|name|value|...`

A line with `N` pairs contains `2 + 2N` `|` characters.

---

## API

```cpp
StringWriter w;
w.setFileName("CAN_Write.can");   // stored, but see Known limitations
bool ok = w.writeFile(line);      // parse + append to file, returns true on success
```

| Function                      | Purpose                                                                     |
| :---------------------------- | :-------------------------------------------------------------------------- |
| `setFileName` / `getFileName` | Store and read a file name                                                  |
| `separateString`              | Split a raw line into `splitString` (date, ID, location, name[], message[]) |
| `writeFile`                   | Parse, serialize, and append one line through FatFS                         |

`separateString` clears the name/message vectors at the start of each call, so pairs from one line never carry into the next.

---

## Testing on a computer (no SD card)

`writeFile` calls FatFS, which needs an SD card driver. For testing, `test/mock/ff.h` replaces FatFS with plain `fopen`/`fwrite`/`fclose`, so a real `CAN_Write.can` appears on disk.

The mock defines `FF_DEFINED`, which makes the real `fatfs/ff.h` skip itself. It is force-included with `-include`, so no source file changes. `ff.c` and `diskio.c` are **not** compiled into the test.

Run from inside `Cellular/`:

```bash
g++ -g -std=c++17 -include test/mock/ff.h -I. test/test_string_write.cpp String_Write.cpp -o test_sw
./test_sw
wc -l text_files/CAN_Write.can
```

Expected result:

```text
All checks passed.
4 text_files/CAN_Write.can
```

The test:

1. Deletes `text_files/CAN_Write.can` and recreates it with the header `DATE|TIME|ID|LOCATION|MESSAGE NAME|MESSAGE`.
2. Writes three real lines: `0x22` (one pair), `0x443` (a value containing a space), and `0x482` (23 pairs).
3. Reads the file back and checks each line, the `|` count, and that there are no extra lines.

### Troubleshooting

| Symptom                                                     | Cause                                                                                                                                                 |
| :---------------------------------------------------------- | :---------------------------------------------------------------------------------------------------------------------------------------------------- |
| `unknown type name 'StringWriter'` coming from `fatfs/ff.h` | Test or source code was saved into `fatfs/ff.h`. Restore the real FatFs header.                                                                       |
| `identifier FATFS / FIL / f_mount is undefined`             | Not building with `-include test/mock/ff.h`, or not running from `Cellular/`. In VS Code, add the mock as `forcedInclude` in `c_cpp_properties.json`. |
| `Wrong configuration file (ffconf.h)`                       | `ffconf.h` is missing from `fatfs/` or is from a different FatFs revision.                                                                            |
| `writeFile` returns false                                   | The output path does not exist relative to where you ran the program.                                                                                 |
| `!hasExtra` assertion fails                                 | An old `CAN_Write.can` has extra lines. The test must delete the same path `f_open` uses.                                                             |
| Terminal appears stuck inside `lldb`                        | The program already stopped at a failed assert. Type `quit`.                                                                                          |

---

## Building for the real target

The firmware build compiles `fatfs/ff.c` and `diskio.c` with your SD card driver, and `String_Write.cpp` includes `fatfs/ff.h`. Do **not** use the mock in the firmware build.

---

## Known limitations

- **File path is hardcoded** in `writeFile` (`text_files/CAN_Write.can`). `setFileName` stores a name but `f_open` does not use it.
- **No header is written.** `writeFile` only appends data lines. The header must be written once at startup.
- **Header and data do not line up.** The header has `DATE|TIME` as two columns, but each data line has date and time in one field.
- **No 200-line rollover.** The file grows until it is cleared.
- **Mounts and unmounts on every call.** Fine on a computer, slow at CAN rates on hardware. Mount once at startup.
- **`FATFS fs` is declared on the stack** inside `writeFile`. It is large (it contains a sector-sized buffer), which matters on a small microcontroller.
- **Missing includes.** `String_Write.h`/`.cpp` should include `<sstream>`, `<algorithm>`, and `<vector>`/`<string>`.

---

## Status

**In development.**

Done:

- Parse monitor lines into fields
- Write pipe-delimited lines through FatFS
- Host-side test with a mocked FatFS

Planned:

- Hook the monitor output into `StringWriter` for live logging
- Write the header at startup and fix the header/data column mismatch
- Use `setFileName` in `f_open`
- 200-line rollover back to the header
- Send the stream over a cellular modem
- Live GPS location
- Remote server and visualization
