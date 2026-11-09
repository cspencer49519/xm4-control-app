# Sony MDR protocol (v1) reference

Reference for the Bluetooth control protocol used by Sony headphones such as the
WH-1000XM3/XM4, written for interoperability with this client.

**Provenance.** Facts here (opcodes, byte layouts, enum values) were taken from the
Android app *Sony | Sound Connect* (`com.sony.songpal.mdr`) version **13.3.0**
(signature verified as Sony Corporation, cert SHA-256 `42:76:91:96:…:4D:91:1E`),
plus packet captures from a WH-1000XM4 on firmware 3.0.1 (the version the headphones report; macOS showed a stale 2.5.1). No Sony code is
reproduced. Source references like `tm0/i3.java` name the decompiled (obfuscated)
class in that app version so a fact can be re-checked; they will differ in other
versions. Items marked **[I]** are inferred rather than read directly; everything
else is **[R]** (read from the parser/serializer).

Newer models (WH-1000XM5 and later, recent WF earbuds) use protocol **v2**, which
is not covered here.

## Framing (implemented in `CommandSerializer.cpp` / `BluetoothWrapper.cpp`)

RFCOMM, service UUID `96CC203E-5068-46ad-B32D-E316F5E069BA`.

```
0x3E  escape( dataType, seq, len[4, big-endian], payload[len], checksum )  0x3C
```

- checksum = sum of all bytes from `dataType` to the end of the payload, mod 256.
- Escaping: `0x3C`, `0x3D`, `0x3E` are sent as `0x3D` followed by the byte AND `0xEF`.
- `dataType`: `0x01` ACK, `0x0C` DATA_MDR (command table 1), `0x0E` DATA_MDR_NO2
  (command table 2: multipoint/peripheral, voice guidance).
- Every data packet must be ACKed with an ACK whose seq = 1 − (received seq). A
  command's own seq is the inverse of the last ACK seq received.
- Payload byte 0 is the opcode. For most commands byte 1 is an *inquired type*
  that selects the sub-feature and the layout of the rest.

## Conventions

- Opcodes come in families: `GET_x` / `RET_x` (reply to GET) / `SET_x` / `NTFY_x`
  (sent by the device on its own, e.g. after a button press, and after many SETs).
  Typically NTFY = SET + 1 and RET = GET + 1.
- `CommonStatus` (used in many `*_STATUS` replies): **0x00 = enabled/usable,
  0x01 = disabled**, the reverse of the usual on/off enums.
- Strings are UTF-8 with a 1-byte length prefix and no terminator (max 128) unless
  noted.
- Things like EQ band count, ambient step count and preset lists are **reported by
  the device** in capability replies; don't hard-code them.

## WH-1000XM4 (firmware 3.0.1) capture

Supported-function reply: `07 00 16 71 62 f5 81 51 a1 e1 e2 d2 f6 d1 f4 f3 39 12 13 11 30 c1 14 22 21`

| FunctionType | Feature |
|---|---|
| 0x11 | Battery level |
| 0x12 | Upscaling (DSEE) indicator |
| 0x13 | Codec indicator |
| 0x14 | BLE setup |
| 0x21 | Power off |
| 0x22 | Concierge data |
| 0x30 | Firmware update |
| 0x39 | Voice guidance |
| 0x51 | Preset EQ (with Clear Bass) |
| 0x62 | Noise cancelling + ambient sound |
| 0x71 | Auto NC/ASM (Adaptive Sound Control, driven by the phone app) |
| 0x81 | NC Optimizer |
| 0xA1 | Playback controller |
| 0xC1 | Action log notifier |
| 0xD1, 0xD2 | General settings 1 and 2 (device-described generic settings) |
| 0xE1 | Connection mode (sound quality vs. stable connection) |
| 0xE2 | Upscaling (DSEE Extreme) |
| 0xF3 | Control by wearing (wearing detection) |
| 0xF4 | Auto power off |
| 0xF5 | Speak-to-Chat |
| 0xF6 | Assignable settings (custom button) |

Other captured replies: NC/ASM capability `61 02 02 00 01 02 00 14 01 14`
(DUAL_SINGLE_OFF, ASM level adjustment, NORMAL and VOICE both 20 steps); NC/ASM
state `67 02 01 02 02 01 00 00` (on, noise cancelling); optimizer status
`83 01 00 00` (idle); Speak-to-Chat `f7 05 00 01` (on), options
`fb 05 00 00 00 01` (Auto, voice focus off, Standard).
Model name `05 01 0a "WH-1000XM4"`, firmware `05 02 05 "3.0.1"`, battery
`11 00 5a 00` (90 %, not charging), codec `19 00 02` (AAC), upscaling
`15 00 02 01` (DSEE Extreme, active). Unsolicited after connect: codec/upscaling
notifications (`1b`, `17`), playback status `a5 01 00 0x` and volume `a9 01 20 1e` (30).
Playback capability `a1 01 1f 01 01` (31 volume steps, play/pause/track keys, metadata). With nothing playing on the
Mac, every name query returned SETTLED with length 0 (`a7 01 00 02 00`). In a later session (after the multipoint
setting had been toggled) the
supported-function list also contained 0x38 (PAIRING_DEVICE_MANAGEMENT_CLASSIC_BT).


---

## Sony MDR protocol v1, table 1: connection and common commands (0x00-0x37)

Source: Sony "Sound Connect" (com.sony.songpal.mdr) 13.3.0, decompiled. This file records interoperability facts only, written in my own words.
Path prefix for cited classes: `decompiled/sources/`. Enums are in `com/sony/songpal/tandemfamily/message/mdr/v1/table1/param/` unless another path is given.

Conventions:
- Every payload below begins with the opcode at offset 0. Framing, sequence numbers and checksums are handled by the transport and are not shown.
- "App→Dev" means the app sends it; "Dev→App" means the headphones send it. GET is a request, RET is the reply to that GET, NTFY is sent by the device on its own, and SET is a write.
- Unless noted, a `u8` is unsigned (the code uses `b & 0xFF`).
- Strings are **UTF-8 with a 1-byte length prefix and no terminator**. The parser clamps the length to 128 bytes. When the length is 0, the string is "".
- `[inferred]` marks something I deduced instead of reading it directly in the code.

---

### Shared enums

#### CommonCapabilityInquiredType (`CommonCapabilityInquiredType.java`)
| value | name |
|---|---|
| 0x00 | FIXED_VALUE (the only valid value) |
| 0xFF | OUT_OF_RANGE |

The app sends 0x00 in byte 1 of every "FIXED_VALUE" request below.

#### CommonStatus (`com/sony/songpal/tandemfamily/mdr/param/CommonStatus.java`)
| value | name |
|---|---|
| 0x00 | ENABLE |
| 0x01 | DISABLE |
| 0xFF | OUT_OF_RANGE |

**Watch out:** 0x00 means ENABLE here. That is the reverse of the on/off enums used elsewhere, where 0x01 means ON.

---

### 0x00 CONNECT_GET_PROTOCOL_INFO (App→Dev): `tm0/y.java`
| off | meaning |
|---|---|
| 0 | 0x00 |
| 1 | CommonCapabilityInquiredType = 0x00 |

### 0x01 CONNECT_RET_PROTOCOL_INFO (Dev→App): `tm0/m2.java`
| off | meaning |
|---|---|
| 0 | 0x01 |
| 1 | CommonCapabilityInquiredType (0x00) |
| 2-3 | protocol version, u16 **big-endian** |

The parser reads only bytes 2 and 3. If the device sends more bytes, they are ignored.

The initializer in `tv/d.java` accepts exactly these table-1 protocol versions: **0x1000, 0x2000, 0x3000, 0x4000, 0x4010, 0x5000, 0x6000, 0x7000, 0x7010**. For any other value it flags a "protocol version mismatch" and stops initialization.

Other version checks in the code:
- version ≥ 0x5000: the app also requests DEVICE_INFO/INSTRUCTION_GUIDE.
- version == 0x2000: track-name metadata is fetched with a delay (`c40/b.java`).
- version ≤ 0x3000: there is a barometric NC optimizer special case (`k70/w0.java`).

### 0x02 CONNECT_GET_CAPABILITY_INFO (App→Dev): `tm0/h.java`
| off | meaning |
|---|---|
| 0 | 0x02 |
| 1 | 0x00 (FIXED_VALUE) |

### 0x03 CONNECT_RET_CAPABILITY_INFO (Dev→App): `tm0/v1.java`
| off | meaning |
|---|---|
| 0 | 0x03 |
| 1 | 0x00 (FIXED_VALUE) |
| 2 | **capability counter**, u8 (0-255) |
| 3 | N = length of the unique-ID string (clamped to 128) |
| 4..4+N-1 | **unique ID**, UTF-8 |

The meanings come from `tv/a.java` and `k70/a.java`, which label these fields "Capability Counter" and "Unique ID". The app uses the pair (unique ID, counter) as the key for its cached capability set. If the cache matches, it can skip re-querying [inferred from `tv/d.java` flow].

### 0x04 CONNECT_GET_DEVICE_INFO (App→Dev): `tm0/k.java`
| off | meaning |
|---|---|
| 0 | 0x04 |
| 1 | DeviceInfoInquiredType |

DeviceInfoInquiredType (`DeviceInfoInquiredType.java`):
| value | name |
|---|---|
| 0x01 | MODEL_NAME |
| 0x02 | FW_VERSION |
| 0x03 | SERIES_AND_COLOR_INFO |
| 0x04 | INSTRUCTION_GUIDE (protocol ≥ 0x5000 only) |
| 0x00 / 0xFF | NO_USE / OUT_OF_RANGE |

### 0x05 CONNECT_RET_DEVICE_INFO (Dev→App): `tm0/y1.java`
Byte 0 is 0x05 and byte 1 is the DeviceInfoInquiredType. The rest depends on that type.

**0x01 MODEL_NAME** (inner class `y1.e`)
| off | meaning |
|---|---|
| 2 | N (string length, clamped to 128) |
| 3.. | model name, UTF-8 (for example "WH-1000XM4") |

**0x02 FW_VERSION** (inner class `y1.c`)
| off | meaning |
|---|---|
| 2 | N |
| 3.. | firmware version string, UTF-8 |

**0x03 SERIES_AND_COLOR_INFO** (inner class `y1.f`)
| off | meaning |
|---|---|
| 2 | ModelSeries |
| 3 | ModelColor |

ModelSeries (`ModelSeries.java`):
| value | name |
|---|---|
| 0x00 | NO_SERIES |
| 0x10 | EXTRA_BASS |
| 0x20 | HEAR |
| 0x30 | PREMIUM |
| 0x40 | SPORTS |
| 0x50 | CASUAL |

ModelColor (`com/sony/songpal/util/modelinfo/ModelColor.java`):
| value | name |
|---|---|
| 0x00 | DEFAULT |
| 0x01 | BLACK |
| 0x02 | WHITE |
| 0x03 | SILVER |
| 0x04 | RED |
| 0x05 | BLUE |
| 0x06 | PINK |
| 0x07 | YELLOW |
| 0x08 | GREEN |
| 0x09 | GRAY |
| 0x0A | GOLD |
| 0x0B | CREAM |
| 0x0C | ORANGE |
| 0x0D | BROWN |
| 0x0E | VIOLET |
| 0x11-0x1E | the same colors with an "_I" suffix (BLACK_I … VIOLET_I) |

**0x04 INSTRUCTION_GUIDE** (inner class `y1.d`)
| off | meaning |
|---|---|
| 2 | N (count) |
| 3..3+N-1 | one GuidanceCategory byte each. The parser drops 0xFF. |

GuidanceCategory (`GuidanceCategory.java`):
| value | name |
|---|---|
| 0x00 | CHANGE_EARPIECE |
| 0x10 | WEAR_EARPHONE |
| 0x20 | PLAY_BUTTON_OPERATION |
| 0x30 | TOUCH_PAD_OPERATION |
| 0x40 | MAIN_BODY_OPERATION |
| 0x50 | QUICK_ATTENTION |
| 0x60 | ASSIGNABLE_BUTTON_SETTINGS |

### 0x06 CONNECT_GET_SUPPORT_FUNCTION (App→Dev): `tm0/e0.java`
| off | meaning |
|---|---|
| 0 | 0x06 |
| 1 | 0x00 (FIXED_VALUE) |

### 0x07 CONNECT_RET_SUPPORT_FUNCTION (Dev→App): `tm0/s2.java`
| off | meaning |
|---|---|
| 0 | 0x07 |
| 1 | 0x00 (FIXED_VALUE) |
| 2 | N = number of function entries, u8 |
| 3..3+N-1 | N FunctionType bytes, **one byte per function** |

There is no per-entry length and no extra data. Unknown bytes map to NO_USE (0x00) and are kept in the list.

FunctionType (`FunctionType.java`). Source literals 17 and 18 are written as `BSON.TIMESTAMP` and `BSON.NUMBER_LONG`.
| value | name | value | name |
|---|---|---|---|
| 0x00 | NO_USE | 0x63 | AMBIENT_SOUND_MODE |
| 0x11 | BATTERY_LEVEL | 0x71 | AUTO_NC_ASM |
| 0x12 | UPSCALING_INDICATOR | 0x81 | NC_OPTIMIZER |
| 0x13 | CODEC_INDICATOR | 0x92 | VIBRATOR_ALERT_NOTIFICATION |
| 0x14 | BLE_SETUP | 0xA1 | PLAYBACK_CONTROLLER |
| 0x15 | LEFT_RIGHT_BATTERY_LEVEL | 0xB1 | TRAINING_MODE |
| 0x17 | LEFT_RIGHT_CONNECTION_STATUS | 0xC1 | ACTION_LOG_NOTIFIER |
| 0x18 | CRADLE_BATTERY_LEVEL | 0xD1 | GENERAL_SETTING1 |
| 0x21 | POWER_OFF | 0xD2 | GENERAL_SETTING2 |
| 0x22 | CONCIERGE_DATA | 0xD3 | GENERAL_SETTING3 |
| 0x23 | TANDEM_KEEP_ALIVE | 0xE1 | CONNECTION_MODE |
| 0x30 | FW_UPDATE | 0xE2 | UPSCALING |
| 0x38 | PAIRING_DEVICE_MANAGEMENT_CLASSIC_BT | 0xF1 | VIBRATOR |
| 0x39 | VOICE_GUIDANCE | 0xF2 | POWER_SAVING_MODE |
| 0x41 | VPT | 0xF3 | CONTROL_BY_WEARING |
| 0x42 | SOUND_POSITION | 0xF4 | AUTO_POWER_OFF |
| 0x51 | PRESET_EQ | 0xF5 | SMART_TALKING_MODE |
| 0x52 | EBB | 0xF6 | ASSIGNABLE_SETTINGS |
| 0x53 | PRESET_EQ_NONCUSTOMIZABLE | | |
| 0x61 | NOISE_CANCELLING | | |
| 0x62 | NOISE_CANCELLING_AND_AMBIENT_SOUND_MODE | | |

