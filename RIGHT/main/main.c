/* NIMRODCORE MAKCU RIGHT v0.9: USB3 HID Host -> UART1 LEFT.
 * Descriptor-driven standard HID mouse and keyboard reports (boot, report-ID,
 * 16-bit motion, NKRO bitmap). No vendor-defined usages or USB high-speed.
 * Experimental: requires LEFT v0.9 and full 4MB ROM backups before flashing.
 */
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
#include "hid_report_parser.h"
#define IPC UART_NUM_1
#define IPC_BAUD 5000000
#define MAX_DEVICES 8
#define KEY_BYTES 32
#define KIND_MOUSE 1
#define KIND_KEYBOARD 2

typedef struct {
 hid_host_device_handle_t h;
 nc_hid_layout layout;
 uint8_t kind,buttons,mods,keys[KEY_BYTES];
 uint8_t report_buttons[NC_HID_MAX_REPORTS];
 uint8_t report_mods[NC_HID_MAX_REPORTS];
 uint8_t report_keys[NC_HID_MAX_REPORTS][KEY_BYTES];
 bool active,descriptor_mode;
} device;
typedef struct {hid_host_device_handle_t h;hid_host_driver_event_t type;} dev_event;
typedef struct {uint8_t type,len,data[NC_MAX_PAYLOAD];uint32_t time;} out_event;
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
 if(!data||len>NC_MAX_PAYLOAD)return;
 out_event e={.type=type,.len=len,.time=(uint32_t)esp_timer_get_time()};
 memcpy(e.data,data,len);
 if(xQueueSend(txqueue,&e,0)!=pdTRUE)dropped++;
}
static uint8_t union_buttons(void){
 uint8_t buttons=0;
 for(int i=0;i<MAX_DEVICES;i++)if(devices[i].active&&(devices[i].kind&KIND_MOUSE))buttons|=devices[i].buttons;
 return buttons;
}
static unsigned report_index(const device *d,uint8_t id){
 for(unsigned i=0;i<d->layout.count;i++)if(d->layout.reports[i].id==id)return i;
 return 0;
}
static void emit_mouse(device *d,uint8_t report_id,uint8_t buttons,int16_t dx,int16_t dy,int8_t wheel){
 uint8_t previous=union_buttons();
 if(d->descriptor_mode){
  d->report_buttons[report_index(d,report_id)]=buttons;
  buttons=0;
  for(unsigned i=0;i<d->layout.count;i++)buttons|=d->report_buttons[i];
 }
 d->buttons=buttons;
 uint8_t combined=union_buttons();
 if(!dx&&!dy&&!wheel&&combined==previous)return;
 uint8_t out[8]={combined,(uint8_t)dx,(uint8_t)(dx>>8),(uint8_t)dy,(uint8_t)(dy>>8),(uint8_t)wheel,0,0};
 emit(NC_MOUSE,out,sizeof(out));
}
static void emit_keyboard_union(void){
 uint8_t out[33]={0};
 for(int i=0;i<MAX_DEVICES;i++)if(devices[i].active&&(devices[i].kind&KIND_KEYBOARD)){
  out[0]|=devices[i].mods;
  for(int j=0;j<KEY_BYTES;j++)out[j+1]|=devices[i].keys[j];
 }
 emit(NC_KEYBOARD_NKRO,out,sizeof(out));
}
static void update_keyboard(device *d,uint8_t report_id,uint8_t mods,const uint8_t keys[KEY_BYTES]){
 if(d->descriptor_mode){
  unsigned index=report_index(d,report_id);
  d->report_mods[index]=mods;
  memcpy(d->report_keys[index],keys,KEY_BYTES);
  mods=0;uint8_t combined[KEY_BYTES]={0};
  for(unsigned i=0;i<d->layout.count;i++){
   mods|=d->report_mods[i];
   for(unsigned j=0;j<KEY_BYTES;j++)combined[j]|=d->report_keys[i][j];
  }
  if(d->mods==mods&&!memcmp(d->keys,combined,KEY_BYTES))return;
  d->mods=mods;memcpy(d->keys,combined,KEY_BYTES);
 }else{
  if(d->mods==mods&&!memcmp(d->keys,keys,KEY_BYTES))return;
  d->mods=mods;memcpy(d->keys,keys,KEY_BYTES);
 }
 emit_keyboard_union();
}
static void boot_mouse(device *d,const uint8_t *p,size_t n){
 if(n<3)return;
 emit_mouse(d,0,p[0],(int8_t)p[1],(int8_t)p[2],n>=4?(int8_t)p[3]:0);
}
static void boot_keyboard(device *d,const uint8_t *p,size_t n){
 if(n<8)return;
 uint8_t keys[KEY_BYTES]={0};
 for(int i=2;i<8;i++){
  uint8_t u=p[i];
  if(u>=1&&u<=3)return; /* ErrorRollOver: retain prior key state */
  if(u>=4)keys[u/8]|=(uint8_t)(1u<<(u%8));
 }
 update_keyboard(d,0,p[0],keys);
}
static void iface_cb(hid_host_device_handle_t h,hid_host_interface_event_t event,void *arg){
 (void)arg;device *d=lookup(h);if(!d)return;
 if(event==HID_HOST_INTERFACE_EVENT_INPUT_REPORT){
  uint8_t data[512];size_t n=0;
  if(hid_host_device_get_raw_input_report_data(h,data,sizeof(data),&n)!=ESP_OK)return;
  if(d->descriptor_mode){
   nc_hid_input in;
   if(!nc_decode_hid_input(&d->layout,data,n,&in))return;
   if(in.mouse&&(d->kind&KIND_MOUSE))emit_mouse(d,in.report_id,in.buttons,in.dx,in.dy,in.wheel);
   if(in.keyboard&&(d->kind&KIND_KEYBOARD))update_keyboard(d,in.report_id,in.mods,in.keys);
  }else{
   if(d->kind&KIND_MOUSE)boot_mouse(d,data,n);
   if(d->kind&KIND_KEYBOARD)boot_keyboard(d,data,n);
  }
 }else if(event==HID_HOST_INTERFACE_EVENT_DISCONNECTED){
  bool mouse=(d->kind&KIND_MOUSE)!=0,keyboard=(d->kind&KIND_KEYBOARD)!=0;
  uint8_t prev=union_buttons();
  hid_host_device_close(h);
  memset(d,0,sizeof(*d));
  if(mouse&&prev!=union_buttons()){
   uint8_t zero[8]={0};zero[0]=union_buttons();emit(NC_MOUSE,zero,8);
  }
  if(keyboard)emit_keyboard_union();
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
  device *d=allocate(e.h);if(!d)continue;
  const hid_host_device_config_t cfg={.callback=iface_cb,.callback_arg=NULL};
  if(hid_host_device_open(e.h,&cfg)!=ESP_OK){memset(d,0,sizeof(*d));continue;}
  size_t desc_len=0;
  uint8_t *desc=hid_host_get_report_descriptor(e.h,&desc_len);
  if(desc&&nc_parse_hid_descriptor(desc,desc_len,&d->layout)){
   for(unsigned i=0;i<d->layout.count;i++){
    if(d->layout.reports[i].mouse)d->kind|=KIND_MOUSE;
    if(d->layout.reports[i].keyboard)d->kind|=KIND_KEYBOARD;
   }
   d->descriptor_mode=(d->kind!=0);
  }
  if(!d->descriptor_mode){
   /* Strict fallback: only boot interfaces, never interpret generic reports
    * as boot bytes. */
   if(p.sub_class==HID_SUBCLASS_BOOT_INTERFACE){
    if(p.proto==HID_PROTOCOL_MOUSE)d->kind=KIND_MOUSE;
    else if(p.proto==HID_PROTOCOL_KEYBOARD)d->kind=KIND_KEYBOARD;
    if(d->kind) (void)hid_class_request_set_protocol(e.h,HID_REPORT_PROTOCOL_BOOT);
   }
  }
  if(!d->kind){hid_host_device_close(e.h);memset(d,0,sizeof(*d));continue;}
  if(d->kind&KIND_KEYBOARD)(void)hid_class_request_set_idle(e.h,0,0);
  if(hid_host_device_start(e.h)!=ESP_OK){hid_host_device_close(e.h);memset(d,0,sizeof(*d));}
 }
}
static void tx_task(void *arg){
 (void)arg;out_event e;uint8_t frame[NC_MAX_FRAME];uint32_t last=0;
 while(1){
  if(xQueueReceive(txqueue,&e,pdMS_TO_TICKS(20))==pdTRUE){
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
 xTaskCreatePinnedToCore(dev_task,"nc_hid",12288,NULL,4,NULL,0);
 const hid_host_driver_config_t hidcfg={.create_background_task=true,.task_priority=6,
  .stack_size=6144,.core_id=0,.callback=driver_cb,.callback_arg=NULL};
 ESP_ERROR_CHECK(hid_host_install(&hidcfg));
}
