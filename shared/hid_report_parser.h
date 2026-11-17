#ifndef NC_HID_REPORT_PARSER_H
#define NC_HID_REPORT_PARSER_H
/* Descriptor-driven HID Input parser for standard relative mice and HID keyboards.
 * Supports report IDs, 8/16-bit relative axes, 8 buttons, boot key arrays and
 * NKRO bitmaps. Unknown/vendor reports are ignored rather than guessed.
 * Standalone C11, no ESP-IDF dependencies, tested on host before firmware build.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

#define NC_HID_MAX_REPORTS 8
#define NC_HID_MAX_KEY_RANGES 16
#define NC_HID_MAX_ARRAYS 4
#define NC_HID_NO_BIT 0xffffu

typedef struct {uint16_t bit; uint8_t width; bool valid;} nc_hid_field;
typedef struct {uint16_t bit; uint8_t usage; uint8_t count;} nc_hid_key_range;
typedef struct {uint16_t bit; uint8_t count; uint8_t width;} nc_hid_key_array;
typedef struct {
 uint8_t id;
 bool used;
 nc_hid_field x,y,wheel,buttons[8];
 nc_hid_key_range key_ranges[NC_HID_MAX_KEY_RANGES];
 uint8_t key_range_count;
 nc_hid_key_array key_arrays[NC_HID_MAX_ARRAYS];
 uint8_t key_array_count;
 bool mouse,keyboard;
} nc_hid_report;
typedef struct {
 bool uses_ids;
 uint8_t count;
 nc_hid_report reports[NC_HID_MAX_REPORTS];
} nc_hid_layout;
typedef struct {
 bool mouse,keyboard;
 uint8_t report_id;
 uint8_t buttons,mods;
 int16_t dx,dy;
 int8_t wheel;
 uint8_t keys[32]; /* 256 HID keyboard usages as bits */
} nc_hid_input;