Note: the FunctionType values 0xD1, 0xD2 and 0xD3 are also used directly as the GsInquiredType byte in the GENERAL_SETTING commands (0xD0-0xD9).

#### Initialization order the app uses (`tv/d.java`)
1. PROTOCOL_INFO (0x00). Abort if the version is unsupported.
2. CAPABILITY_INFO (0x02). If the cache hits, the app may reuse stored capabilities [inferred].
3. DEVICE_INFO (0x04) for MODEL_NAME, then FW_VERSION, then SERIES_AND_COLOR_INFO, then INSTRUCTION_GUIDE (only if protocol ≥ 0x5000).
4. SUPPORT_FUNCTION (0x06).
5. Per-feature capability queries, gated on the FunctionType list. For example, BLE_SETUP leads to BT device info requests for BLE_HASH_VALUE and then BLUETOOTH_DEVICE_ADDRESS. GENERAL_SETTINGn leads to a GS capability request.

The app sends each request and waits for the matching RET before sending the next one.

---

### 0x10 COMMON_GET_BATTERY_LEVEL (App→Dev): `tm0/f.java`
| off | meaning |
|---|---|
| 0 | 0x10 |
| 1 | BatteryInquiredType |

BatteryInquiredType: 0x00 BATTERY (single), 0x01 LEFT_RIGHT_BATTERY, 0x02 CRADLE_BATTERY, 0xFF OUT_OF_RANGE.

### 0x11 COMMON_RET_BATTERY_LEVEL (Dev→App): `tm0/t1.java`
### 0x13 COMMON_NTFY_BATTERY_LEVEL (Dev→App): `tm0/s0.java`
Both messages use the same layout. Bytes from offset 2 onward are parsed by `vm0/h.java`, `vm0/x.java` or `vm0/m.java`, depending on the type.

| type (off 1) | off 2 | off 3 | off 4 | off 5 |
|---|---|---|---|---|
| 0x00 BATTERY | level u8 | charging status | n/a | n/a |
| 0x01 LEFT_RIGHT_BATTERY | left level u8 | left charging status | right level u8 | right charging status |
| 0x02 CRADLE_BATTERY | cradle level u8 | charging status | n/a | n/a |

- The level is a percentage, 0-100 [inferred from UI use; the parser does not range-check it].
- BatteryChargingStatus (`BatteryChargingStatus.java`): 0x00 NOT_CHARGING, 0x01 CHARGING, 0xF0 UNKNOWN.
- WH-1000XM4 is single-battery, so it uses type 0x00 [inferred].

### 0x14 COMMON_GET_UPSCALING_EFFECT (App→Dev): `tm0/k0.java`
| off | meaning |
|---|---|
| 0 | 0x14 |
| 1 | 0x00 (FIXED_VALUE) |

### 0x15 COMMON_RET_UPSCALING_EFFECT (Dev→App): `tm0/y2.java`
### 0x17 COMMON_NTFY_UPSCALING_EFFECT (Dev→App): `tm0/l1.java`
| off | meaning |
|---|---|
| 0 | 0x15 / 0x17 |
| 1 | 0x00 (FIXED_VALUE) |
| 2 | UpscalingEffectType: 0x00 DSEE_HX, 0x01 DSEE, 0x02 DSEE_HX_AI |
| 3 | UpscalingEffectStatus: 0x00 OFF, 0x01 VALID (active), 0x02 INVALID (enabled but not taking effect) [meaning of VALID/INVALID inferred from names] |

This is the read-only "indicator" (FunctionType 0x12). The on/off setting for DSEE is the AUDIO_* group with AudioInquiredType 0x02 UPSCALING, which is outside this scope.

### 0x18 COMMON_GET_AUDIO_CODEC (App→Dev): `tm0/c.java`
| off | meaning |
|---|---|
| 0 | 0x18 |
| 1 | 0x00 (FIXED_VALUE) |

### 0x19 COMMON_RET_AUDIO_CODEC (Dev→App): `tm0/q1.java`
### 0x1B COMMON_NTFY_AUDIO_CODEC (Dev→App): `tm0/p0.java`
| off | meaning |
|---|---|
| 0 | 0x19 / 0x1B |
| 1 | 0x00 (FIXED_VALUE) |
| 2 | AudioCodec |

AudioCodec (`AudioCodec.java`):
| value | name |
|---|---|
| 0x00 | UNSETTLED (no A2DP stream yet) |
| 0x01 | SBC |
| 0x02 | AAC |
| 0x10 | LDAC |
| 0x20 | APT_X |
| 0x21 | APT_X_HD |
| 0xFF | OTHER |

### 0x1C COMMON_GET_BLUETOOTH_DEVICE_INFO (App→Dev): `tm0/g.java`
| off | meaning |
|---|---|
| 0 | 0x1C |
| 1 | BluetoothDeviceInfoType: 0x00 BLUETOOTH_DEVICE_ADDRESS, 0x01 BLE_HASH_VALUE |

### 0x1D COMMON_RET_BLUETOOTH_DEVICE_INFO (Dev→App): `tm0/u1.java`
| off | meaning |
|---|---|
| 0 | 0x1D |
| 1 | BluetoothDeviceInfoType |
| 2 | N (clamped to 128) |
| 3.. | UTF-8 string |

- For type 0x00, the string is the BD address as **text**, for example "AA:BB:CC:DD:EE:FF". The app rejects it unless it matches `^XX:XX:XX:XX:XX:XX$` (hex digits).
- For type 0x01, the string is an opaque BLE hash value stored as text.

The parser is `cp0/a.java` `b()`, which reads a length-prefixed string.

### 0x22 COMMON_SET_POWER_OFF (App→Dev): `tm0/m3.java`
| off | meaning |
|---|---|
| 0 | 0x22 |
| 1 | PowerOffInquiredType = 0x00 FIXED_VALUE |
| 2 | PowerOffSettingValue: 0x01 USER_POWER_OFF (0x00 is NO_USE) |

To power the headset off, send `22 00 01`. The code has no RET or NTFY for this command; the device presumably just drops the link [inferred]. It is gated by FunctionType 0x21 POWER_OFF.

### 0x24 COMMON_GET_CONNECTION_STATUS (App→Dev): `tm0/j.java`
| off | meaning |
|---|---|
| 0 | 0x24 |
| 1 | ConnectionStatusInquiredType: 0x01 LEFT_RIGHT_CONNECTION_STATUS (the only valid value; 0xFF is OUT_OF_RANGE) |

### 0x25 COMMON_RET_CONNECTION_STATUS (Dev→App): `tm0/x1.java`
### 0x27 COMMON_NTFY_CONNECTION_STATUS (Dev→App): `tm0/t0.java`
Bytes from offset 2 are parsed by `vm0/y.java`.
| off | meaning |
|---|---|
| 0 | 0x25 / 0x27 |
| 1 | 0x01 LEFT_RIGHT_CONNECTION_STATUS |
| 2 | left unit ConnectionStatus |
| 3 | right unit ConnectionStatus |

ConnectionStatus: 0x00 NOT_CONNECTED, 0x01 CONNECTED.

This status is for true-wireless earbuds and is gated by FunctionType 0x17. It does not apply to WH-1000XM4 [inferred].

### 0x28 COMMON_GET_CONCIERGE_DATA (App→Dev): `tm0/i.java`
| off | meaning |
|---|---|
| 0 | 0x28 |
| 1 | 0x00 (FIXED_VALUE) |
| 2.. | optional raw string bytes, at most 64. The app always sends an empty string, so the request is just `28 00`. |

### 0x29 COMMON_RET_CONCIERGE_DATA (Dev→App): `tm0/w1.java`
| off | meaning |
|---|---|
| 0 | 0x29 |
| 1 | 0x00 (FIXED_VALUE). The app ignores the message if this byte has any other value. |
| 2..end | **unprefixed** string that runs to the end of the payload, at most 1024 bytes when serialized |

The app decodes it with the platform default charset (UTF-8 on Android). Its content is opaque to the app; it is fetched by `k70/v0.java` `a0()` and presumably used for Sony's support ("concierge") web page [inferred]. It is gated by FunctionType 0x22.

### 0x2E COMMON_SET_LINK_CONTROL (App→Dev): `tm0/g3.java`
Bytes from offset 1 are written by `vm0/w.java`.
| off | meaning |
|---|---|
| 0 | 0x2E |
| 1 | LinkControlInquiredType: 0x00 KEEP_ALIVE (the only valid value; 0xFF is OUT_OF_RANGE) |
| 2 | CommonStatus: 0x00 ENABLE, 0x01 DISABLE |
| 3 | timer value, u8 |

How the app uses it (`j30/a.java`, called from `pj/o.java`):
- The code names this the "EcoTimer".
- While a WebView instruction-guide screen is open, the app sends `2E 00 00 05` (ENABLE, 5) every 60 s.
- When the screen closes, it sends `2E 00 01 00` (DISABLE, 0).
- It is only used if FunctionType 0x23 TANDEM_KEEP_ALIVE is present, or the model name is "WF-1000XM3" or "WF-H800".

The unit of the value is not stated. It is probably minutes, and the purpose is probably to keep the device from auto-disconnecting or powering off [inferred].

### 0x2F COMMON_NTFY_LINK_CONTROL (Dev→App): `tm0/w0.java`
Bytes from offset 2 are parsed by `vm0/v.java`.
| off | meaning |
|---|---|
| 0 | 0x2F |
| 1 | 0x00 KEEP_ALIVE |
| 2 | CommonStatus: 0x00 ENABLE, 0x01 DISABLE |

The app (`j30/b.java`) stores this as a boolean "keep-alive enabled" state.

---

### Firmware update group (described only)
Gated by FunctionType 0x30 FW_UPDATE. I have not documented the update procedure.

| opcode | name | dir | layout | class |
|---|---|---|---|---|
| 0x34 | UPDT_SET_STATUS | App→Dev | `[34, UpdateInquiredType, CommonStatus]`. For example, it enters or leaves FW-update mode with type 0x01. | `tm0/s3.java` |
| 0x35 | UPDT_NTFY_STATUS | Dev→App | `[35, UpdateInquiredType, CommonStatus]`. If the status byte is out of range, the app treats it as ENABLE. | `tm0/k1.java` |
| 0x36 | UPDT_GET_PARAM | App→Dev | `[36, UpdateInquiredType]` | `tm0/j0.java` |
| 0x37 | UPDT_RET_PARAM | Dev→App | `[37, UpdateInquiredType, value…]` (see below) | `tm0/x2.java` |

UpdateInquiredType:
| value | name |
|---|---|
| 0x01 | FW_UPDATE_MODE |
| 0x02 | CATEGORY_ID |
| 0x03 | SERVICE_ID |
| 0x04 | NATION_CODE |
| 0x05 | LANGUAGE |
| 0x06 | SERIAL_NUMBER |
| 0x07 | BLE_TX_POWER |
| 0x08 | BATTERY_POWER_THRESHOLD |
| 0x09 | UPDATE_METHOD |
| 0x0A | BATTERY_POWER_THRESHOLD_FOR_INTERRUPTING_FW_UPDATE (spelled "INTERRUPTIONG" in the source) |
| 0x0B | UNIQUE_ID_FOR_DEVICE_BINDING |
| 0x00 / 0xFF | NO_USE / OUT_OF_RANGE |

UPDT_RET_PARAM value layouts, starting at offset 2:
| type | layout | class |
|---|---|---|
| 0x02, 0x03, 0x04, 0x05, 0x06, 0x0B | length-prefixed UTF-8 string, max 128 | `vm0/y0.java` |
| 0x07 BLE_TX_POWER | 1 **signed** byte | `vm0/v0.java` |
| 0x08 and 0x0A | 1 unsigned byte, a battery percentage [inferred] | `vm0/u0.java`, `vm0/t0.java` |
| 0x09 | 1 UpdateMethod byte | `vm0/w0.java` |

UpdateMethod (`com/sony/songpal/tandemfamily/mdr/param/UpdateMethod.java`):
| value | name |
|---|---|
| 0x00 | TANDEM_METHOD |
| 0x10 | CSR_METHOD |
| 0x11 | CSR_RESUMABLE_METHOD |
| 0x12 | CSR_TWS_METHOD |
| 0x13 | CSR_TWS_RESUMABLE_METHOD |
| 0x20 | MTK_METHOD |
| 0x21 | MTK_RESUMABLE_METHOD |
| 0x23 | MTK_TWS_RESUMABLE_METHOD |
| 0x25 | MTK_BACKGROUND_RESUMABLE_METHOD |
| 0x27 | MTK_TWS_BACKGROUND_RESUMABLE_METHOD |

For SERIAL_NUMBER, use GET `36 06` and read the string in the RET. This is a convenient read-only query.

---

### Microphone findings

**Summary: in protocol v1 (which WH-1000XM4 uses), the app has no command that changes microphone quality.** It has no mic gain, uplink noise reduction, beamforming, wind-noise or voice-pickup control. I searched the whole decompiled app, not only tandemfamily, for mic, microphone, sidetone, call, talk, voice pickup, uplink, HFP, wind and noise reduction. The only call-related setting reachable over v1 is **sidetone** (playing your own voice back into your ears during a call). Sidetone changes what you hear, not what the far end hears.

#### 1. Sidetone, "Capture Voice During a Phone Call" (v1, through General Settings)
This is not a dedicated opcode. It is one of the device-defined **General Setting** slots (FunctionType 0xD1, 0xD2 or 0xD3). The device names the slot itself, and the app identifies this slot by the title string `SIDETONE_SETTING`. Relevant files: `com/sony/songpal/mdr/j2objc/cap/GsTitleTitle.java`, `GsTitleTitleResourceMap.java`, and the `ok0/b.java` case that opens the screen.

- UI title: "Capture Voice During a Phone Call".
- UI help text: "Your own voice will be easier to hear during calls. If your voice sounds too loud or background noise is distracting, please turn off this feature."

GS commands (v1 table 1). The GsInquiredType byte equals the FunctionType: 0xD1, 0xD2 or 0xD3.

| opcode | dir | layout | class |
|---|---|---|---|
| 0xD0 GET_CAPABILITY | App→Dev | `[D0, gsType, DisplayLanguage]` | `um0/a.java` |
| 0xD1 RET_CAPABILITY | Dev→App | `[D1, gsType, titleFmt, titleLen, title…, summaryLen, summary…, GsSettingType, (LIST only: count, then items each as fmt,len,str,len,str)]` | `um0/f.java`, `xm0/d.java` |
| 0xD2 GET_STATUS | App→Dev | `[D2, gsType]` | `um0/c.java` |
| 0xD3 RET_STATUS | Dev→App | `[D3, gsType, CommonStatus]` (whether the slot is currently enabled or available) | `um0/h.java` |
| 0xD5 NTFY_STATUS | Dev→App | same as 0xD3 | `um0/e.java` |
| 0xD6 GET_PARAM | App→Dev | `[D6, gsType]` | `um0/b.java` |
| 0xD7 RET_PARAM | Dev→App | `[D7, gsType, GsSettingType, value]` | `um0/g.java` |
| 0xD8 SET_PARAM | App→Dev | `[D8, gsType, GsSettingType, value]` | `um0/i.java` |
| 0xD9 NTFY_PARAM | Dev→App | `[D9, gsType, GsSettingType, value]` | `um0/d.java` |

