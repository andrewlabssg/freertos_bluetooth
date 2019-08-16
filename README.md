# freertos_bluetooth
FreeRTOS support for Silicon Labs EFR32BG/MG Bluetooth Stack.

For further information, refer to:
https://dev.andrewlabs.com/2019/06/freertos-support-for-silicon-labs.html

## Usage

The following describe the steps required to use the Bluetooth Stack in FreeRTOS starting from the Silicon Labs soc-empty template project:

1. Ensure you have FreeRTOS and the Bluetooth SDK properly functioning on your device.
2. Remove references to native_gecko.h from your project.
3. Include freertos_bluetooth.c and freertos_bluetooth.h in your project.
4. Make sure the Bluetooth stack config_flags configuration has the GECKO_CONFIG_FLAG_RTOS flag set.
```C
static gecko_configuration_t config = {
    .config_flags = GECKO_CONFIG_FLAG_RTOS,
    // ...
};
```
5. Configure interrupts in your MCU initialization function, since specifying the GECKO_CONFIG_FLAG_RTOS flag turns off interrupt configuration in the Bluetooth stack. Other interrupts should be configured as required.
```C
// Radio interrupts.
NVIC_SetPriority(FRC_PRI_IRQn, 1);
NVIC_SetPriority(FRC_IRQn, 1);
NVIC_SetPriority(MODEM_IRQn, 1);
NVIC_SetPriority(RAC_SEQ_IRQn, 1);
NVIC_SetPriority(RAC_RSM_IRQn, 1);
NVIC_SetPriority(BUFC_IRQn, 1);
NVIC_SetPriority(AGC_IRQn, 1);
NVIC_SetPriority(PROTIMER_IRQn, 1);
NVIC_SetPriority(RTCC_IRQn, 4);      // Required for EFR32BG1 and EFR32BG12 only.
```
6. Start the Bluetooth link-layer and stack tasks in your main function or equivalent. BLUETOOTH_LL_PRIORITY must be the highest priority task in your system. BLUETOOTH_STACK_PRIORITY should be set lower than the link-layer, but higher than your application task.
```C
bluetooth_start(BLUETOOTH_LL_PRIORITY, BLUETOOTH_STACK_PRIORITY, NULL);
app_bluetooth_start_task();     // Start your app's Bluetooth task.
vTaskStartScheduler();          // Start the FreeRTOS scheduler.
```
7. In your application's Bluetooth task, wait for the BLUETOOTH_EVENT_FLAG_EVT_WAITING flag in the bluetooth_event_flags event group, indicating an event from the stack is waiting to be handled by the user application.
```C
for (;;)
{
    xEventGroupWaitBits(bluetooth_event_flags, BLUETOOTH_EVENT_FLAG_EVT_WAITING, pdTRUE, pdTRUE, portMAX_DELAY);
    // ...
```
8. Handle the event appropriately by examining bluetooth_evt:
```C
    switch (BGLIB_MSG_ID(bluetooth_evt->header))
    {
        case gecko_evt_system_boot_id:
    // ...
```
9. After handling the event, flag it as handled by setting BLUETOOTH_EVENT_FLAG_EVT_HANDLED in the bluetooth_event_flags event group:
```C
    // ...
        default:
            break;
    }
 
    xEventGroupSetBits(bluetooth_event_flags, BLUETOOTH_EVENT_FLAG_EVT_HANDLED);
}
```

## Energy Modes

The Bluetooth stack sets a block on the EM3 energy mode if SLEEP_FLAGS_DEEP_SLEEP_ENABLE is specified in the configuration struct, allowing the SoC to power down to the EM2 energy mode. Otherwise, it sets a block on the EM2 energy mode, only allowing EM1 to be entered. The sleep driver must be manually initialized when using an RTOS.

Note that as the HFXO clock is not available in EM2, the SysTick timer will not operate. FreeRTOS will need to be configured for tickless idle mode.
