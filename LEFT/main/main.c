/* NIMRODCORE MAKCU LEFT v0.7: USB2 CH343 UART0 to PC, UART1 from RIGHT.
 * 4MB ESP32-S3 DIO. USB1 is intentionally not a virtual USB controller.
 * This is experimental; hardware validation is required before flashing. */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/uart.h"
#include "esp_timer.h"
#include "esp_err.h"
#include "nc_protocol.h"

#define COM UART_NUM_0
#define IPC UART_NUM_1
static SemaphoreHandle_t txlock;
static volatile bool enabled=false;
static volatile uint32_t keepalive=0,good=0,invalid=0,seqgaps=0,last_packet=0;
static uint16_t prevseq;
static bool have_seq;
static uint8_t devices;
static uint32_t right_drops;

static void send_com(const void *data,size_t len) {
    if(xSemaphoreTake(txlock,pdMS_TO_TICKS(20))==pdTRUE) {
        uart_write_bytes(COM,data,len);
        xSemaphoreGive(txlock);
    }
}
static void reply(const char *s){send_com(s,strlen(s));}
static void command(const char *s) {
    if(!strcmp(s,"NC:HELLO"))reply("NC:READY protocol=1 role=LEFT baud=4000000 ipc=5000000\r\n");
    else if(!strcmp(s,"NC:START")){keepalive=(uint32_t)esp_timer_get_time();enabled=true;reply("NC:STARTED\r\n");}
    else if(!strcmp(s,"NC:KEEPALIVE"))keepalive=(uint32_t)esp_timer_get_time();
    else if(!strcmp(s,"NC:STOP")){enabled=false;reply("NC:STOPPED\r\n");}
    else if(!strcmp(s,"NC:STATUS")) {
        char line[180];
        snprintf(line,sizeof(line),"NC:STATUS stream=%d frames=%lu bad=%lu seqgap=%lu link_ms=%lu hid=%u txdrop=%lu\r\n",
          enabled,(unsigned long)good,(unsigned long)invalid,(unsigned long)seqgaps,
          (unsigned long)(((uint32_t)esp_timer_get_time()-last_packet)/1000),devices,(unsigned long)right_drops);
        reply(line);
    } else reply("NC:ERROR unknown-command\r\n");
}
static void command_task(void *arg) {
    (void)arg;char cmd[96];size_t len=0;uint8_t data[128];
    while(1){
        int n=uart_read_bytes(COM,data,sizeof(data),pdMS_TO_TICKS(10));
        for(int i=0;i<n;i++){
            uint8_t c=data[i];
            if(c=='\r'||c=='\n'){if(len){cmd[len]=0;command(cmd);len=0;}}
            else if(c>=32&&c<127){if(len<sizeof(cmd)-1)cmd[len++]=c;else len=0;}
            else len=0;
        }
    }
}
static void bridge_task(void *arg){
    (void)arg;
    uint8_t ring[1024],chunk[256];size_t nbuf=0;
    nc_packet pkt;
    while(1){
        if(enabled&&(uint32_t)((uint32_t)esp_timer_get_time()-keepalive)>3000000u)enabled=false;
        int n=uart_read_bytes(IPC,chunk,sizeof(chunk),pdMS_TO_TICKS(5));
        for(int i=0;i<n;i++){
            if(nbuf==sizeof(ring)){memmove(ring,ring+1,--nbuf);invalid++;}
            ring[nbuf++]=chunk[i];
        }
        size_t off=0;
        while(off<nbuf){
            size_t consumed=0;
            int rc=nc_decode(ring+off,nbuf-off,&pkt,&consumed);
            if(rc==0)break;
            if(rc<0){invalid++;off+=consumed?consumed:1;continue;}
            if(have_seq&&(uint16_t)(pkt.seq-prevseq)!=1)seqgaps++;
            prevseq=pkt.seq;have_seq=true;good++;last_packet=(uint32_t)esp_timer_get_time();
            if(pkt.type==NC_STATUS&&pkt.len>=5){
                devices=pkt.data[0];
                right_drops=(uint32_t)pkt.data[1]|((uint32_t)pkt.data[2]<<8)|((uint32_t)pkt.data[3]<<16)|((uint32_t)pkt.data[4]<<24);
            }
            if(enabled)send_com(ring+off,consumed);
            off+=consumed;
        }
        if(off){memmove(ring,ring+off,nbuf-off);nbuf-=off;}
    }
}
static void uart_init(uart_port_t id,int baud,int tx,int rx){
    const uart_config_t cfg={.baud_rate=baud,.data_bits=UART_DATA_8_BITS,.parity=UART_PARITY_DISABLE,
       .stop_bits=UART_STOP_BITS_1,.flow_ctrl=UART_HW_FLOWCTRL_DISABLE,.source_clk=UART_SCLK_DEFAULT};
    ESP_ERROR_CHECK(uart_param_config(id,&cfg));
    ESP_ERROR_CHECK(uart_set_pin(id,tx,rx,UART_PIN_NO_CHANGE,UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(id,8192,8192,0,NULL,0));
}
void app_main(void){
    txlock=xSemaphoreCreateMutex();configASSERT(txlock);
    uart_init(COM,4000000,43,44);
    uart_init(IPC,5000000,2,1);
    xTaskCreatePinnedToCore(command_task,"nc_cmd",4096,NULL,6,NULL,0);
    xTaskCreatePinnedToCore(bridge_task,"nc_ipc",8192,NULL,9,NULL,1);
}
