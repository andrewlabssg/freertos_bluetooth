/*
 * Copyright (C) 2019 AndrewLabs.
 *
 * This file is a derivative work of Silicon Labs open source code subject to the
 * terms found at www.silabs.com/about-us/legal/master-software-license-agreement,
 * and Copyright 2018 Silicon Laboratories Inc.
 */

#include "freertos_bluetooth.h"

#include "em_core.h"

#include "semphr.h"

// Constants.
// ==========

#define BLUETOOTH_LL_STACK_SIZE         (1024/sizeof(StackType_t))
#define BLUETOOTH_STACK_STACK_SIZE      (1536/sizeof(StackType_t))

#define BLUETOOTH_TO_RTOS_TICK          (32768/configTICK_RATE_HZ)

// Variables.
// ==========

EventGroupHandle_t bluetooth_event_flags;
volatile struct gecko_cmd_packet* bluetooth_evt;

static SemaphoreHandle_t l_bluetoothMutex;

static volatile uint32_t l_bluetoothCmdHeader;
static volatile gecko_cmd_handler l_bluetoothCmdHandler;
static volatile const void* l_bluetoothCmdPayload;

static volatile wakeupCallback l_bluetoothWakeupCallback = NULL;

// Functions.
// ==========

/**
 * The Bluetooth link-layer task.
 * @param pvParameters
 */
static void freertos_bluetoothLLTask(void *pvParameters)
{
    for (;;)
    {
        xEventGroupWaitBits(bluetooth_event_flags, BLUETOOTH_EVENT_FLAG_LL, pdTRUE, pdFALSE, portMAX_DELAY);
        gecko_priority_handle();
    }
}

/**
 * The Bluetooth stack task.
 * @param pvParameters
 */
static void freertos_bluetoothStackTask(void *pvParameters)
{
    EventBits_t flags = BLUETOOTH_EVENT_FLAG_EVT_HANDLED | BLUETOOTH_EVENT_FLAG_STACK;

    for (;;)
    {
        // BGAPI command from user application to be sent to BLE stack.
        if (flags & BLUETOOTH_EVENT_FLAG_CMD_WAITING)
        {
            uint32_t header = l_bluetoothCmdHeader;
            gecko_cmd_handler cmd_handler = l_bluetoothCmdHandler;
            sli_bt_cmd_handler_delegate(header, cmd_handler, (void*)l_bluetoothCmdPayload);
            l_bluetoothCmdHandler = NULL;
            flags &= ~BLUETOOTH_EVENT_FLAG_CMD_WAITING;
            xEventGroupSetBits(bluetooth_event_flags, BLUETOOTH_EVENT_FLAG_RSP_WAITING); // Flag command processing complete.
        }

        // Link-layer has update for BLE stack, and bluetooth_evt is not in use by user application.
        if ((flags & BLUETOOTH_EVENT_FLAG_STACK) && (flags & BLUETOOTH_EVENT_FLAG_EVT_HANDLED))
        {
            if ((bluetooth_evt = gecko_peek_event()) != NULL)
            {
                // Inform user application that bluetooth_evt requires handling.
                xEventGroupSetBits(bluetooth_event_flags, BLUETOOTH_EVENT_FLAG_EVT_WAITING);
                flags &= ~BLUETOOTH_EVENT_FLAG_EVT_HANDLED;
                if (l_bluetoothWakeupCallback != NULL) l_bluetoothWakeupCallback();     // Callback if required.
            }
            else
            {   // No event to handle, clear flag.
                flags &= ~BLUETOOTH_EVENT_FLAG_STACK;
            }
        }

        // Determine duration stack can sleep.
        TickType_t ticks = gecko_can_sleep_ticks();
        if (ticks == 0) // Stack cannot sleep, has events to handle.
        {
            if (flags & BLUETOOTH_EVENT_FLAG_EVT_HANDLED)   // bluetooth_evt is available.
            {
                flags |= BLUETOOTH_EVENT_FLAG_STACK;        // Continue to handle stack events.
                continue;
            }
            else
            {
                ticks = portMAX_DELAY;  // Suspend while waiting for bluetooth_evt to be handled.
            }
        }
        else if (ticks != UINT32_MAX)   // Sleep for a specified duration.
        {
            // Convert to RTOS ticks.
            ticks = (ticks + BLUETOOTH_TO_RTOS_TICK - 1) / BLUETOOTH_TO_RTOS_TICK;
        }

        EventBits_t bits = xEventGroupWaitBits(
                bluetooth_event_flags,
                BLUETOOTH_EVENT_FLAG_STACK | BLUETOOTH_EVENT_FLAG_EVT_HANDLED | BLUETOOTH_EVENT_FLAG_CMD_WAITING,
                pdTRUE,
                pdFALSE,
                ticks);

        // Events were received.
        if (bits & (BLUETOOTH_EVENT_FLAG_STACK | BLUETOOTH_EVENT_FLAG_EVT_HANDLED | BLUETOOTH_EVENT_FLAG_CMD_WAITING))
        {
            flags |= bits;
        }
        // Timed-out waiting for bits.
        else
        {
            // Set the flag to update the Bluetooth stack.
            flags |= BLUETOOTH_EVENT_FLAG_STACK;
        }
    }
}

