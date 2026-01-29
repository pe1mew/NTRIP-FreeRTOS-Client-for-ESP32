
# ToDo List

Topics that need further development.

## 1. Nearest Base Selection / Automatic Mountpoint Assignment

In many RTK networks, especially those offering VRS (Virtual Reference Station) or i-Mount services, your receiver sends its approximate position (typically using a GGA NMEA sentence) to the NTRIP caster, which then assigns or generates correction data from the most suitable base station (or synthesizes a VRS stream).

### Workflow Overview
1. **Client sends a GGA message** to the caster (part of the NTRIP protocol for some mountpoints) indicating its estimated position.
2. **Caster receives the GGA** and may select (or dynamically reassign) the best ‘real’ base station, or generate a virtual base station correction (VRS).
3. **Caster sends corrections**—either from a real base or a synthesized virtual base—back to the client.

### Detecting Base Selection or Change

#### RTCM Messages to Monitor
- **RTCM 1005/1006:** Contain the current base station’s ECEF coordinates. If these coordinates change during your session (e.g., after sending a new GGA), the base station for your stream has likely switched. In VRS modes, the base coordinates will closely match your reported position.
- **RTCM 1033, 1007, 1008:** Identify antenna model, serial, and sometimes a station ID. If these values change, the base station supplying corrections has likely changed.
- **RTCM 1013 (Network ID), 1029, 1031:** Less commonly included, but may transmit base network information or broader context.

#### Best Practical Method
No specialized NTRIP message notifies you directly that your base has changed, but monitoring these RTCM messages is the most reliable indirect way.

| Step                                  | Rationale                                                                                       |
|----------------------------------------|-------------------------------------------------------------------------------------------------|
| **Record RTCM 1005/1006 messages**     | Reveal the current base station’s coordinates.                                                  |
| **Monitor for coordinate changes**     | If the base changes, new 1005/1006 messages will show new positions; in VRS, they'll track your GGA positions. |
| **Watch 1007/1008/1033 messages**      | If base antenna info or IDs change, the source of corrections has changed.                      |

#### Example: Detecting a Change
1. Connect to the caster; log/parse RTCM.
2. Send GGA; caster reassigns the stream (if applicable).
3. Observe new 1006/1033: If position changes from previous, or antenna ID updates, base changed.