Field values:
- GsStringFormat (title format byte): 0x01 RAW_NAME (literal text), 0x02 ENUM_NAME (an identifier such as `SIDETONE_SETTING`).
- GsSettingType: 0x01 BOOLEAN_TYPE, 0x02 LIST_TYPE.
- BOOLEAN value: CommonOnOffSettingValue, 0x00 OFF, 0x01 ON (`xm0/a.java`).
- LIST value: a u8 index (`xm0/c.java`).
- DisplayLanguage (MdrLanguage): 0x00 undefined, 0x01 English, 0x02 French, 0x03 German, 0x04 Spanish, 0x05 Italian, 0x06 Portuguese, 0x07 Dutch, 0x08 Swedish, 0x09 Finnish, 0x0A Russian, 0x0B Japanese, 0x0C Simplified Chinese, 0x0D Brazilian Portuguese, 0x0E Traditional Chinese, 0x0F Korean, 0x10 Turkish.
- Strings in the capability reply are length-prefixed UTF-8 (max 128). Title and summary come one after the other.

To toggle sidetone:
1. Find the gsType whose capability title is ENUM_NAME "SIDETONE_SETTING".
2. Send `D8 <gsType> 01 01` to turn it on, or `D8 <gsType> 01 00` to turn it off.

Whether WH-1000XM4 advertises this slot is not known from the app. Check the RET_CAPABILITY replies on the real device.

#### 2. Speak-to-Chat (SMART_TALKING_MODE, FunctionType 0xF5, SYSTEM_* group)
This feature uses the mics to detect your voice and pause music. Its parameters are detection sensitivity, voice focus and mode-out time (`param/smarttalkingmode/*`). It is not call-mic processing, and it belongs to another agent's scope (SYSTEM group).

#### 3. Mic features that exist only in protocol v2 (newer models, not WH-1000XM4)
In `com/sony/songpal/tandemfamily/message/mdr/v2/...`:
- **CALL_SETTINGS** (v2 SystemInquiredType 0x08, v2 FunctionType 0xF8). A status of 6 bytes: CommonStatus, setting-mode status (MODE_OUT/MODE_IN), test-sound status, calling status (IN_CALLING/OUT_OF_CALLING). It also has a "test sound" start/ack (`eo0/k.java`, `v2/table1/system/param/CallSettings*.java`).
- **MIC_ON_OFF_BY_HEADPHONE_OPERATION**: a mic mute toggle on double-tap or a button press during a call, with an optional "mic-off notification tone".
- **MIC_MUTE** assignable function (0x70).
- **PLAYBACK_CONTROLLER_WITH_CALL_VOLUME_ADJUSTMENT(_AND_MUTE)**.
- **AMBIENT_SOUND_CONTROL_MIC** preset.

None of these appear in the v1 enums. Even on v2 devices, none of them changes microphone DSP or quality for the far end; they cover mute, monitoring and call volume.

**Conclusion for macOS:** call mic quality on WH-1000XM4 depends on the Bluetooth HFP codec (CVSD/mSBC) negotiated by macOS, not on any Sony app setting. The only lever the app exposes is sidetone (if advertised), and Speak-to-Chat, which is unrelated to call quality.

---

### Open questions / uncertain
1. **Protocol version width.** The v1 parser reads only 2 bytes (big-endian) at offsets 2-3. Elsewhere the app compares 32-bit versions such as 0x03004001 and 0x03002001, but those are for the v2 table-set path. Check how many bytes WH-1000XM4 actually sends and what value; it should be one of the listed 0x1000-0x7010 values.
2. **Keep-alive (0x2E) timer unit.** The meaning of the byte value (5 in the app) and what exactly "EcoTimer" suspends are not stated in the code.
3. **UpscalingEffectStatus VALID vs INVALID.** The semantics are inferred from the names only.
4. **Battery level range.** 0-100 % is assumed, not range-checked in the code.
5. **Concierge data.** The string's content and format are opaque to the app.
6. **Sidetone on WH-1000XM4.** Whether the device lists a General Setting slot titled SIDETONE_SETTING is unverified.
7. Opcodes 0x08-0x0F, 0x12, 0x16, 0x1A, 0x1E-0x21, 0x23, 0x26, 0x2A-0x2D and 0x30-0x33 are not defined in the v1 table-1 Command enum.

---

## Sony MDR protocol v1 / table1 — NC/ASM, NC Optimizer, Sense, Speak-to-Chat

Source: decompiled Sony "Sound Connect" 13.3.0 (com.sony.songpal.mdr). This file states only protocol facts
(opcodes, byte layouts, value meanings). Offsets are within the message payload (byte 0 = opcode), i.e.
after the frame header/length has been removed.

Conventions: "[R]" = read directly from the serializer/parser. "[I]" = inferred (from UI code, string
resources or naming), not from a serializer. Enum values are 1 byte unless noted. Unsigned ints are 1 byte.

Shared enum: **CommonStatus** (`com/sony/songpal/tandemfamily/mdr/param/CommonStatus`): 0x00 ENABLE, 0x01 DISABLE, 0xFF out of range. Note that 0x00 means *enabled*.

---------------------------------------------------------------------------------------------------

### 0. Enums (all [R])