/**
 * Notifies the Bluetooth link-layer task of pending events.
 * Called by kernel-aware interrupt context and stack.
 */
void BluetoothLLCallback(void)
{
    if (CORE_InIrqContext())
    {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        BaseType_t xResult = xEventGroupSetBitsFromISR(bluetooth_event_flags, BLUETOOTH_EVENT_FLAG_LL,
                &xHigherPriorityTaskWoken);
        if (xResult != pdFAIL) portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
    else
    {
        xEventGroupSetBits(bluetooth_event_flags, BLUETOOTH_EVENT_FLAG_LL);
    }
}

/**
 * Notifies the Bluetooth stack task of pending events.
 */
void BluetoothUpdate(void)
{
    if (CORE_InIrqContext())
    {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        BaseType_t xResult = xEventGroupSetBitsFromISR(bluetooth_event_flags, BLUETOOTH_EVENT_FLAG_STACK,
                &xHigherPriorityTaskWoken);
        if (xResult != pdFAIL) portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
    else
    {
        xEventGroupSetBits(bluetooth_event_flags, BLUETOOTH_EVENT_FLAG_STACK);
    }
}

/**
 * Starts the Bluetooth tasks.
 * @param ll_priority
 * @param stack_priority
 * @param initialize_bluetooth_stack
 */
void bluetooth_start(UBaseType_t ll_priority, UBaseType_t stack_priority, bluetooth_stack_init_func initialize_bluetooth_stack)
{
    // Create the Bluetooth event group.
    bluetooth_event_flags = xEventGroupCreate();

    // Create the Bluetooth mutex for multiple task access.
    l_bluetoothMutex = xSemaphoreCreateMutex();

    if (initialize_bluetooth_stack != NULL) initialize_bluetooth_stack();

    // Create the Bluetooth link-layer task.
    TaskHandle_t bluetoothLLHandle;
    xTaskCreate(freertos_bluetoothLLTask, "BLE LL", BLUETOOTH_LL_STACK_SIZE, NULL, ll_priority, &bluetoothLLHandle);

    // Create the Bluetooth stack task.
    TaskHandle_t bluetoothStackHandle;
    xTaskCreate(freertos_bluetoothStackTask, "BLE Stack", BLUETOOTH_STACK_STACK_SIZE, NULL, stack_priority,
            &bluetoothStackHandle);
}

/**
 * Sets a user-defined callback function which is called when bluetooth_evt
 * is ready to be processed by the user application. Usually a semaphore is set
 * in the callback function.
 * @param cb
 */
void BluetoothSetWakeupCallback(wakeupCallback cb)
{
    l_bluetoothWakeupCallback = (volatile wakeupCallback)cb;
}

/**
 * Grabs the Bluetooth mutex.
 * A task can use the Bluetooth API after this.
 * @return
 */
BaseType_t BluetoothPend(void)
{
    return xSemaphoreTake(l_bluetoothMutex, portMAX_DELAY);
}

/**
 * Releases the Bluetooth mutex.
 * Other tasks are free to use the Bluetooth API after this.
 * @return
 */
BaseType_t BluetoothPost(void)
{
    return xSemaphoreGive(l_bluetoothMutex);
}

// BGAPI hooks.
// ============

void rtos_gecko_handle_command(uint32_t header, void* payload)
{
    sli_bt_cmd_handler_rtos_delegate(header, NULL, payload);
}

void rtos_gecko_handle_command_noresponse(uint32_t header, void* payload)
{
    sli_bt_cmd_handler_rtos_delegate(header, NULL, payload);
}

void sli_bt_cmd_handler_rtos_delegate(uint32_t header, gecko_cmd_handler handler, const void* payload)
{
    l_bluetoothCmdHeader = header;
    l_bluetoothCmdHandler = handler;
    l_bluetoothCmdPayload = payload;

    // Send command to BLE task for processing, and wait for completion.
    xEventGroupSync(bluetooth_event_flags,
            BLUETOOTH_EVENT_FLAG_CMD_WAITING,
            BLUETOOTH_EVENT_FLAG_RSP_WAITING,
            portMAX_DELAY);
}
