/* NIMRODCORE MAKCU RIGHT v0.7 - USB3 HID Host -> UART1 LEFT.
 * EXPERIMENTAL: boot-protocol mice/keyboards only; advanced report-ID gaming
 * mice and composite HID devices need a descriptor parser before production.
 * Requires 4MB flash, DIO, matching LEFT firmware. */
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/uart.h"
#include "esp_timer.h"
#include "esp_intr_alloc.h"
#include "esp_err.h"
#include "usb/usb_host.h"
#include "usb/hid_host.h"
#include "nc_protocol.h"
#define IPC UART_NUM_1
#define IPC_BAUD 5000000
#define MAX_DEVICES 8
typedef struct {hid_host_device_handle_t h;uint8_t kind,buttons,mods,keys[14];bool active;} device;
typedef struct {hid_host_device_handle_t h;hid_host_driver_event_t type;} dev_event;
typedef struct {uint8_t type,len,data[15];uint32_t time;} out_event;
static device devices[MAX_DEVICES];
static QueueHandle_t devqueue,txqueue;
static uint16_t seq;
static volatile uint32_t dropped;
static device *lookup(hid_host_device_handle_t h){
    for(int i=0;i<MAX_DEVICES;i++)if(devices[i].active&&devices[i].h==h)return &devices[i];
    return NULL;
}
static device *allocate(hid_host_device_handle_t h){
    for(int i=0;i<MAX_DEVICES;i++)if(!devices[i].active){
        device *d=&devices[i];memset(d,0,sizeof(*d));d->active=true;d->h=h;return d;
    }
    return NULL;
}
static void emit(uint8_t type,const uint8_t *data,uint8_t len){
    out_event e={.type=type,.len=len,.time=(uint32_t)esp_timer_get_time()};
    memcpy(e.data,data,len);
    if(xQueueSend(txqueue,&e,0)!=pdTRUE)dropped++;
}
static void mouse_report(device *d,const uint8_t *p,size_t n){
    if(n<3)return;
    /* Boot protocol: buttons, dx i8, dy i8, optional wheel i8. */
    uint8_t buttons=p[0],wheel=n>=4?p[3]:0;
    if(!p[1]&&!p[2]&&!wheel&&buttons==d->buttons)return;
    d->buttons=buttons;
    uint8_t out[8]={buttons,p[1],(p[1]&0x80)?255:0,p[2],(p[2]&0x80)?255:0,wheel,0,0};
    emit(NC_MOUSE,out,sizeof(out));
}
static void keyboard_report(device *d,const uint8_t *p,size_t n){
    if(n<8)return;
    uint8_t keys[14]={0};
    memcpy(keys,p+2,6);
    if(p[0]==d->mods&&!memcmp(keys,d->keys,sizeof(keys)))return;
    d->mods=p[0];memcpy(d->keys,keys,sizeof(keys));
    uint8_t out[15]={0};out[0]=d->mods;memcpy(out+1,keys,14);
    emit(NC_KEYBOARD,out,sizeof(out));
}
static void iface_cb(hid_host_device_handle_t h,hid_host_interface_event_t event,void *arg){
    (void)arg;device *d=lookup(h);if(!d)return;
    if(event==HID_HOST_INTERFACE_EVENT_INPUT_REPORT){
        uint8_t data[512];size_t n=0;
        if(hid_host_device_get_raw_input_report_data(h,data,sizeof(data),&n)==ESP_OK){
            if(d->kind==1)mouse_report(d,data,n);
            else if(d->kind==2)keyboard_report(d,data,n);
        }
    } else if(event==HID_HOST_INTERFACE_EVENT_DISCONNECTED){
        if(d->kind==1){uint8_t zero[8]={0};emit(NC_MOUSE,zero,8);}
        else if(d->kind==2){uint8_t zero[15]={0};emit(NC_KEYBOARD,zero,15);}
        hid_host_device_close(h);memset(d,0,sizeof(*d));
    }
}
static void driver_cb(hid_host_device_handle_t h,hid_host_driver_event_t type,void *arg){
    (void)arg;dev_event e={.h=h,.type=type};
    if(xQueueSend(devqueue,&e,0)!=pdTRUE)dropped++;
}
static void usb_task(void *notify){
    const usb_host_config_t cfg={.skip_phy_setup=false,.intr_flags=ESP_INTR_FLAG_LOWMED};
    ESP_ERROR_CHECK(usb_host_install(&cfg));
    xTaskNotifyGive((TaskHandle_t)notify);
    while(1){uint32_t flags=0;usb_host_lib_handle_events(portMAX_DELAY,&flags);}
}
static void dev_task(void *arg){
    (void)arg;dev_event e;
    while(1){
        if(xQueueReceive(devqueue,&e,portMAX_DELAY)!=pdTRUE)continue;
        if(e.type!=HID_HOST_DRIVER_EVENT_CONNECTED)continue;
        hid_host_dev_params_t p;
        if(hid_host_device_get_params(e.h,&p)!=ESP_OK)continue;
        /* Explicitly support boot-interface mice and keyboards. Do not
           misinterpret arbitrary report-ID gaming-mouse packets as boot. */
        if(p.sub_class!=HID_SUBCLASS_BOOT_INTERFACE)continue;
        uint8_t kind=(p.proto==HID_PROTOCOL_MOUSE)?1:(p.proto==HID_PROTOCOL_KEYBOARD)?2:0;
        if(!kind)continue;
        device *d=allocate(e.h);if(!d)continue;
        d->kind=kind;
        const hid_host_device_config_t cfg={.callback=iface_cb,.callback_arg=NULL};
        if(hid_host_device_open(e.h,&cfg)!=ESP_OK){memset(d,0,sizeof(*d));continue;}
        (void)hid_class_request_set_protocol(e.h,HID_REPORT_PROTOCOL_BOOT);
        if(kind==2)(void)hid_class_request_set_idle(e.h,0,0);
        if(hid_host_device_start(e.h)!=ESP_OK){
            hid_host_device_close(e.h);memset(d,0,sizeof(*d));
        }
    }
}
static void tx_task(void *arg){
    (void)arg;out_event e;uint8_t frame[NC_MAX_FRAME];uint32_t last=0;
    while(1){
        if(xQueueReceive(txqueue,&e,pdMS_TO_TICKS(20))==pdTRUE){
            /* Don't delay input by sending old movement packets. */
            if(e.type==NC_MOUSE&&(uint32_t)((uint32_t)esp_timer_get_time()-e.time)>20000u){
                dropped++;continue;
            }
            size_t n=nc_encode(frame,sizeof(frame),e.type,seq++,e.time,e.data,e.len);
            if(n)uart_write_bytes(IPC,frame,n);
        }
        uint32_t now=(uint32_t)esp_timer_get_time();
        if((uint32_t)(now-last)>=1000000u){
            last=now;uint8_t connected=0;
            for(int i=0;i<MAX_DEVICES;i++)if(devices[i].active)connected++;
            uint8_t status[5]={connected,(uint8_t)dropped,(uint8_t)(dropped>>8),(uint8_t)(dropped>>16),(uint8_t)(dropped>>24)};
            size_t n=nc_encode(frame,sizeof(frame),NC_STATUS,seq++,now,status,5);
            if(n)uart_write_bytes(IPC,frame,n);
        }
    }
}
void app_main(void){
    devqueue=xQueueCreate(16,sizeof(dev_event));txqueue=xQueueCreate(128,sizeof(out_event));
    configASSERT(devqueue&&txqueue);
    const uart_config_t cfg={.baud_rate=IPC_BAUD,.data_bits=UART_DATA_8_BITS,.parity=UART_PARITY_DISABLE,
        .stop_bits=UART_STOP_BITS_1,.flow_ctrl=UART_HW_FLOWCTRL_DISABLE,.source_clk=UART_SCLK_DEFAULT};
    ESP_ERROR_CHECK(uart_param_config(IPC,&cfg));
    ESP_ERROR_CHECK(uart_set_pin(IPC,1,2,UART_PIN_NO_CHANGE,UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(IPC,4096,4096,0,NULL,0));
    xTaskCreatePinnedToCore(tx_task,"nc_tx",4096,NULL,8,NULL,1);
    xTaskCreatePinnedToCore(usb_task,"nc_usb",4096,xTaskGetCurrentTaskHandle(),3,NULL,0);
    configASSERT(ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(3000))!=0);
    xTaskCreatePinnedToCore(dev_task,"nc_hid",8192,NULL,4,NULL,0);
    const hid_host_driver_config_t hidcfg={.create_background_task=true,.task_priority=6,
        .stack_size=6144,.core_id=0,.callback=driver_cb,.callback_arg=NULL};
    ESP_ERROR_CHECK(hid_host_install(&hidcfg));
}
