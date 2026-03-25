#pragma once

/* Binary framing constants shared by sender (sensorEmulatorTask)
 * and receiver (telemetryReceiverTask). */
#define FRAME_SOH  0x01u   /* Start of Header — never stuffed */
#define FRAME_CAN  0x18u   /* Cancel / end of frame — never stuffed */
#define FRAME_DLE  0x10u   /* Data Link Escape */