static inline uint32_t nc_hid_bits(const uint8_t *p,size_t n,uint32_t bit,uint32_t width){
 uint32_t v=0;
 if(!p||!width||width>32||bit>n*8||width>n*8-bit)return 0;
 for(uint32_t i=0;i<width;i++)v|=(uint32_t)((p[(bit+i)/8]>>((bit+i)%8))&1u)<<i;
 return v;
}
static inline int32_t nc_hid_signed(uint32_t v,unsigned width){
 if(!width||width>32)return 0;
 if(width==32)return (int32_t)v;
 uint32_t sign=1u<<(width-1);
 return (int32_t)((v^sign)-sign);
}
static inline int8_t nc_hid_wheel(int32_t value){
 if(value>127)value=127;
 if(value< -128)value= -128;
 return (int8_t)value;
}
static inline nc_hid_report *nc_hid_slot(nc_hid_layout *l,uint8_t id){
 for(unsigned i=0;i<l->count;i++)if(l->reports[i].id==id)return &l->reports[i];
 if(l->count>=NC_HID_MAX_REPORTS)return NULL;
 nc_hid_report *r=&l->reports[l->count++];r->id=id;r->used=true;return r;
}
static inline bool nc_hid_add_key(nc_hid_report *r,uint16_t bit,uint8_t usage){
 if(r->key_range_count){
  nc_hid_key_range *prev=&r->key_ranges[r->key_range_count-1];
  if(prev->count<255 && (unsigned)prev->usage+prev->count==usage &&
     (unsigned)prev->bit+prev->count==bit){prev->count++;return true;}
 }
 if(r->key_range_count>=NC_HID_MAX_KEY_RANGES)return false;
 r->key_ranges[r->key_range_count++]=(nc_hid_key_range){bit,usage,1};return true;
}
static inline bool nc_parse_hid_descriptor(const uint8_t *d,size_t n,nc_hid_layout *out){
 if(!d||!out||!n||n>4096)return false;
 memset(out,0,sizeof(*out));
 uint32_t page=0,sz=0,count=0,id=0,usage=0,umin=0,umax=0;
 uint32_t offsets[256]={0};
 uint32_t local[32]={0};unsigned nlocal=0;
 bool has_min=false,has_max=false;
 typedef struct {uint32_t page,sz,count,id;} globals;
 globals stack[8];unsigned depth=0;
 uint8_t apps[16]={0};unsigned app_depth=0;
 bool good=false;
 for(size_t i=0;i<n;){
  uint8_t p=d[i++];
  if(p==0xfe){if(i+2>n)return false;size_t len=d[i];i+=2;if(len>n-i)return false;i+=len;continue;}
  unsigned len=p&3u;if(len==3)len=4;
  if(len>n-i)return false;
  uint32_t v=0;for(unsigned j=0;j<len;j++)v|=(uint32_t)d[i+j]<<(j*8);i+=len;
  unsigned type=(p>>2)&3u,tag=(p>>4)&15u;
  if(type==1){
   switch(tag){
    case 0:page=v;break;
    case 7:sz=v;break;
    case 8:if(!v||v>255)return false;id=v;out->uses_ids=true;break;
    case 9:count=v;break;
    case 10:if(depth>=8)return false;stack[depth++]=(globals){page,sz,count,id};break;
    case 11:if(!depth)return false;depth--;page=stack[depth].page;sz=stack[depth].sz;count=stack[depth].count;id=stack[depth].id;break;
   }
  }else if(type==2){
   if(tag==0){usage=v;if(nlocal<32)local[nlocal++]=v;}
   else if(tag==1){umin=v;has_min=true;}
   else if(tag==2){umax=v;has_max=true;}
  }else if(type==0){
   if(tag==10){
    if(app_depth>=16)return false;
    uint8_t app=app_depth?apps[app_depth-1]:0;
    if(v==1){uint32_t u=nlocal?local[0]:usage;
     app=(page==1&&u==2)?1:(page==1&&u==6)?2:0;
    }
    apps[app_depth++]=app;
   }else if(tag==12){if(!app_depth)return false;app_depth--;}
   else if(tag==8){
    if(!sz||sz>32||count>512||sz*count>8192||offsets[id]>65535-sz*count)return false;
    uint32_t bit=offsets[id];offsets[id]+=sz*count;
    bool variable=(v&2u)!=0,constant=(v&1u)!=0,relative=(v&4u)!=0;
    uint8_t app=app_depth?apps[app_depth-1]:0;
    if(!constant&&(app==1||app==2)){
     nc_hid_report *r=nc_hid_slot(out,(uint8_t)id);if(!r)return false;
     if(app==1&&variable){
      for(uint32_t k=0;k<count;k++){
       uint32_t u=k<nlocal?local[k]:(has_min&&has_max&&umin+k<=umax?umin+k:usage);
       nc_hid_field f={(uint16_t)(bit+k*sz),(uint8_t)sz,true};
       if(page==1&&relative&&sz<=16){
        if(u==0x30&&!r->x.valid)r->x=f;
        if(u==0x31&&!r->y.valid)r->y=f;
        if(u==0x38&&!r->wheel.valid)r->wheel=f;
       }else if(page==9&&sz==1&&u>=1&&u<=8)r->buttons[u-1]=f;
      }
     }else if(app==2&&page==7){
      if(variable&&sz==1){
       for(uint32_t k=0;k<count;k++){
        uint32_t u=k<nlocal?local[k]:(has_min&&has_max&&umin+k<=umax?umin+k:usage);
        if(u>=4&&u<=255){
         if(!nc_hid_add_key(r,(uint16_t)(bit+k),(uint8_t)u))return false;
        }else if(u>=224&&u<=231){
         /* Modifiers are represented by the same key ranges. */
         if(!nc_hid_add_key(r,(uint16_t)(bit+k),(uint8_t)u))return false;
        }
       }
      }else if(!variable&&sz==8&&count<=32){
       if(r->key_array_count>=NC_HID_MAX_ARRAYS)return false;
       r->key_arrays[r->key_array_count++]=(nc_hid_key_array){(uint16_t)bit,(uint8_t)count,8};
      }
     }
     r->mouse=r->x.valid&&r->y.valid;
     r->keyboard=r->key_range_count>0||r->key_array_count>0;
     if(r->mouse||r->keyboard)good=true;
    }
   }
   /* All main items clear local usages. */
   nlocal=0;usage=umin=umax=0;has_min=has_max=false;
  }
 }
 if(depth||app_depth)return false;
 return good;
}
static inline bool nc_decode_hid_input(const nc_hid_layout *layout,const uint8_t *raw,size_t n,nc_hid_input *out){
 if(!layout||!raw||!out||!n)return false;
 uint8_t id=layout->uses_ids?raw[0]:0;
 if(layout->uses_ids){raw++;n--;}
 const nc_hid_report *r=NULL;
 for(unsigned i=0;i<layout->count;i++)if(layout->reports[i].id==id){r=&layout->reports[i];break;}
 if(!r)return false;
 nc_hid_input temp={0};temp.report_id=id;
 if(r->mouse){
  if((size_t)r->x.bit+r->x.width>n*8||(size_t)r->y.bit+r->y.width>n*8)return false;
  temp.mouse=true;
  temp.dx=(int16_t)nc_hid_signed(nc_hid_bits(raw,n,r->x.bit,r->x.width),r->x.width);
  temp.dy=(int16_t)nc_hid_signed(nc_hid_bits(raw,n,r->y.bit,r->y.width),r->y.width);
  if(r->wheel.valid){if((size_t)r->wheel.bit+r->wheel.width>n*8)return false;
   temp.wheel=nc_hid_wheel(nc_hid_signed(nc_hid_bits(raw,n,r->wheel.bit,r->wheel.width),r->wheel.width));}
  for(unsigned k=0;k<8;k++)if(r->buttons[k].valid){
   if((size_t)r->buttons[k].bit+r->buttons[k].width>n*8)return false;
   if(nc_hid_bits(raw,n,r->buttons[k].bit,r->buttons[k].width))temp.buttons|=(uint8_t)(1u<<k);
  }
 }
 if(r->keyboard){
  temp.keyboard=true;
  for(unsigned k=0;k<r->key_range_count;k++){
   nc_hid_key_range a=r->key_ranges[k];
   if((size_t)a.bit+a.count>n*8)return false;
   for(unsigned j=0;j<a.count;j++)if(nc_hid_bits(raw,n,a.bit+j,1)){
    unsigned u=a.usage+j;
    if(u>=224&&u<=231)temp.mods|=(uint8_t)(1u<<(u-224));
    else temp.keys[u/8]|=(uint8_t)(1u<<(u%8));
   }
  }
  for(unsigned k=0;k<r->key_array_count;k++){
   nc_hid_key_array a=r->key_arrays[k];
   if((size_t)a.bit+(size_t)a.count*a.width>n*8)return false;
   for(unsigned j=0;j<a.count;j++){
    unsigned u=nc_hid_bits(raw,n,a.bit+j*a.width,a.width);
    if(u>=1&&u<=3)return false; /* HID ErrorRollOver: preserve old snapshot */
    if(u>=224&&u<=231)temp.mods|=(uint8_t)(1u<<(u-224));
    else if(u>=4&&u<=255)temp.keys[u/8]|=(uint8_t)(1u<<(u%8));
   }
  }
 }
 if(!temp.mouse&&!temp.keyboard)return false;
 *out=temp;return true;
}
#endif