| Enum | Values |
|---|---|
| NcAsmInquiredType | 0x00 NO_USE, 0x01 NOISE_CANCELLING, 0x02 NOISE_CANCELLING_AND_AMBIENT_SOUND_MODE, 0x03 AMBIENT_SOUND_MODE |
| NcAsmEffect | 0x00 OFF, 0x01 ON, 0x10 ADJUSTMENT_IN_PROGRESS, 0x11 ADJUSTMENT_COMPLETION |
| NcAsmSettingType | 0x00 ON_OFF, 0x01 LEVEL_ADJUSTMENT, 0x02 DUAL_SINGLE_OFF |
| NcDualSingleValue | 0x00 OFF, 0x01 SINGLE, 0x02 DUAL |
| NcOnOffValue | 0x00 OFF, 0x01 ON |
| NcSettingType | 0x00 ON_OFF, 0x01 LEVEL_ADJUSTMENT |
| NcSettingValue | 0x00 OFF, 0x01 ON |
| AsmSettingType | 0x00 ON_OFF, 0x01 LEVEL_ADJUSTMENT |
| AsmId | 0x00 NORMAL, 0x01 VOICE ("Focus on Voice") |
| AsmOnOffValue | 0x00 OFF, 0x01 ON |
| OptimizerInquiredType | 0x00 NO_USE, 0x01 NC_OPTIMIZER, 0x02 NC_MUSIC_OPTIMIZER |
| OptimizerControl | 0x00 CANCEL, 0x01 START |
| OptimizerStatus | 0x00 IDLE, 0x01 IN_PROGRESS_OF_PERSONAL, 0x02 IN_PROGRESS_OF_BAROMETRIC_PRESSURE, 0x10 OPTIMIZING, 0x11 OPTIMIZER_END |
| PersonalMeasureType | 0x00 NOT_SUPPORT, 0x01 PERSONAL |
| PersonalValue | 0x00 UNMEASURED, 0x01 MEASURED |
| BarometricMeasureType | 0x00 NOT_SUPPORT, 0x01 BAROMETRIC_PRESSURE |
| BarometricPressureValue | 0x00 UNMEASURED, 0x07 = 0.7 atm, 0x08 = 0.8, 0x09 = 0.9, 0x0A = 1.0 (the atm values come from the app-side mapping in BarometricPressureMeasurementValue) |
| SenseInquiredType | 0x00 NO_USE, 0x01 AUTO_NC_ASM |
| SenseSettingControl | 0x00 NO_USE, 0x01 START |
| SenseTableType | 0x00 NO_USE, 0x01 TYPE1, 0x02 TYPE2, 0x03 TYPE3 |
| SystemInquiredType | 0x00 NO_USE, 0x01 VIBRATOR, 0x02 POWER_SAVING_MODE, 0x04 AUTO_POWER_OFF, **0x05 SMART_TALKING_MODE**, 0x06 ASSIGNABLE_SETTINGS |
| SmartTalkingModeSettingType | 0x00 ON_OFF (any other value makes the app's parser throw) |
| SmartTalkingModeSettingValue | 0x00 OFF, 0x01 ON |
| SmartTalkingModeParameterType | 0x00 NO_USE, 0x01 MODE_ON_OFF, 0x02 PREVIEW_MODE_ON_OFF |
| SmartTalkingModePreviewType | 0x00 NOT_SUPPORT, 0x01 SUPPORT |
| SmartTalkingModeDetailSettingType | 0x00 TYPE_1 |
| SmartTalkingModeDetectionSensitivityType | 0x00 AUTO_HIGH_LOW |
| SmartTalkingModeVoiceFocusType | 0x00 ON_OFF |
| SmartTalkingModeModeOutTimeType | 0x00 TYPE_1 |
| DetectionSensitivity | 0x00 AUTO, 0x01 HIGH, 0x02 LOW |
| ModeOutTime | 0x00 FAST, 0x01 MID ("Standard"), 0x02 SLOW, 0x03 NONE ("Does not close automatically") |
| CommonOnOffSettingValue | 0x00 OFF, 0x01 ON |
| SmartTalkingModeEffectStatus | 0x00 NOT_ACTIVE, 0x01 ACTIVE |
| CommonCapabilityInquiredType | 0x00 FIXED_VALUE (only valid value) |
| FunctionType (relevant) | 0x38 PAIRING_DEVICE_MANAGEMENT_CLASSIC_BT, 0x41 VPT, 0x61 NOISE_CANCELLING, 0x62 NOISE_CANCELLING_AND_AMBIENT_SOUND_MODE, 0x63 AMBIENT_SOUND_MODE, 0x71 AUTO_NC_ASM, 0x81 NC_OPTIMIZER, 0xF5 SMART_TALKING_MODE |

---------------------------------------------------------------------------------------------------

### 1. NC / Ambient Sound (NCASM, 0x60–0x69)

Opcodes present: 0x60 GET_CAPABILITY, 0x61 RET_CAPABILITY, 0x62 GET_STATUS, 0x63 RET_STATUS, 0x65 NTFY_STATUS,
0x66 GET_PARAM, 0x67 RET_PARAM, 0x68 SET_PARAM, 0x69 NTFY_PARAM. There is no 0x64 (SET_STATUS) in table1.

#### 1.1 NCASM_GET_CAPABILITY 0x60 — tm0/p.java [R]
| Off | Field |
|---|---|
| 0 | 0x60 |
| 1 | NcAsmInquiredType |

#### 1.2 NCASM_RET_CAPABILITY 0x61 — tm0/d2.java [R]
The layout depends on byte 1.

**Inquired 0x01 NOISE_CANCELLING**
| Off | Field |
|---|---|
| 0 | 0x61 |
| 1 | 0x01 |
| 2 | NcSettingType |

**Inquired 0x02 NOISE_CANCELLING_AND_AMBIENT_SOUND_MODE**
| Off | Field |
|---|---|
| 0 | 0x61 |
| 1 | 0x02 |
| 2 | NcAsmSettingType (ON_OFF / LEVEL_ADJUSTMENT / DUAL_SINGLE_OFF) |
| 3 | NC step count (uint8; called "NC step" in the app) |
| 4 | AsmSettingType |
| 5 | n = number of ASM entries |
| 6.. | n × { AsmId (1 byte), step count (uint8) } |

If the AsmSettingType is LEVEL_ADJUSTMENT, an entry with step count 0 is invalid and the app drops it. An entry with an AsmId outside {0,1} is also dropped.

**Inquired 0x03 AMBIENT_SOUND_MODE**
| Off | Field |
|---|---|
| 0 | 0x61 |
| 1 | 0x03 |
| 2 | AsmSettingType |
| 3 | n |
| 4.. | n × { AsmId, step count } |

**Ambient level range.** The maximum ambient level is the step count reported for that AsmId (per AsmId: NORMAL and VOICE may differ). The app never hard-codes it. In the seamless-slider logic (v30/c.java, v30/e.java) the ambient level sent is in **1..N**, where N = step count. Level 0 is only sent together with NC on (DUAL/SINGLE/ON).

#### 1.3 NCASM_GET_STATUS 0x62 — tm0/r.java [R]
`[0x62, NcAsmInquiredType]`

#### 1.4 NCASM_RET_STATUS 0x63 / NCASM_NTFY_STATUS 0x65 — tm0/f2.java, tm0/z0.java [R]
| Off | Field |
|---|---|
| 0 | 0x63 or 0x65 |
| 1 | NcAsmInquiredType |
| 2 | CommonStatus (0x00 = the NC/ASM function is enabled/usable, 0x01 = disabled) |

#### 1.5 NCASM_GET_PARAM 0x66 — tm0/q.java [R]
`[0x66, NcAsmInquiredType]`

#### 1.6 NCASM_SET_PARAM 0x68 / RET_PARAM 0x67 / NTFY_PARAM 0x69 — tm0/i3.java, tm0/e2.java, tm0/y0.java [R]
All three opcodes share the same body, selected by byte 1. The serializers are vm0/c0.java (0x01), vm0/a0.java (0x02) and vm0/c.java (0x03).

**Inquired 0x01 NOISE_CANCELLING** (vm0/c0)
| Off | Field |
|---|---|
| 0 | opcode |
| 1 | 0x01 |
| 2 | NcSettingType |
| 3 | NcSettingValue (0 off / 1 on) |

**Inquired 0x02 NOISE_CANCELLING_AND_AMBIENT_SOUND_MODE** (vm0/a0) — total 8 bytes
| Off | Field | Notes |
|---|---|---|
| 0 | opcode (0x67/0x68/0x69) | |
| 1 | 0x02 | |
| 2 | NcAsmEffect | 0x00 OFF = whole NC/ASM off; 0x01 ON; 0x10 = slider is being dragged (UNDER_CHANGE); 0x11 = change finished (CHANGED) |
| 3 | NcAsmSettingType | the app always sends the value from the capability reply (§1.2). It rejects a RET/NTFY whose byte 3 differs from the capability ("Illegal param ... Ignore it") |
| 4 | NC value (uint8) | its meaning depends on byte 3: DUAL_SINGLE_OFF → NcDualSingleValue; ON_OFF → NcOnOffValue |
| 5 | AsmSettingType | the app sends the capability value and rejects a mismatch |
| 6 | AsmId | 0 NORMAL, 1 VOICE |
| 7 | ASM value (uint8) | LEVEL_ADJUSTMENT → ambient level (1..N, 0 while NC is on); ON_OFF → AsmOnOffValue |

How the app builds byte 4 / byte 7 [R, v30/b,c,d,e.java]:

| UI state | NcAsmSettingType=DUAL_SINGLE_OFF, Asm=LEVEL_ADJUSTMENT (v30/c) | DUAL_SINGLE_OFF, Asm=ON_OFF (v30/b) | NC ON_OFF, Asm=LEVEL (v30/e) | NC ON_OFF, Asm ON_OFF (v30/d) |
|---|---|---|---|---|
| Noise cancelling | b4=0x02 DUAL, b7=0 | b4=DUAL, b7=0 (ASM OFF) | b4=0x01 ON, b7=0 | b4=ON, b6=NORMAL, b7=0 |
| Wind noise reduction | b4=0x01 SINGLE, b7=0 | b4=SINGLE, b7=0 | n/a | n/a |
| Ambient sound level L | b4=0x00 OFF, b7=L (1..N) | b4=OFF, b7=1 (ASM ON) | b4=0x00 OFF, b7=L (1..N) | b4=OFF, b7=1 |

In the seamless slider (v30/c), slider position 0 = DUAL, 1 = SINGLE, and k ≥ 2 = ambient level k−1 (max position N+1).

**NcDualSingleValue SINGLE = "Wind Noise Reduction"**: in the UI view
`ModeNcAsmNcDualSingleModeSwitchSeamlessThirdLayerDetailView`, ON_SINGLE is labelled with string
`ASM_Param_Street` = "Wind Noise Reduction", and ON_DUAL with "Noise Canceling". DUAL is full NC. SINGLE is
the wind-reduced NC mode (single-mic NC [I]).

**Inquired 0x03 AMBIENT_SOUND_MODE** (vm0/c)
| Off | Field |
|---|---|
| 0 | opcode |
| 1 | 0x03 |
| 2 | NcAsmEffect |
| 3 | AsmSettingType |
| 4 | AsmId |
| 5 | ASM value (level or AsmOnOffValue) |

---------------------------------------------------------------------------------------------------

### 2. NC Optimizer (OPT, 0x80–0x89)

Opcodes present: 0x80 GET_CAPABILITY, 0x81 RET_CAPABILITY, 0x82 GET_STATUS, 0x83 RET_STATUS, 0x84 SET_STATUS,
0x85 NTFY_STATUS, 0x86 GET_PARAM, 0x87 RET_PARAM, 0x89 NTFY_PARAM (there is no 0x88 SET_PARAM).

#### 2.1 OPT_GET_CAPABILITY 0x80 / GET_STATUS 0x82 / GET_PARAM 0x86 — tm0/s.java, u.java, t.java [R]
`[opcode, OptimizerInquiredType]`. The app uses 0x01 NC_OPTIMIZER.

#### 2.2 OPT_RET_CAPABILITY 0x81 — tm0/g2.java [R]
| Off | Field |
|---|---|
| 0 | 0x81 |
| 1 | OptimizerInquiredType |
| 2 | total optimization time, seconds (uint8) |
| 3 | PersonalMeasureType |
| 4 | personal measurement time, seconds (uint8) |
| 5 | BarometricMeasureType |
| 6 | barometric measurement time, seconds (uint8) |
(The "seconds" unit comes from the capability toString in devicecapability/tableset1/j.java.)

#### 2.3 OPT_SET_STATUS 0x84 — tm0/j3.java [R]
| Off | Field |
|---|---|
| 0 | 0x84 |
| 1 | OptimizerInquiredType (0x01) |
| 2 | CommonStatus — the app always writes 0x00 ENABLE |
| 3 | OptimizerControl: 0x01 START, 0x00 CANCEL |
Start = `84 01 00 01`, cancel = `84 01 00 00` (y30/b.java).

#### 2.4 OPT_RET_STATUS 0x83 / OPT_NTFY_STATUS 0x85 — tm0/i2.java, tm0/b1.java [R]
| Off | Field |
|---|---|
| 0 | 0x83 / 0x85 |
| 1 | OptimizerInquiredType |
| 2 | CommonStatus (0x00 enabled, 0x01 disabled) |
| 3 | OptimizerStatus (0x00 IDLE, 0x01 measuring personal, 0x02 measuring pressure, 0x10 OPTIMIZING, 0x11 OPTIMIZER_END) |

#### 2.5 OPT_RET_PARAM 0x87 / OPT_NTFY_PARAM 0x89 — tm0/h2.java, tm0/a1.java [R]
| Off | Field |
|---|---|
| 0 | 0x87 / 0x89 |
| 1 | OptimizerInquiredType |
| 2 | PersonalMeasureType |
| 3 | PersonalValue (0 unmeasured / 1 measured) |
| 4 | BarometricMeasureType |
| 5 | BarometricPressureValue (0x00 unmeasured, 0x07..0x0A = 0.7..1.0 atm) |

---------------------------------------------------------------------------------------------------

### 3. SENSE (0x70–0x74) — Adaptive Sound Control trigger

Opcodes present in table1: only 0x70 SENSE_GET_CAPABILITY, 0x71 SENSE_RET_CAPABILITY and 0x74 SENSE_SET_STATUS.

#### 3.1 SENSE_GET_CAPABILITY 0x70 — tm0/z.java [R]
`[0x70, SenseInquiredType]` (0x01 AUTO_NC_ASM)

#### 3.2 SENSE_RET_CAPABILITY 0x71 — tm0/n2.java [R]
| Off | Field |
|---|---|
| 0 | 0x71 |
| 1 | SenseInquiredType |
| 2 | SenseTableType (TYPE1..3; the meaning of the types was not determined) |

#### 3.3 SENSE_SET_STATUS 0x74 — tm0/n3.java [R]
| Off | Field |
|---|---|
| 0 | 0x74 |
| 1 | SenseInquiredType (0x01 AUTO_NC_ASM) |
| 2 | CommonStatus — the app always writes 0x00 |
| 3 | SenseSettingControl (0x01 START) |
The only value the app sends is `74 01 00 01` (a50/a.java). The Adaptive Sound Control controller
(j2objc/application/adaptivesoundcontrol/h.java) sends it when it applies an activity/place-triggered
setting change, right before it pushes the new NC/ASM (and EQ) settings. [I] This most likely makes the
headset play its "settings changed" notification cue.

---------------------------------------------------------------------------------------------------

### 4. Speak-to-Chat (SMART_TALKING_MODE, SystemInquiredType 0x05)

It uses the SYSTEM_* commands with byte 1 = 0x05. On/off and preview use SYSTEM_*_PARAM. Detail settings
use SYSTEM_*_EXTENDED_PARAM. Live state uses SYSTEM_*_STATUS. Feature controllers: e50/a.java (state) and e50/b.java
(commands). Parameter classes: ym0/a–e.java.

#### 4.1 SYSTEM_GET_CAPABILITY 0xF0 — tm0/f0.java [R]
`[0xF0, 0x05]`

#### 4.2 SYSTEM_RET_CAPABILITY 0xF1 (inquired 0x05) — tm0/t2.java (inner class e) [R]
| Off | Field |
|---|---|
| 0 | 0xF1 |
| 1 | 0x05 |
| 2 | SmartTalkingModeSettingType (0x00 ON_OFF) |
| 3 | SmartTalkingModePreviewType (0 not supported / 1 supported) |
| 4 | SmartTalkingModeDetailSettingType (0x00 TYPE_1); if this is not TYPE_1, the message ends here |
| 5 | SmartTalkingModeDetectionSensitivityType (0x00 AUTO_HIGH_LOW) |
| 6 | SmartTalkingModeVoiceFocusType (0x00 ON_OFF) |
| 7 | SmartTalkingModeModeOutTimeType (0x00 TYPE_1); if this is not TYPE_1, the message ends here |
| 8..11 | four uint8 mode-out durations, indexed by ModeOutTime FAST, MID, SLOW, NONE |
Bytes 8..11 are what the UI shows as "Approx. N s / N m" next to Fast/Standard/Slow ([I]: the unit is seconds, from the
string resources `SmartTalkingMode_Setting_ModeOutTime_second/minute`; qy/h.java formats the value).

#### 4.3 SYSTEM_GET_STATUS 0xF2 — tm0/i0.java [R]
`[0xF2, 0x05]`

#### 4.4 SYSTEM_RET_STATUS 0xF3 / SYSTEM_NTFY_STATUS 0xF5 — tm0/w2.java, tm0/j1.java + ym0/e.java [R]
| Off | Field |
|---|---|
| 0 | 0xF3 / 0xF5 |
| 1 | 0x05 |
| 2 | CommonStatus (0x00 = the feature is enabled/usable, 0x01 = disabled) |
| 3 | SmartTalkingModeEffectStatus (0x01 ACTIVE = currently in speak-to-chat/talking mode, 0x00 not active) |

#### 4.5 SYSTEM_GET_PARAM 0xF6 — tm0/h0.java [R]
`[0xF6, 0x05]`

#### 4.6 SYSTEM_SET_PARAM 0xF8 — tm0/r3.java + ym0/d.java [R]
| Off | Field |
|---|---|
| 0 | 0xF8 |
| 1 | 0x05 |
| 2 | SmartTalkingModeParameterType: 0x01 MODE_ON_OFF, 0x02 PREVIEW_MODE_ON_OFF |
| 3 | SmartTalkingModeSettingValue: **0x01 ON, 0x00 OFF** |
Enable = `F8 05 01 01`, disable = `F8 05 01 00`. Preview ("try it") on/off = `F8 05 02 01` / `F8 05 02 00`.

#### 4.7 SYSTEM_NTFY_PARAM 0xF9 — tm0/i1.java + ym0/d.java [R]
Same layout as SET: `[0xF9, 0x05, ParameterType(01 mode / 02 preview), value(0/1)]`. The app updates the
main toggle when byte 2 is 0x01 and the preview flag when byte 2 is 0x02.

#### 4.8 SYSTEM_RET_PARAM 0xF7 — tm0/v2.java + ym0/c.java [R]
| Off | Field |
|---|---|
| 0 | 0xF7 |
| 1 | 0x05 |
| 2 | **SmartTalkingModeSettingType = 0x00 (ON_OFF)**, not a ParameterType |
| 3 | SmartTalkingModeSettingValue (0x01 ON, 0x00 OFF) |
The RET (0xF7) and the NTFY (0xF9) of the same command use different byte-2 meanings. Byte 3 is the state in both.

#### 4.9 SYSTEM_GET_EXTENDED_PARAM 0xFA — tm0/g0.java [R]
`[0xFA, 0x05]`

#### 4.10 SYSTEM_SET_EXTENDED_PARAM 0xFC / RET 0xFB / NTFY 0xFD — tm0/q3.java, u2.java, h1.java + ym0/a.java, ym0/b.java [R]
| Off | Field |
|---|---|
| 0 | 0xFC / 0xFB / 0xFD |
| 1 | 0x05 |
| 2 | SmartTalkingModeDetailSettingType (0x00 TYPE_1); bytes 3–5 are present only for TYPE_1 |
| 3 | DetectionSensitivity: 0x00 AUTO, 0x01 HIGH, 0x02 LOW |
| 4 | Voice focus, CommonOnOffSettingValue: 0x00 OFF, 0x01 ON (the app's parser throws on other values) |
| 5 | ModeOutTime: 0x00 FAST, 0x01 MID/Standard, 0x02 SLOW, 0x03 NONE (never auto-close) |
(jadx shows tm0/u2.java's constructor using the SPORTS_RET_EXTENDED_PARAM constant (0xBB). This looks like a decompiler artefact: Command.java maps 0xFB SYSTEM_RET_EXTENDED_PARAM → u2, and the parse layout is as above.)

---------------------------------------------------------------------------------------------------

### 5. AUTO_NC_ASM (FunctionType 0x71) = Adaptive Sound Control

- FunctionType 0x71 in the supported-function list makes the app show the ADAPTIVE_SOUND_CONTROL settings
  (xz/b.java maps it to SettingItem.ADAPTIVE_SOUND_CONTROL and ADAPTIVE_SOUND_CONTROL_MANUAL_PLACE_SWITCH). [R]
- table1 has no dedicated parameter command for it. The logic runs in the phone app (activity/place detection), which drives the headset with:
  - SENSE_SET_STATUS `74 01 00 01` (§3.3), then
  - the normal NCASM_SET_PARAM (0x68) with the per-activity NC/ASM values, plus EQ/S2C etc. as configured. [R for the order in adaptivesoundcontrol/h.java: the sense call runs in the deactivate/apply path before the NC/ASM and EQ updates]
- Capability for it: SENSE_GET_CAPABILITY `70 01` → `71 01 <SenseTableType>`.
- A macOS client can implement ASC itself (detect the activity, then send 0x68). It is optional to send `74 01 00 01` first.

---------------------------------------------------------------------------------------------------

### 6. Supported-function query (for the verification below) — tm0/e0.java, tm0/s2.java [R]
- CONNECT_GET_SUPPORT_FUNCTION: `[0x06, CommonCapabilityInquiredType]`. The **only** valid value is 0x00 FIXED_VALUE.
- CONNECT_RET_SUPPORT_FUNCTION: `[0x07, 0x00, n, FunctionType × n]`.

---------------------------------------------------------------------------------------------------

### Verification of the earlier client (all WRONG/PARTLY items below have since been fixed)

| # | Assumption | Verdict | Details |
|---|---|---|---|
| 1 | NCASM_SET_PARAM = `[68, 02, effect, ncSettingType, dualSingle, asmSettingType, asmId, asmLevel]` | **PARTLY** | The byte order is correct. But the client hard-codes byte 3 = 0x01 LEVEL_ADJUSTMENT. The app always sends the NcAsmSettingType from the 0x61 capability reply and treats a mismatch as illegal. For the dual/single model that is 0x02 DUAL_SINGLE_OFF; LEVEL_ADJUSTMENT is never used by the table1 NC/ASM senders (v30/*). Fix: query `60 02` and echo byte 2 of the reply (most likely 0x02 for XM4 [I]). |
| 2 | dual = NC, single = "wind-noise mode" | **CONFIRMED** | SINGLE (0x01) is labelled "Wind Noise Reduction"; DUAL (0x02) is "Noise Canceling"; OFF (0x00) = NC off (ambient). |
| 3 | Level encoding: client level 0 → DUAL, 1 → SINGLE, ≥2 → OFF, and byte 7 = the raw client level | **WRONG (encoding)** | The app sends byte 7 = **0** for both DUAL and SINGLE (the client sends 1 with SINGLE). For ambient it sends byte 7 = slider−1, i.e. ambient level **1..N**. The client sends the raw slider index (2..19), so it is off by one and can never send levels 1 or N. |
| 4 | Ambient level 0..19, max steps 19 | **WRONG / PARTLY** | The max is not fixed: it is the per-AsmId step count in `61 02` bytes 6.. (entry = AsmId, steps). Valid ambient levels are 1..steps; 0 only appears with NC on. (XM3/XM4 are widely reported to give 20 steps [I, not from the app]). |
| 5 | NCASM_GET_PARAM = `[66, 02]`; RET 0x67 / NTFY 0x69 have the same layout as SET | **CONFIRMED** | The RET/NTFY parser (vm0/a0) is identical to SET. Also handle byte 2 = 0x10 (in progress) and 0x11 (completed) as "on". The client already treats any value other than 0x00 as on. |
| 6 | S2C toggle SET = `[F8, 05, 01, state]`, 1 = on | **CONFIRMED** | Byte 2 0x01 = ParameterType MODE_ON_OFF; byte 3 0x01 = ON, 0x00 = OFF. (0x02 in byte 2 = preview mode.) |
| 7 | S2C GET = `[F6, 05]` | **CONFIRMED** | |
| 8 | S2C RET 0xF7 / NTFY 0xF9 = `[cmd, 05, 01, state]` | **PARTLY** | NTFY 0xF9 matches. RET 0xF7 per the app is `[F7, 05, **00**, state]` (byte 2 = SmartTalkingModeSettingType ON_OFF = 0x00). The client only reads byte 3, so it works. Do not validate byte 2 == 0x01 for RET, and on NTFY ignore byte 2 == 0x02 (a preview-mode change, not the main toggle). The client currently treats a preview NTFY as a toggle change. |
| 9 | S2C options SET = `[FC, 05, 00, sensitivity, voice, offTime]`, GET `[FA, 05]`, RET 0xFB | **CONFIRMED** | Byte 2 = DetailSettingType TYPE_1 (0x00). Sensitivity 0 Auto / 1 High / 2 Low. Voice focus 0 off / 1 on. Off time 0 Fast / 1 Standard / 2 Slow / 3 Never. NTFY 0xFD has the same layout. Reading the last 3 bytes is fine. |
| 10 | Optimizer SET = `[84, 01, 00, state]` | **CONFIRMED** | Byte 2 = CommonStatus ENABLE (0x00); byte 3 = OptimizerControl 0x01 START / 0x00 CANCEL. |
| 11 | Optimizer status reply 0x85: byte[2] != 0 means optimizing | **WRONG** | Byte 2 is CommonStatus (0x00 = enabled, so this check reads "idle" for a normal device and "optimizing" only when the feature is *disabled*). The state is in **byte 3**: OptimizerStatus 0x00 IDLE, 0x01/0x02 measuring, 0x10 OPTIMIZING, 0x11 END. Treat 0x01, 0x02 and 0x10 as busy, and 0x00 and 0x11 as done. Also handle RET 0x83 (same layout; request with `82 01`). |
| 12 | Capability query `[06, 01]` | **WRONG (value)** | The inquired type must be 0x00 FIXED_VALUE; 0x01 is out of range for the app's enum. Send `[06, 00]`. (Whether the device tolerates 0x01 is unknown.) |
| 13 | Reply 0x07 parsed by scanning all bytes for FunctionType values | **PARTLY** | The reply is `[07, 00, n, ft1..ftn]`. Scanning all bytes can give false positives: byte 0 = 0x07, byte 2 = count, e.g. 0x38/0x41/0x62 if the device has that many functions. Parse n entries from offset 3. The FunctionType constants in Constants.h (0x38, 0x41, 0x62, 0x81, 0xF5) are correct. |

---------------------------------------------------------------------------------------------------

### Open questions / uncertain

1. Which NcAsmSettingType and step count the WH-1000XM4 actually reports in `61 02` (likely DUAL_SINGLE_OFF with ASM LEVEL_ADJUSTMENT, NORMAL and VOICE entries each with 20 steps). This is not determinable from the app; query the device. It is also unknown whether the XM4 tolerates byte 3 = LEVEL_ADJUSTMENT (the client's current value). Community clients reportedly work with it, so the firmware may ignore that byte.
2. The meaning of the "NC step" byte (`61 02` byte 3) when NcAsmSettingType = LEVEL_ADJUSTMENT. No table1 sender uses NC LEVEL_ADJUSTMENT, so the NC-level semantics of byte 4 are unverified.
3. RET 0xF7 byte 2: the app's parser requires 0x00 and throws on anything else. If a real XM4 sends 0x01 there, the app would fail to parse it, so the device most likely sends 0x00 [I].
4. The S2C capability mode-out duration bytes (offsets 8–11): seconds is inferred from the UI strings. The NONE slot's value is unused by the UI.
5. SENSE_SET_STATUS effect on the headset (notification cue?) and the meaning of SenseTableType 1–3: inferred / unknown.
6. What NcAsmEffect 0x01 ON means in a device-originated NTFY, compared with 0x11. The app maps 0x01 to NcAsmSendStatus.ON and 0x11 to CHANGED; both mean the function is active.
7. Whether the device accepts a SET with NcAsmEffect 0x00 OFF and arbitrary trailing bytes (the client sends level ASM_LEVEL_DISABLED). The app's OFF path was not traced in detail. [I] It likely re-sends the current params with effect 0x00.

---

## Sony MDR protocol v1 / table1 — EQ/EBB, AUDIO, VPT

Source: com.sony.songpal.mdr 13.3.0 (decompiled). Facts only. Byte 0 is always the opcode.
Message-class files are relative to `sources/`. "Read" = taken directly from serializer/parser code;
"Inferred" = deduced from app-layer usage or naming.

Common enums used below:

| CommonStatus (`com/sony/songpal/tandemfamily/mdr/param/CommonStatus`) | |
|---|---|
| 0x00 | ENABLE |
| 0x01 | DISABLE |
| 0xFF | OUT_OF_RANGE |

DisplayLanguage (byte sent in GET_CAPABILITY requests; same codes as MdrLanguage): 0x00 UNDEFINED, 0x01 ENGLISH,
0x02 FRENCH, 0x03 GERMAN, 0x04 SPANISH, 0x05 ITALIAN, 0x06 PORTUGUESE, 0x07 DUTCH, 0x08 SWEDISH, 0x09 FINNISH,
0x0A RUSSIAN, 0x0B JAPANESE, 0x0C SIMPLIFIED_CHINESE, 0x0D BRAZILIAN_PORTUGUESE, 0x0E TRADITIONAL_CHINESE,
0x0F KOREAN, 0x10 TURKISH (0xF0 CHINESE exists in MdrLanguage but not in DisplayLanguage). It selects the language of
the preset-name strings the device returns.

---------------------------------------------------------------------------------------------------

### 1. EQ / EBB (0x50–0x5B)

#### Enums

EqEbbInquiredType: 0x00 NO_USE, **0x01 PRESET_EQ** (customizable EQ — what WH-1000XM4 uses), 0x02 EBB (standalone
"Electronic Bass Boost" level), 0x03 PRESET_EQ_NONCUSTOMIZABLE (presets only, no band editing), 0xFF OUT_OF_RANGE.

EqPresetId (table1):

| hex | name | UI string (en) |
|---|---|---|
| 0x00 | OFF | Off |
| 0x01 | ROCK | Rock |
| 0x02 | POP | Pop |
| 0x03 | JAZZ | Jazz |
| 0x04 | DANCE | Dance |
| 0x05 | EDM | EDM |
| 0x06 | R_AND_B_HIP_HOP | R&B / Hip Hop |
| 0x07 | ACOUSTIC | Acoustic |
| 0x08–0x0F | RESERVED_FOR_FUTURE_NO8..15 | (blank) |
| 0x10 | BRIGHT | Bright |
| 0x11 | EXCITED | Excited |
| 0x12 | MELLOW | Mellow |
| 0x13 | RELAXED | Relaxed |
| 0x14 | VOCAL | Vocal |
| 0x15 | TREBLE | Treble Boost |
| 0x16 | BASS | Bass Boost |
| 0x17 | SPEECH | Speech |
| 0x18–0x1F | RESERVED_FOR_FUTURE_NO24..31 | (blank) |
| 0xA0 | CUSTOM | **Manual** |
| 0xA1 | USER_SETTING1 | Custom 1 |
| 0xA2 | USER_SETTING2 | Custom 2 |
| 0xA3 | USER_SETTING3 | Custom 3 |
| 0xA4 | USER_SETTING4 | Custom 4 |
| 0xA5 | USER_SETTING5 | Custom 5 |
| 0xFF | UNSPECIFIED | (used in SET to mean "just set band levels") |

Labels from `com/sony/songpal/mdr/view/EqResourceMap.java` + `res/values/strings.xml`
(`EQ_Preset_Custom`="Manual", `EQ_Preset_UserSetting`="Custom" with " N" appended for USER_SETTING N).
The app prefers its fixed string for known IDs; for IDs without a fixed string it shows the UTF-8 name sent by the
device in RET_CAPABILITY. (Gaming/FPS/Heavy/Clear/Hard/Soft/Artist presets exist only in the v2 protocol enum, not table1.)

EqBandInformationType: 0x00 NO_INFORMATION, 0x01 HZ, 0x02 KHZ, 0x10 SPECIFIC_INFORMATION.
SpecificInformationType (value field when type = 0x10): 0x0000 DONT_CARE, **0x0001 CLEAR_BASS**.

#### 0x50 EQEBB_GET_CAPABILITY (host → device) — `tm0/l.java`
| off | value |
|---|---|
| 0 | 0x50 |
| 1 | EqEbbInquiredType |
| 2 | DisplayLanguage |

#### 0x51 EQEBB_RET_CAPABILITY (device → host) — `tm0/z1.java`

Variant PRESET_EQ (0x01) / PRESET_EQ_NONCUSTOMIZABLE (0x03):
| off | value |
|---|---|
| 0 | 0x51 |
| 1 | inquired type |
| 2 | band count (uint8) |
| 3 | level steps (uint8) — number of discrete positions per band |
| 4 | preset count N (uint8) |
| 5.. | N × { presetId (1 byte), nameLen L (uint8), name (L bytes UTF-8) } |

Parser treats L > 128 as 0. App-layer (`com/sony/songpal/mdr/j2objc/devicecapability/tableset1/e.java`,
`DeviceCapabilityTableset1Builder`) labels these "Band Count" and "Level Steps"; inquired type == 0x01 marks the EQ
as customizable.

Variant EBB (0x02):
| off | value |
|---|---|
| 0 | 0x51 |
| 1 | 0x02 |
| 2 | min level (int8, signed) |
| 3 | max level (int8, signed) |
(-128 is treated as invalid / clamped to -127.)

#### 0x52 EQEBB_GET_STATUS (host → device) — `tm0/o.java`
`[0x52, inquiredType]`

#### 0x53 EQEBB_RET_STATUS (device → host) — `tm0/c2.java`
`[0x53, inquiredType, CommonStatus]`. ENABLE (0x00) = EQ currently operable; DISABLE (0x01) = greyed out
(inferred: e.g. while another feature locks EQ).

#### 0x55 EQEBB_NTFY_STATUS (device → host) — `tm0/v0.java`
Same layout as 0x53. Parser maps OUT_OF_RANGE to ENABLE.

#### 0x56 EQEBB_GET_PARAM (host → device) — `tm0/n.java`
`[0x56, inquiredType]`

#### 0x57 EQEBB_RET_PARAM / 0x59 EQEBB_NTFY_PARAM (device → host) — `tm0/b2.java`, `tm0/u0.java`; payload `vm0/s.java`, `vm0/n.java`

PRESET_EQ / PRESET_EQ_NONCUSTOMIZABLE:
| off | value |
|---|---|
| 0 | 0x57 or 0x59 |
| 1 | inquired type |
| 2 | current EqPresetId |
| 3 | band count N |
| 4..4+N-1 | band level step per band (one byte each) |

EBB:
| off | value |
|---|---|
| 0 | 0x57 / 0x59 |
| 1 | 0x02 |
| 2 | EBB level (int8, signed, within capability min..max) |

#### 0x58 EQEBB_SET_PARAM (host → device) — `tm0/f3.java`
Same layout as RET_PARAM. Two usages seen in app (`h20/c.java`, `f20/b.java`):

* **Select preset:** `[0x58, 0x01 (or 0x03 if non-customizable), presetId, 0x00]` — band count 0, no band bytes.
* **Set band levels:** `[0x58, 0x01, 0xFF (UNSPECIFIED), N, step0 .. stepN-1]`. Device then reports the resulting
  preset (typically CUSTOM/0xA0 "Manual" — inferred) via NTFY_PARAM.
* **EBB:** `[0x58, 0x02, level(int8)]`, level range -127..127 enforced by app; device range from capability.

#### Band level encoding (read from `h20/c.java`)
Bytes on the wire are unsigned step indices `0 .. levelSteps-1`. Center/flat = `(levelSteps-1)/2`.
UI value = `step - (levelSteps-1)/2`, so min = `-(levelSteps-1)/2`, max = `+(levelSteps-1)/2`.
For WH-1000XM4 (known from device behaviour, not from this APK): levelSteps = 21 → raw 0..20, centre 10 → −10..+10.
(The protocol parser reads the bytes as Java signed bytes, but with steps ≤ 127 this does not matter.)

#### Clear Bass representation
Clear Bass is **not** a separate command on table1 customizable-EQ devices: it is one of the EQ "bands" in the
band array. Which index is Clear Bass is given by EXTENDED_INFO (band info type 0x10, value 0x0001). The app
(`com/sony/songpal/mdr/j2objc/tandem/features/eq/b.java`) finds the single CLEAR_BASS entry, removes it from the
frequency-slider list and shows it as a separate "CLEAR BASS" slider; it is encoded with the same step scheme as
other bands. `j2objc/feature/eqlisteningcomparison/EqBandStepsStandard.java` hard-codes the standard 6-band layout:
**[Clear Bass, 400 Hz, 1 kHz, 2.5 kHz, 6.3 kHz, 16 kHz]** (16 kHz may be reported as 15662 Hz or 16000 Hz, or
as KHZ value 16). So for XM4 a band-set message is
`[0x58, 0x01, 0xFF, 0x06, CB, b400, b1k, b2k5, b6k3, b16k]` with each byte = level+10 (inferred from typical
steps=21; confirm via RET_CAPABILITY byte 3).

#### 0x5A EQEBB_GET_EXTENDED_INFO (host → device) — `tm0/m.java`
`[0x5A, inquiredType]` (app sends PRESET_EQ or PRESET_EQ_NONCUSTOMIZABLE).

#### 0x5B EQEBB_RET_EXTENDED_INFO (device → host) — `tm0/a2.java`; payload `vm0/r.java`
Only parsed for inquired type 0x01/0x03.
| off | value |
|---|---|
| 0 | 0x5B |
| 1 | inquired type |
| 2 | entry count M (uint8) — must equal band count, otherwise app ignores it |
| 3.. | M × { EqBandInformationType (1 byte), value (uint16 **big-endian**) } |

Value meaning: HZ → frequency in Hz; KHZ → frequency in kHz; SPECIFIC_INFORMATION → SpecificInformationType code
(1 = Clear Bass). Label formatting (`EqResourceMap.b`): HZ < 1000 → "<n>" (Hz); HZ ≥ 1000 → value/1000 with one
decimal, rounded down, trailing zeros stripped, + "k" (e.g. 2500 → "2.5k", 6300 → "6.3k", 15662 → "15.6k");
KHZ → "<n>k".

---------------------------------------------------------------------------------------------------

### 2. AUDIO (0xE0–0xE9)

#### Enums
AudioInquiredType: 0x00 NO_USE, **0x01 CONNECTION_MODE**, **0x02 UPSCALING**, 0xFF OUT_OF_RANGE.
ConnectionModeSettingType: 0x00 SOUND_CONNECTION.
ConnectionModeSettingValue: 0x00 SOUND_QUALITY_PRIOR ("Priority on Sound Quality"), 0x01 CONNECTION_QUALITY_PRIOR
("Priority on Stable Connection").
UpscalingType: 0x00 DSEE_HX, 0x01 DSEE, 0x02 DSEE_HX_AI ("DSEE Extreme").
UpscalingSettingType: 0x00 AUTO_OFF.
UpscalingSettingValue: 0x00 OFF, 0x01 AUTO.

#### 0xE0 AUDIO_GET_CAPABILITY (host → device) — `tm0/b.java`
`[0xE0, AudioInquiredType]` (no language byte).

#### 0xE1 AUDIO_RET_CAPABILITY (device → host) — `tm0/p1.java`
| variant | layout |
|---|---|
| CONNECTION_MODE | `[0xE1, 0x01, ConnectionModeSettingType]` |
| UPSCALING | `[0xE1, 0x02, UpscalingType, UpscalingSettingType]` |

#### 0xE2 AUDIO_GET_STATUS — `tm0/e.java`: `[0xE2, type]`
#### 0xE3 AUDIO_RET_STATUS — `tm0/s1.java` / 0xE5 AUDIO_NTFY_STATUS — `tm0/r0.java`
`[op, type, CommonStatus]` — ENABLE = setting currently changeable (inferred).

#### 0xE6 AUDIO_GET_PARAM — `tm0/d.java`: `[0xE6, type]`

#### 0xE7 AUDIO_RET_PARAM / 0xE8 AUDIO_SET_PARAM / 0xE9 AUDIO_NTFY_PARAM — `tm0/r1.java`, `tm0/e3.java`, `tm0/q0.java`; payloads `vm0/k.java`, `vm0/z0.java`
| variant | layout |
|---|---|
| CONNECTION_MODE | `[op, 0x01, ConnectionModeSettingType (0x00), ConnectionModeSettingValue]` |
| UPSCALING | `[op, 0x02, UpscalingSettingType (0x00), UpscalingSettingValue (0x00 OFF / 0x01 AUTO)]` |

Examples: DSEE auto on: `[0xE8, 0x02, 0x00, 0x01]`. Stable connection: `[0xE8, 0x01, 0x00, 0x01]`.
UI strings warn that changing connection mode reconnects the device and stops playback.

#### Related: upscaling effect indicator (COMMON, not AUDIO) — `tm0/k0.java`, `tm0/y2.java`, `tm0/l1.java`
* 0x14 COMMON_GET_UPSCALING_EFFECT `[0x14, 0x00 (CommonCapabilityInquiredType.FIXED_VALUE)]`
* 0x15 RET / 0x17 NTFY `[op, 0x00, UpscalingEffectType, UpscalingEffectStatus]`
  UpscalingEffectType: 0x00 DSEE_HX, 0x01 DSEE, 0x02 DSEE_HX_AI. UpscalingEffectStatus: 0x00 OFF, 0x01 VALID
  (actively upscaling), 0x02 INVALID (on but not applicable to current source — inferred).

---------------------------------------------------------------------------------------------------

### 3. VPT / Sound Position (0x40–0x49)

#### Enums
VptInquiredType: 0x00 NO_USE, 0x01 VPT, 0x02 SOUND_POSITION, 0xFF OUT_OF_RANGE.
VptPresetId: 0x00 OFF, 0x01 OUTDOOR_FESTIVAL ("Outdoor Stage"), 0x02 ARENA, 0x03 CONCERT_HALL, 0x04 CLUB,
0x05–0x0F RESERVED. (UI title "Surround (VPT)".)
SoundPositionPresetId: 0x00 OFF, 0x01 FRONT_LEFT, 0x02 FRONT_RIGHT, 0x03 FRONT, 0x11 REAR_LEFT, 0x12 REAR_RIGHT,
0xFF OUT_OF_RANGE. (UI title "Sound Position Control".)
SoundPositionType: 0x00 NO_USE, 0x01 TYPE1, 0xFF OUT_OF_RANGE.

#### 0x40 VPT_GET_CAPABILITY (host → device) — `tm0/l0.java`
`[0x40, VptInquiredType, DisplayLanguage]`

#### 0x41 VPT_RET_CAPABILITY (device → host) — `tm0/z2.java`
| variant | layout |
|---|---|
| VPT (0x01) | `[0x41, 0x01, N, N × {VptPresetId, nameLen, UTF-8 name}]` (nameLen > 128 treated as 0) |
| SOUND_POSITION (0x02) | `[0x41, 0x02, SoundPositionType]` |

#### 0x42 VPT_GET_STATUS — `tm0/n0.java`: `[0x42, type]`
#### 0x43 VPT_RET_STATUS — `tm0/b3.java` / 0x45 VPT_NTFY_STATUS — `tm0/n1.java`
`[op, type, CommonStatus]` (NTFY maps OUT_OF_RANGE → ENABLE).

#### 0x46 VPT_GET_PARAM — `tm0/m0.java`: `[0x46, type]` — **confirmed**.

#### 0x47 VPT_RET_PARAM / 0x48 VPT_SET_PARAM / 0x49 VPT_NTFY_PARAM — `tm0/a3.java`, `tm0/t3.java`, `tm0/m1.java`; payloads `vm0/c1.java` (VPT), `vm0/j0.java` (Sound Position)
| variant | layout |
|---|---|
| VPT | `[op, 0x01, VptPresetId]` |
| SOUND_POSITION | `[op, 0x02, SoundPositionPresetId]` |

The existing client's `[0x48, type, preset]`, `[0x46, type]` and RET `[0x47, type, preset]` are **correct**.
Note SoundPosition rear values are 0x11/0x12, not 0x04/0x05.

---------------------------------------------------------------------------------------------------

### Open questions / uncertain

1. XM4-specific numbers (band count 6, level steps 21, presets list, band frequencies) are not in the APK; they come
   from the device's RET_CAPABILITY / RET_EXTENDED_INFO at runtime. The 6-band layout [ClearBass, 400, 1k, 2.5k,
   6.3k, 16k] is confirmed as the app's "standard" layout (EqBandStepsStandard), but index of Clear Bass must be
   taken from 0x5B. Steps=21 / offset 10 is inferred from community knowledge, not from this APK.
2. Whether XM4 answers 0x5A at all is unverified; if not, the app's band-label list stays empty (no hard-coded
   fallback found in table1 path).
3. After a band-level SET with presetId 0xFF, the preset the device reports (expected CUSTOM 0xA0) is inferred.
4. Exact semantic of RET_STATUS DISABLE for EQ/AUDIO/VPT (which features lock them) not traced.
5. UpscalingEffectStatus INVALID meaning is inferred from naming.
6. The app never sends PRESET_EQ_NONCUSTOMIZABLE in a band-level SET (only preset select); the SET parser handles
   only EBB and PRESET_EQ.

---

## Sony MDR protocol (v1): SYSTEM, GENERAL_SETTING, PLAY, ALERT, PERIPHERAL, VOICE_GUIDANCE

Extracted from Sound Connect 13.3.0 (com.sony.songpal.mdr) for interoperability. This file records facts only.
Byte offsets are positions inside the MDR *payload*: byte 0 is the opcode. The transport framing
(start/end markers, data type, sequence number, checksum) is not covered here.

Conventions
- "dir": H→D means host (app) to device. D→H means device to host.
- All counts and lengths are unsigned 8-bit.
- Strings are UTF-8 with a 1-byte length prefix and no terminator, unless noted otherwise.
- **CommonStatus** (used everywhere as "status"): `0x00 ENABLE`, `0x01 DISABLE`, `0xFF OUT_OF_RANGE`. **0 means enabled.**
- Data type: table1 commands (0x90–0xFD here) go out as `DATA_MDR = 0x0C`. Table2 commands (PERIPHERAL 0x30–0x3D and
  VOICE_GUIDANCE 0x40–0x49) go out as `DATA_MDR_NO2 = 0x0E`. Confirmed in `DataType.java` (`DATA_MDR(BSON.REF=12)`,
  `DATA_MDR_NO2(BSON.SYMBOL=14)`) and in `tv/d.java`, which selects TandemfamilyTableNumber MDR_NO1/MDR_NO2 by table.
- Capability discovery: the device's support-function list (CONNECT_RET_SUPPORT_FUNCTION, covered by another agent) reports
  `FunctionType` codes. The ones relevant here are:
  `0x38 PAIRING_DEVICE_MANAGEMENT_CLASSIC_BT`, `0x39 VOICE_GUIDANCE`, `0x92 VIBRATOR_ALERT_NOTIFICATION`,
  `0xA1 PLAYBACK_CONTROLLER`, `0xD1/0xD2/0xD3 GENERAL_SETTING1/2/3`, `0xF1 VIBRATOR`, `0xF2 POWER_SAVING_MODE`,
  `0xF3 CONTROL_BY_WEARING`, `0xF4 AUTO_POWER_OFF`, `0xF5 SMART_TALKING_MODE`, `0xF6 ASSIGNABLE_SETTINGS`.
  For SYSTEM and GENERAL_SETTING functions, the low nibble of the FunctionType is the inquired type.
  At connect, the app sends GET_CAPABILITY only for functions that are present (`tv/d.java`).

---

### 1. SYSTEM_* (0xF0–0xFD, table1)

| Opcode | Name | dir | Class (`tm0/`) |
|---|---|---|---|
| 0xF0 | SYSTEM_GET_CAPABILITY | H→D | f0.java |
| 0xF1 | SYSTEM_RET_CAPABILITY | D→H | t2.java |
| 0xF2 | SYSTEM_GET_STATUS | H→D | i0.java |
| 0xF3 | SYSTEM_RET_STATUS | D→H | w2.java |
| 0xF5 | SYSTEM_NTFY_STATUS | D→H | j1.java (same layout as 0xF3) |
| 0xF6 | SYSTEM_GET_PARAM | H→D | h0.java |
| 0xF7 | SYSTEM_RET_PARAM | D→H | v2.java |
| 0xF8 | SYSTEM_SET_PARAM | H→D | r3.java |
| 0xF9 | SYSTEM_NTFY_PARAM | D→H | i1.java (same layout as 0xF7) |
| 0xFA–0xFD | SYSTEM_*_EXTENDED_PARAM | | g0/u2/q3/h1. These are only implemented for SMART_TALKING_MODE (out of scope) |

**SystemInquiredType** (`SystemInquiredType.java`):
`0x00 NO_USE`, `0x01 VIBRATOR`, `0x02 POWER_SAVING_MODE`, `0x04 AUTO_POWER_OFF`, `0x05 SMART_TALKING_MODE`,
`0x06 ASSIGNABLE_SETTINGS`, `0xFF OUT_OF_RANGE`.
`0x03 CONTROL_BY_WEARING` is missing from this Java enum. It is implemented separately in the Kotlin-multiplatform layer
(see 1.5).

Generic request layouts (GET_CAPABILITY, GET_STATUS, GET_PARAM): `[op, inquiredType]` (2 bytes).

Generic STATUS layout (0xF3/0xF5) for every type except ASSIGNABLE_SETTINGS and SMART_TALKING_MODE (`vm0/l0.java`):

| off | meaning |
|---|---|
| 0 | 0xF3 / 0xF5 |
| 1 | inquired type |
| 2 | CommonStatus (0x00 enabled, 0x01 disabled). Tells you whether the function is currently usable |

#### 1.1 VIBRATOR (0x01)
- RET_CAPABILITY `[0xF1, 0x01, settingType]`. VibratorSettingType: `0x00 ON_OFF` (t2.java inner class g).
- RET/NTFY/SET_PARAM `[0xF7|0xF9|0xF8, 0x01, settingType=0x00, value]`. VibratorSettingValue: `0x00 OFF`, `0x01 ON`
  (`vm0/b1.java`).

#### 1.2 POWER_SAVING_MODE (0x02)
- RET_CAPABILITY `[0xF1, 0x02, settingType]`. PowerSavingModeSettingType: `0x00 ON_OFF` (t2.java inner class d).
- PARAM `[op, 0x02, settingType=0x00, value]`. PowerSavingModeSettingValue: `0x00 OFF`, `0x01 ON` (`vm0/i0.java`).

#### 1.3 AUTO_POWER_OFF (0x04)
RET_CAPABILITY (t2.java inner class c):

| off | meaning |
|---|---|
| 0 | 0xF1 |
| 1 | 0x04 |
| 2 | N = number of supported element IDs |
| 3..3+N-1 | AutoPowerOffElementId, one byte each |

**AutoPowerOffElementId** (`AutoPowerOffElementId.java`):

| hex | name | kind |
|---|---|---|
| 0x00 | POWER_OFF_IN_5_MIN | "select time" |
| 0x01 | POWER_OFF_IN_30_MIN | "select time" |
| 0x02 | POWER_OFF_IN_60_MIN | "select time" |
| 0x03 | POWER_OFF_IN_180_MIN | "select time" |
| 0x10 | POWER_OFF_WHEN_REMOVED_FROM_EARS | mode (wearing detection, "power off when not worn") |
| 0x11 | POWER_OFF_DISABLE | mode (never) |

The `isSelectTime()` helper returns true only for 0x00–0x03. The app splits the capability list into "modes" (0x10, 0x11) and
"select-time" entries (0x00–0x03). It shows a timeout picker only when at least 2 select-time IDs are supported
(`q00/b.java`).

PARAM (RET 0xF7 / NTFY 0xF9 / SET 0xF8) (`vm0/g.java`):

| off | meaning |
|---|---|
| 0 | op |
| 1 | 0x04 |
| 2 | AutoPowerOffParameterType: `0x01 ACTIVE_AND_SELECTIME_ID` (`0xFF OUT_OF_RANGE`) |
| 3 | active element ID (the one in effect) |
| 4 | "select time" element ID (the timeout the user last picked, kept even when a mode is active) |

Example (inferred): to set "power off when removed from ears" while remembering 30 min, send
`F8 04 01 10 01`. To use the timeout itself, send `F8 04 01 01 01`.
The app builds SET with (active, selectTime) in `q00/b.java`.
On WH-1000XM4 the capability probably contains only 0x10 and 0x11 (unverified).

#### 1.4 ASSIGNABLE_SETTINGS (0x06): custom button / NC-AMB key
RET_CAPABILITY (t2.java inner class b plus `wm0/c.java`, `wm0/b.java`, `wm0/d.java`, `wm0/a.java`). This is a nested
list:

```
[0xF1, 0x06,
 keyCount,
 repeat keyCount times:                       // "AsCapabilityKey"
   key (AssignableSettingsKey)
   keyType (AssignableSettingsKeyType)
   defaultPreset (AssignableSettingsPreset)   // see note
   presetCount
   repeat presetCount times:                  // "AsCapabilityPreset"
     preset (AssignableSettingsPreset)
     actionCount
     repeat actionCount times:                // "AsCapabilityAction"
       action (AssignableSettingsAction)
       function (AssignableSettingsFunction)
]
```
- Note: the 3rd byte of each key entry is parsed as an AssignableSettingsPreset. Its role as the "default/factory preset"
  is inferred.
- The parser quirk: if presetCount or actionCount is 0, the sub-parser returns a consumed length of 1. Entries with any
  OUT_OF_RANGE field, or with empty sub-lists, are dropped.

STATUS (0xF3 / 0xF5, `vm0/e.java`): `[op, 0x06, n, CommonStatus × n]`. The app treats the function as enabled if any entry
is 0x00. That there is one entry per key is inferred.

PARAM (RET 0xF7 / NTFY 0xF9 / SET 0xF8, `vm0/d.java`): `[op, 0x06, n, preset × n]`. There is one preset per key, in the
same order as the keys in the capability (`l00/b.java` fills an array of key-count length and puts each key's preset at
that key's capability index).

**AssignableSettingsKey**: `0x00 LEFT_SIDE_KEY`, `0x01 RIGHT_SIDE_KEY`, `0x02 CUSTOM_KEY`, `0x03 C_KEY`, `0xFF OUT_OF_RANGE`.
**AssignableSettingsKeyType**: `0x00 TOUCH_SENSOR`, `0x01 BUTTON`, `0xFF OUT_OF_RANGE`.
**AssignableSettingsPreset**:

| hex | name |
|---|---|
| 0x00 | AMBIENT_SOUND_CONTROL |
| 0x10 | VOLUME_CONTROL |
| 0x20 | PLAYBACK_CONTROL |
| 0x30 | VOICE_RECOGNITION (phone's assistant) |
| 0x31 | GOOGLE_ASSISTANT |
| 0x32 | AMAZON_ALEXA |
| 0x33 | TENCENT_XIAOWEI |
| 0xFF | NO_FUNCTION |
| 0xFE | OUT_OF_RANGE (internal) |

**AssignableSettingsAction**: `0x00 SINGLE_TAP`, `0x01 DOUBLE_TAP`, `0x02 TRIPLE_TAP`, `0x10 SINGLE_TAP_AND_HOLD`,
`0x11 DOUBLE_TAP_AND_HOLD`, `0x21 LONG_PRESS_THEN_ACTIVATE`, `0x22 LONG_PRESS_DURING_ACTIVATION`, `0xFF OUT_OF_RANGE`.
**AssignableSettingsFunction**: `0x00 NO_FUNCTION`, `0x01 NC_ASM_OFF`, `0x02 NC_OPTIMIZER`, `0x10 QUICK_ATTENTION`,
`0x11 VOLUME_UP`, `0x12 VOLUME_DOWN`, `0x20 PLAY_PAUSE`, `0x21 NEXT_TRACK`, `0x22 PREVIOUS_TRACK`,
`0x30 VOICE_RECOGNITION`, `0x31 GET_YOUR_NOTIFICATION`, `0x32 TALK_TO_GA`, `0x33 STOP_GA`, `0x34 VOICE_INPUT_CANCEL_AA`,
`0x35 TALK_TO_TENCENT_XIAOWEI`, `0x36 CANCEL_VOICE_RECOGNITION`, `0xFF OUT_OF_RANGE`.
The action→function pairs in the capability are descriptive only, for showing what each preset does. The host sets only
the preset.

Related alerts: changing the key to or from a voice assistant can trigger ALERT 0x99 with
`DISCONNECT_CAUSED_BY_CHANGING_KEY_ASSIGN` or `DUAL_ASSIGN_OF_VOICE_ASSISTANT_IS_UNAVAILABLE` (section 4).

#### 1.5 CONTROL_BY_WEARING (inquired type 0x03): "pause when headphones are taken off"
This is not in the Java SystemInquiredType enum. It is implemented in the KMP layer (`xi0/*.java`). The app probes it
when FunctionType `0xF3 CONTROL_BY_WEARING` is present (`tv/d.java` → `bg0/c.java`: TABLE1, expects reply 0xF1 with
inquired type 3). Each parser enforces an exact length.

| Message | Bytes | Source |
|---|---|---|
| GET_CAPABILITY | `F0 03` | xi0/b.java |
| RET_CAPABILITY | `F1 03 settingType` (3 bytes). settingType `0x00 ON_OFF` | xi0/l.java, k.java |
| GET_STATUS | `F2 03` | xi0/f.java |
| RET_STATUS / NTFY_STATUS | `F3 03 status` / `F5 03 status` (3 bytes). status: `0x00 ENABLE`, `0x01 DISABLE` | xi0/p.java, j.java |
| GET_PARAM | `F6 03` | xi0/d.java |
| RET_PARAM / NTFY_PARAM | `F7 03 00 value` / `F9 03 00 value` (4 bytes). value `0x00 OFF`, `0x01 ON` | xi0/n.java, h.java |
| SET_PARAM | `F8 03 00 value` (4 bytes) | xi0/r.java (opcode literal shows as `Imgcodecs.IMWRITE_PNG_ALL_FILTERS` = 248 = 0xF8) |

(Enums: `ProtocolSettingType ON_OFF=0`, `ProtocolSettingValueOnOff OFF=0/ON=1`, `ProtocolStatus ENABLE=0/DISABLE=1`.)
"Power off when not worn" is not here. It is AUTO_POWER_OFF element 0x10 (section 1.3).

#### 1.6 SMART_TALKING_MODE (0x05)
Out of scope (covered by another agent). Note: `u2.java` (SYSTEM_RET_EXTENDED_PARAM) passes `SPORTS_RET_EXTENDED_PARAM`
(0xBB) to its constructor, which looks like a bug in Sony's code. Only parsing matters for a client, and parsing keys on
byte 1.

---

### 2. GENERAL_SETTING_* (0xD0–0xD9, table1): generic capability-described settings

| Opcode | Name | dir | Class |
|---|---|---|---|
| 0xD0 | GET_CAPABILITY | H→D | um0/a.java |
| 0xD1 | RET_CAPABILITY | D→H | um0/f.java (+ xm0/d.java, xm0/b.java; KMP ui0/p.java) |
| 0xD2 | GET_STATUS | H→D | um0/c.java |
| 0xD3 | RET_STATUS | D→H | um0/h.java |
| 0xD5 | NTFY_STATUS | D→H | um0/e.java (tm0/e) |
| 0xD6 | GET_PARAM | H→D | um0/b.java |
| 0xD7 | RET_PARAM | D→H | um0/g.java |
| 0xD8 | SET_PARAM | H→D | um0/i.java |
| 0xD9 | NTFY_PARAM ("NTNY" in enum) | D→H | um0/d.java (tm0/d) |

**GsInquiredType**: `0xD1 GENERAL_SETTING1`, `0xD2 GENERAL_SETTING2`, `0xD3 GENERAL_SETTING3`, `0xFF OUT_OF_RANGE`.
These match FunctionType 0xD1–0xD3 in the support-function list. The app sends GET_CAPABILITY for each present one.
**GsSettingType**: `0x00 NO_USE`, `0x01 BOOLEAN_TYPE`, `0x02 LIST_TYPE`, `0xFF OUT_OF_RANGE`.
**GsStringFormat**: `0x00 NO_USE`, `0x01 RAW_NAME` (display the string as-is), `0x02 ENUM_NAME` (the string is a
symbolic key that the app maps to a localized string), `0xFF OUT_OF_RANGE`.

#### GET_CAPABILITY `[0xD0, gsType, displayLanguage]`
displayLanguage is an MdrLanguage code. It presumably selects the language of RAW_NAME strings (inferred):
`00 undefined, 01 en, 02 fr, 03 de, 04 es, 05 it, 06 pt, 07 nl, 08 sv, 09 fi, 0A ru, 0B ja, 0C zh-Hans, 0D pt-BR, 0E zh-Hant,
0F ko, 10 tr, F0 zh`.

#### RET_CAPABILITY (0xD1)
```
off 0   0xD1
off 1   gsType (D1/D2/D3)
        -- title info block ("ProtocolGeneralSettingInfo") --
off 2   stringFormat (GsStringFormat)
off 3   subjectLen  (Java parser accepts 1..128; 0 = empty)
        subject     UTF-8 [subjectLen]
        summaryLen  (0 = no summary)
        summary     UTF-8 [summaryLen]
        -- end info block --
        settingType (GsSettingType)
        if settingType == LIST_TYPE (0x02):
          itemCount (1..64)
          repeat itemCount: info block (stringFormat, nameLen, name, descLen, desc)
```
For BOOLEAN_TYPE nothing follows settingType. The KMP parser checks that the total length is exact.

Known ENUM_NAME subject keys (`com/sony/songpal/mdr/j2objc/cap/GsTitleTitle.java`, mapped to UI strings in
`GsTitleTitleResourceMap.java`):
`ASSIGNABLE_KEY_SETTING`, `ASSIGNABLE_KEY_SETTING_FOR_SEPARATED`, `ASSIGNABLE_KEY_SETTING_NC`, `ASSIGNABLE_KEY_SETTING_NCAMB`,
`ASSIGNABLE_KEY_SETTING_CUSTOM`, `ASSIGNABLE_KEY_SETTING_C`, `ASSIGNABLE_KEY_SETTING_FOR_SEPARATED_R`,
`VOICE_GUIDANCE_SETTING`, **`TOUCH_PANEL_SETTING`** ("Touch sensor control panel"), **`MULTIPOINT_SETTING`**
("Connect to 2 devices simultaneously"), `FACETAP_SETTING`, `SELECTABLE_FACETAP_SETTING`, `SIDETONE_SETTING`,
`TWS_ONE_SIDE_USE_NCASM_SETTING`, `TAP_SENSITIVITY_SETTING`.
For ENUM_NAME list items the app also maps the item string through a resource table, but I did not extract the item-key
list. For RAW_NAME, show subject/summary/name/desc directly.
**Generic rendering rule:** BOOLEAN → toggle, LIST → picker of `itemCount` entries indexed 0..n-1. Use subject as the
title and summary as the description.

#### GET_STATUS `[0xD2, gsType]` → RET_STATUS / NTFY_STATUS `[0xD3|0xD5, gsType, CommonStatus]`
0x00 means the setting is currently changeable/enabled. 0x01 means disabled (grey it out).

#### GET_PARAM `[0xD6, gsType]` → RET_PARAM / NTFY_PARAM
`[0xD7|0xD9, gsType, settingType, value]`
- BOOLEAN (settingType 0x01): value = CommonOnOffSettingValue `0x00 OFF`, `0x01 ON` (`xm0/a.java`; KMP
  `ProtocolSettingValueOnOff`).
- LIST (settingType 0x02): value = currently selected item index, 0..63 (`xm0/c.java`).

#### SET_PARAM `[0xD8, gsType, settingType, value]`
Same value encoding as above. Booleans: `D8 Dx 01 00|01`. Lists: `D8 Dx 02 idx`.
(Caution: the *v2* protocol's GsSettingValue has ON=0x00/OFF=0x01. That enum is not used for v1, which uses OFF=0/ON=1.)

---

### 3. PLAY_* (0xA0–0xA9, table1): playback controller

| Opcode | Name | dir | Class (`tm0/`) |
|---|---|---|---|
| 0xA0 | GET_CAPABILITY `[A0, playType]` | H→D | v.java |
| 0xA1 | RET_CAPABILITY | D→H | j2.java |
| 0xA2 | GET_STATUS `[A2, playType]` | H→D | x.java |
| 0xA3 | RET_STATUS | D→H | l2.java |
| 0xA4 | SET_STATUS | H→D | l3.java |
| 0xA5 | NTFY_STATUS | D→H | d1.java (same as A3) |
| 0xA6 | GET_PARAM `[A6, playType, detailedDataType]` | H→D | w.java |
| 0xA7 | RET_PARAM | D→H | k2.java |
| 0xA8 | SET_PARAM | H→D | k3.java |
| 0xA9 | NTFY_PARAM | D→H | c1.java |

**PlayInquiredType**: `0x00 NO_USE`, `0x01 PLAYBACK_CONTROLLER`, `0xFF`. Only 0x01 is used.

RET_CAPABILITY `[A1, 01, volumeSteps, playbackControlType, metaDataDisplayType]`
- volumeSteps: unsigned 0..255. The app builder calls it "volume step" (`DeviceCapabilityTableset1Builder.java`). It is
  the number of levels including 0: the WH-1000XM4 (fw 3.0.1) replies `a1 01 1f 01 01` (31 steps, transport and metadata
  supported) and has reported volume 30 (`a7 01 20 1e`) but never 31. Values are taken as 0..volumeSteps-1, as with EQ
  steps (inferred; to be confirmed at full volume).
- PlaybackControlType: `0x00 NOT_SUPPORT`, `0x01 PLAY_PAUSE_TRACKUP_TRACKDOWN`.
- MetaDataDisplayType: `0x00 NOT_SUPPORT`, `0x01 TRACK_ALBUM_ARTIST_GENRE_PLAYER`.

RET/NTFY_STATUS `[A3|A5, 01, CommonStatus, PlaybackStatus]`.
PlaybackStatus: `0x00 UNSETTLED`, `0x01 PLAY`, `0x02 PAUSE`, `0x03 STOP`, `0xFF`.

SET_STATUS `[A4, 01, 0x00 (CommonStatus ENABLE, always), PlaybackControl]` (`c40/c.java` uses it for transport keys).
PlaybackControl: `0x00 KEY_OFF`, `0x01 PAUSE`, `0x02 TRACK_UP` (next), `0x03 TRACK_DOWN` (previous), `0x04 GROUP_UP`,
`0x05 GROUP_DOWN`, `0x06 STOP`, `0x07 PLAY`, `0x08 FAST_FORWARD`, `0x09 FAST_REWIND`.
(The app sends PLAY 0x07 or PAUSE 0x01 explicitly. There is no toggle value.)

**PlaybackDetailedDataType**: `0x00 TRACK_NAME`, `0x01 ALBUM_NAME`, `0x02 ARTIST_NAME`, `0x03 GENRE_NAME`,
`0x10 PLAYER_NAME`, `0x20 VOLUME`, `0xFF`.

RET_PARAM (0xA7) by detailed type:
- Names (0x00–0x03, 0x10) (`vm0/e0.java`): `[A7, 01, dataType, PlaybackNameStatus, len, UTF-8 name[len]]`. len is capped at
  128. If len is 0, the status is forced to NOTHING.
  PlaybackNameStatus: `0x00 UNSETTLED`, `0x01 NOTHING`, `0x02 SETTLED`.
- Volume (0x20) (`vm0/g0.java`): `[A7, 01, 0x20, volume]`.

SET_PARAM (volume only) `[A8, 01, 0x20, volume]` (`c40/c.java`).

NTFY_PARAM (0xA9) (`c1.java`):
- For TRACK_NAME or PLAYER_NAME: `[A9, 01, dataType]` with no string (`vm0/f0.java` reads only the type). This means
  "this name changed, re-query with A6" (interpretation inferred).
- For VOLUME: `[A9, 01, 0x20, volume]`.
- Other types are ignored by the app.

---

### 4. ALERT_* (0x90–0x99, table1)

| Opcode | Name | dir | Class (`tm0/`) |
|---|---|---|---|
| 0x90 | GET_CAPABILITY `[90, alertType]` | H→D | a.java |
| 0x91 | RET_CAPABILITY `[91, alertType, AlertVibrationPattern]` | D→H | o1.java |
| 0x94 | SET_STATUS `[94, alertType, CommonStatus]` | H→D | d3.java |
| 0x98 | SET_PARAM (host's reply) | H→D | c3.java (+ vm0/u.java, vm0/a1.java) |
| 0x99 | NTFY_PARAM (device asks the user something) | D→H | o0.java |

**AlertInquiredType**: `0x00 NO_USE`, `0x01 FIXED_MESSAGE`, `0x02 VIBRATOR_ALERT_NOTIFICATION`, `0xFF`.
**AlertVibrationPattern**: `0x00 NO_USE`, `0x01 ONE_PATTERN_ONLY`.
**AlertMessageType**:

| hex | name |
|---|---|
| 0x00 | NO_USE |
| 0x01 | DISCONNECT_CAUSED_BY_CONNECTION_MODE_CHANGE |
| 0x02 | DISCONNECT_CAUSED_BY_CHANGING_KEY_ASSIGN |
| 0x03 | NEED_DISCONNECTION_FOR_UPDATING_FIRMWARE |
| 0x04 | GOOGLE_ASSISTANT_IS_NOW_AVAILABLE |
| 0x05 | DUAL_ASSIGN_OF_VOICE_ASSISTANT_IS_UNAVAILABLE |
| 0x06 | DISCONNECT_CAUSED_BY_CHANGING_MULTIPOINT_TO_ON |
| 0x07 | DISCONNECT_CAUSED_BY_CHANGING_MULTIPOINT_TO_OFF |
| 0x08 | BATTERY_CONSUMPTION_INCREASE_DUE_TO_EQ_AND_UPSCALING |

**AlertActionType**: `0x00 CONFIRMATION_ONLY` (show OK), `0x01 POSITIVE_NEGATIVE` (show OK/Cancel).
**AlertAction**: `0x00 NEGATIVE`, `0x01 POSITIVE`.

Flow:
1. To enable fixed-message alerts, the app sends `94 01 00` (FIXED_MESSAGE, ENABLE) (`f00/a.java`). The trigger point
   (probably at connect) is inferred.
2. The device sends `99 01 msgType actionType` (`o0.java`).
3. The app replies `98 01 msgType action` (`vm0/u.java`). msgType echoes the one received. action is 0x01 POSITIVE or
   0x00 NEGATIVE. The device presumably blocks the pending change (e.g. a multipoint toggle or key reassignment that
   forces a disconnect) until it gets this reply. **A client that enables these alerts must answer them.**
   Sending POSITIVE accepts the change.
- Vibrator alert SET_PARAM: `98 02 vibrationType` with VibrationType `0x00 NO_PATTERN_SPECIFIED` (`vm0/a1.java`).

---

### 5. Table2 (data type 0x0E)

#### 5.1 PERIPHERAL_* (0x30–0x3D): paired-device management / multipoint
| Opcode | Name | dir | Class (`an0/`) |
|---|---|---|---|
| 0x30 | GET_CAPABILITY `[30, pType]` (exactly 2 bytes) | H→D | a.java |
| 0x31 | RET_CAPABILITY | D→H | l.java → m.java |
| 0x32 | GET_STATUS `[32, pType]` | H→D | c.java |
| 0x33 | RET_STATUS | D→H | p.java |
| 0x34 | SET_STATUS | H→D | t.java |
| 0x35 | NTFY_STATUS | D→H | i.java → j.java |
| 0x36 | GET_PARAM `[36, pType]` | H→D | b.java |
| 0x37 | RET_PARAM (device list) | D→H | n.java → o.java |
| 0x39 | NTFY_PARAM (device list) | D→H | g.java → h.java |
| 0x3C | SET_EX_PARAM (connect/disconnect/unpair) | H→D | q.java → r.java → s.java |
| 0x3D | NTFY_EX_PARAM (result of 0x3C) | D→H | d.java → e.java → f.java |

**PeripheralInquiredType**: `0x00 NO_USE`, `0x01 PAIRING_DEVICE_MANAGEMENT_CLASSIC_BT`.
**PeripheralBluetoothModeStatus**: `0x00 NORMAL_MODE`, `0x01 INQUIRY_SCAN_MODE` (pairing mode), `0xFF`.
**PeripheralDetailDataType**: `0x00 CONNECTION_CONTROL`.
**ActionType**: `0x00 DISCONNECT`, `0x01 CONNECT`, `0x02 UNPAIR`.
**FileTransferInMultiConnection**: `0x00 POSSIBLE`, `0x01 IMPOSSIBLE`.
**ResultType**: 0x00/01/02/03 = DISCONNECTION_{SUCCESS, ERROR, IN_PROGRESS, BUSY}; 0x10–0x13 = CONNECTION_{…};
0x20–0x23 = UNPAIRING_{…}; 0x30–0x33 = PAIRING_{…}.

RET_CAPABILITY (exactly 5 bytes): `[31, 01, maxPairedDevices, maxConnectedDevices, FileTransferInMultiConnection]`
(field names from `devicecapability/tableset1/k.java` toString).

RET/NTFY_STATUS and SET_STATUS (exactly 4 bytes): `[33|35|34, 01, BluetoothModeStatus, CommonStatus]`.
`34 01 01 00` presumably puts the headset into pairing (inquiry-scan) mode so that "add device" works. That use is
inferred from the enum names.

RET_PARAM / NTFY_PARAM device list (`o.java`, `h.java`):

| off | meaning |
|---|---|
| 0 | 0x37 / 0x39 |
| 1 | 0x01 |
| 2 | N = number of paired devices |
| repeat N | 17 bytes BD address as ASCII text `"XX:XX:XX:XX:XX:XX"` |
| | 1 byte connectedStatus. 0 = paired but not connected. >0 = connected, and the value orders the connected devices (the app sorts ascending) |
| | 1 byte nameLen (validated 1..128) |
| | nameLen bytes friendly name (UTF-8 per KMP. The Java code uses the platform default charset) |
| last | 1 byte **playbackRightDevice**: the connectedStatus value of the device that currently holds playback (KMP name `playbackrightDevice`) |

The validator requires `total length == offset after the last device + 1`, so the trailing byte is mandatory.
→ **Confirms the existing client's layout. One correction: there is one extra trailing byte after the list.** The "slot"
byte is the connection-order number, with 0 meaning not connected.

SET_EX_PARAM (exactly 21 bytes) (`s.java`): `[3C, 01, 00 (CONNECTION_CONTROL), ActionType, 17-byte ASCII BD address]`.
**The existing client's format is correct.**
NTFY_EX_PARAM result (exactly 21 bytes) (`f.java`): `[3D, 01, 00, ResultType, 17-byte ASCII BD address]`.

Multipoint on/off is **not** a PERIPHERAL command in v1. It is exposed as a GENERAL_SETTING (section 2) whose subject is
ENUM_NAME `MULTIPOINT_SETTING`. Toggling it may produce ALERT 0x99 with msg 0x06 or 0x07, which must be answered with
0x98.

(The KMP package `sj0/` contains a richer peripheral model: SourceSwitchControl, MusicHandOverSetting, and a
device-info variant with Bluetooth Class-of-Device. Its pairing-list parser checks inquired type 0x00 rather than 0x01. It
is most likely a different/newer table and not used for WH-1000XM4. See Open questions.)

#### 5.2 VOICE_GUIDANCE_* (0x40–0x49)
| Opcode | Name | dir | Class (`bn0/`) |
|---|---|---|---|
| 0x40 | GET_CAPABILITY `[40, vgType]` | H→D | a.java |
| 0x41 | RET_CAPABILITY | D→H | i.java |
| 0x42 | GET_STATUS `[42, vgType, StatusType]` (3 bytes) | H→D | c.java |
| 0x43 | RET_STATUS | D→H | r.java → t.java (ON_OFF), s.java (LANGUAGE) |
| 0x44 | SET_STATUS | H→D | w.java → y.java (ON_OFF), x.java (LANGUAGE) |
| 0x45 | NTFY_STATUS | D→H | f.java → h.java (ON_OFF), g.java (LANGUAGE) |
| 0x46 | GET_PARAM `[46, vgType, DetailedDataType]` (3 bytes) | H→D | b.java |
| 0x47 | RET_PARAM | D→H | j.java → m/l/n/k/o(p,q).java |
| 0x48 | SET_PARAM | H→D | u.java → v.java (ON_OFF only) |
| 0x49 | NTFY_PARAM | D→H | d.java → e.java (ON_OFF only) |

**VoiceGuidanceInquiredType**: `0x00 NO_USE`, `0x01 VOICE_GUIDANCE_SETTING`.
**StatusType**: `0x00 NO_USE`, `0x01 ON_OFF`, `0x02 LANGUAGE`.
**DetailedDataType**: `0x00 NO_USE`, `0x01 ON_OFF`, `0x02 LANGUAGE`, `0x03 REQUIRED_TIME`, `0x04 DOWNLOAD_SERVER_METHOD`,
`0x05 UPDATE_METHOD`.
**VoiceGuidanceSettingValue**: `0x00 OFF`, `0x01 ON`, `0xFF`.
**SupportsSwitch**: `0x00 NOT_SUPPORT`, `0x01 SUPPORT`.
**VoiceGuidanceLanguage** (MdrLanguage codes): `01 en, 02 fr, 03 de, 04 es, 05 it, 06 pt, 07 nl, 08 sv, 09 fi, 0A ru,
0B ja, 0D pt-BR, 0F ko, 10 tr, F0 zh`, with `00` undefined.

RET_CAPABILITY (`i.java`, interpreted in `tv/a.java` `t()`):

| off | meaning |
|---|---|
| 0 | 0x41 |
| 1 | 0x01 |
| 2 | SupportsSwitch: on/off switch supported |
| 3 | SupportsSwitch: language switch supported |
| 4 | (only if byte 3 == SUPPORT) N = number of languages |
| 5..5+N-1 | VoiceGuidanceLanguage codes |

If byte 3 is NOT_SUPPORT, the message is exactly 4 bytes. Otherwise it is exactly 5+N bytes.

RET/NTFY_STATUS (exactly 4 bytes): `[43|45, 01, StatusType, CommonStatus]` (0x00 = enabled).
SET_STATUS (4 bytes): `[44, 01, StatusType, CommonStatus]`.

RET_PARAM variants (0x47, byte 1 = 0x01):
- ON_OFF: `[47, 01, 01, VoiceGuidanceSettingValue]` (4 bytes) (`m.java`).
- LANGUAGE: `[47, 01, 02, languageCode]` (4 bytes) (`l.java`). Reports the current language.
- REQUIRED_TIME: `[47, 01, 03, n]` (4 bytes). n is capped at 128. Probably the time the language update takes (unit
  unknown) (`n.java`).
- DOWNLOAD_SERVER_METHOD (`k.java`): `[47, 01, 04, 0x01 AUTOMAGIC, catIdLen(>=1), categoryId…, serialLen(>=1), serial…,
  serviceCount(>=1), then serviceCount × (languageCode, serviceIdLen(>=1), serviceId…)]`. Total length must match
  exactly. This is the language-pack download descriptor.
- UPDATE_METHOD (`o.java` → `p.java` / `q.java`):
  `[47, 01, 05, UpdateMethod, …]`. UpdateMethod values: `00 TANDEM, 10 CSR, 11 CSR_RESUMABLE, 12 CSR_TWS,
  13 CSR_TWS_RESUMABLE, 20 MTK, 21 MTK_RESUMABLE, 23 MTK_TWS_RESUMABLE, 25 MTK_BACKGROUND_RESUMABLE,
  27 MTK_TWS_BACKGROUND_RESUMABLE`.
  - p-variant (6 bytes): byte 4 = TxPower (signed), byte 5 = battery power threshold.
  - q-variant (≥7 bytes): byte 4 = battery threshold, byte 5 = battery threshold for interrupting, byte 6 = idLen,
    then the unique ID for device binding.

SET_PARAM: only ON_OFF is accepted: `[48, 01, 01, 00|01]` (`v.java`). LANGUAGE, REQUIRED_TIME and the other types
throw in the builder. **Changing the guidance language is not a simple SET.** It goes through a data download/transfer
using the DOWNLOAD_SERVER_METHOD / UPDATE_METHOD info, a firmware-update-style transfer (inferred).
NTFY_PARAM: `[49, 01, 01, value]` (ON_OFF only).
The voice-guidance *volume* is not part of the v1 table2 messages in this app (it exists only in newer MTK/v2 item IDs).

---

### Open questions / uncertain
1. **SYSTEM 0x03 CONTROL_BY_WEARING:** the byte layout is read directly from KMP xi0/*. Whether WH-1000XM4 reports
   FunctionType 0xF3 has not been verified on a device.
2. **AUTO_POWER_OFF byte 4 ("select time" ID):** the role comes from the app's naming (`sendAutoPowerOffActiveElementId(elementId,
   selectTimeElementId)`). For devices with no timeout entries, the app likely echoes the active ID or another valid ID.
   Not verified.
3. **ASSIGNABLE_SETTINGS:** the per-key 3rd byte being the "default preset", and the STATUS list being one entry per key,
   are both inferred.
4. **GENERAL_SETTING ENUM_NAME list items:** the localized-key table for list items was not extracted. Which GS slot
   (D1/D2/D3) carries TOUCH_PANEL vs MULTIPOINT on XM4 is device-reported. Discover it from the capability subject.
5. **GS GET_CAPABILITY displayLanguage:** that this byte selects the RAW_NAME language is inferred.
6. **PLAY volume range:** 0..volumeSteps-1 is inferred from the XM4 capture (31 steps, volume 30 seen) and the EQ steps convention.
7. **PLAY NTFY_PARAM name types:** these carry only the type byte. "Re-query" is an inference.
8. **ALERT:** the exact moment the app sends `94 01 00` was not traced. What happens if no 0x98 reply arrives
   (timeout/default) is unknown.
9. **PERIPHERAL name encoding:** UTF-8 is assumed (KMP decodes UTF-8). SET_STATUS with INQUIRY_SCAN_MODE meaning "enter
   pairing mode" is inferred.
10. **KMP `sj0/` peripheral classes:** they use inquired type 0x00 for the pairing list and add Class-of-Device, source
    switch and music hand-over variants. They probably belong to a different protocol table/version and do not apply to
    XM4. Not resolved.
11. **VOICE_GUIDANCE REQUIRED_TIME:** the unit is unknown.
