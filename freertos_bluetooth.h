/*
 * Copyright (C) 2019 AndrewLabs.
 *
 * This file is a derivative work of Silicon Labs open source code subject to the
 * terms found at www.silabs.com/about-us/legal/master-software-license-agreement,
 * and Copyright 2018 Silicon Laboratories Inc.
 */

#ifndef __FREERTOS_BLUETOOTH_H__
#define __FREERTOS_BLUETOOTH_H__

#include "rtos_gecko.h"

#include "FreeRTOS.h"
#include "event_groups.h"

// Constants.
// ==========

#define BLUETOOTH_EVENT_FLAG_STACK        ((EventBits_t)1)    // Link-layer task signals BLE task needs an update.
#define BLUETOOTH_EVENT_FLAG_LL           ((EventBits_t)2)    // Link-layer task needs an update.
#define BLUETOOTH_EVENT_FLAG_CMD_WAITING  ((EventBits_t)4)    // BGAPI command is waiting to be processed.
#define BLUETOOTH_EVENT_FLAG_RSP_WAITING  ((EventBits_t)8)    // BGAPI response is waiting to be processed.
#define BLUETOOTH_EVENT_FLAG_EVT_WAITING  ((EventBits_t)16)   // BGAPI event is waiting to be processed.
#define BLUETOOTH_EVENT_FLAG_EVT_HANDLED  ((EventBits_t)32)   // BGAPI event is not pending.

// Typedefs.
// =========

typedef void (*wakeupCallback)(void);
typedef errorcode_t(*bluetooth_stack_init_func)(void);

// Variables.
// ==========

extern EventGroupHandle_t bluetooth_event_flags;
extern volatile struct gecko_cmd_packet* bluetooth_evt;

// Functions.
// ==========

void bluetooth_start(UBaseType_t ll_priority, UBaseType_t stack_priority, bluetooth_stack_init_func initialize_bluetooth_stack);
void BluetoothSetWakeupCallback(wakeupCallback cb);
void BluetoothLLCallback(void);
void BluetoothUpdate(void);
BaseType_t BluetoothPend(void);
BaseType_t BluetoothPost(void);

#endif
