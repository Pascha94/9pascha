#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Wire format: 'N','C',v1,type,length,seq LE16,time_us LE32,payload,crc LE16.
   CRC16-CCITT over v1..payload. Mouse: buttons u8, dx/dy i16 LE, wheel/pan/tilt i8.
   Keyboard: modifiers u8, 14 HID usage bytes. */
#define NC_MOUSE 1
#define NC_KEYBOARD 2
#define NC_STATUS 3
#define NC_MAX_PAYLOAD 64
#define NC_MAX_FRAME 77
typedef struct { uint8_t type, len; uint16_t seq; uint32_t time_us; uint8_t data[64]; } nc_packet;
static inline uint16_t nc_crc(const uint8_t *p, size_t n) {
    uint16_t c=0xffff;
    for(size_t i=0;i<n;i++){c^=(uint16_t)p[i]<<8;for(int j=0;j<8;j++)c=(uint16_t)((c&0x8000)?(c<<1)^0x1021:c<<1);}
    return c;
}
static inline size_t nc_encode(uint8_t *out,size_t cap,uint8_t type,uint16_t seq,uint32_t time,const uint8_t *data,uint8_t len){
    if(!out||!data||len>NC_MAX_PAYLOAD||cap<(size_t)13+len)return 0;
    out[0]='N';out[1]='C';out[2]=1;out[3]=type;out[4]=len;
    out[5]=seq&255;out[6]=seq>>8;
    for(int i=0;i<4;i++)out[7+i]=(uint8_t)(time>>(i*8));
    memcpy(out+11,data,len);
    uint16_t crc=nc_crc(out+2,9+len);
    out[11+len]=crc&255;out[12+len]=crc>>8;
    return 13+len;
}
static inline int nc_decode(const uint8_t *p,size_t n,nc_packet *out,size_t *used){
    *used=0;
    if(n<5)return 0;
    if(p[0]!='N'||p[1]!='C'||p[2]!=1||p[4]>NC_MAX_PAYLOAD||(p[3]!=1&&p[3]!=2&&p[3]!=3)){*used=1;return -1;}
    size_t total=13+p[4];if(n<total)return 0;
    uint16_t actual=nc_crc(p+2,9+p[4]),want=p[11+p[4]]|((uint16_t)p[12+p[4]]<<8);
    if(actual!=want){*used=1;return -1;}
    out->type=p[3];out->len=p[4];out->seq=p[5]|((uint16_t)p[6]<<8);
    out->time_us=p[7]|((uint32_t)p[8]<<8)|((uint32_t)p[9]<<16)|((uint32_t)p[10]<<24);
    memcpy(out->data,p+11,p[4]);*used=total;return 1;
}
