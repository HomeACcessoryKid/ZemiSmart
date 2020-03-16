/*  (c) 2018-2020 HomeAccessoryKid
 *  This example makes an RGBW smart lightbulb as offered on e.g. alibaba
 *  with the brand of ZemiSmart. It uses an ESP8266 with a 1MB flash on a 
 *  TYLE1R printed circuit board by TuyaSmart (also used in AiLight).
 *  There are terminals with markings for GND, 3V3, Tx, Rx and IO0
 *  There's a second GND terminal that can be used to set IO0 for flashing
 *  Popping of the plastic cap is sometimes hard, but never destructive
 *  Note that the LED color is COLD white.
 *  Changing them for WARM is possible but requires skill and nerves.
 */


#include <stdio.h>
#include <espressif/esp_wifi.h>
#include <espressif/esp_sta.h>
//#include <espressif/esp_system.h> //for timestamp report only
#include <esp/uart.h>
#include <esp8266.h>
#include <FreeRTOS.h>
#include <task.h>

#include <homekit/homekit.h>
#include <homekit/characteristics.h>
//#include "wifi.h"

#include <math.h>  //requires LIBS ?= hal m to be added to Makefile
#include "mjpwm.h"
#include <udplogger.h>

#ifndef VERSION
 #error You must set VERSION=x.y.z to match github version tag x.y.z
#endif

#define  BEATTIME    50 //the granularity of calculating light transitions in ms
#define  MODES       11 //0-9 + 10

typedef struct _values {
    int hue;
    int sat;
    int bri;
    int transtime;
    int staytime;
} values;

values  array0[1]; //this array is loaded with the homekit values
//   hue sat bri      tt      st    times in ms
values  array1[] = {
    { 57, 23,100,    500,      0}, //0
    { 35, 50, 95,    600,      0}, //1
    { 42, 40, 90,    400,      0}, //2
    { 32, 50, 85,    500,      0}, //3
    { 45, 35, 95,    600,      0}, //4
    { 60, 20,100,    400,      0}, //5
    { 50, 34, 90,    500,      0}, //6
    { 42, 40, 80,    600,      0}, //7
    { 37, 50, 90,    400,      0}, //8
    { 30, 30, 95,    500,      0}  //9
};
values  array2[] = {
    { 45,100,100,   1000,   2000}, //0
    {135,100,100,   1000,   2000}, //1
    {225,100,100,   1000,   3000}, //2
    {315,100,100,   1000,   3000}  //3
};
values  array3[] = {
    { 45,100,100,   2000,   1000}, //0
    {135,100,100,   2000,   1000}, //1
    {225,100,100,   2000,   1000}, //2
    {315,100,100,   2000,   1000}, //3
    { 45,100,100,   2000,   1000}, //0
    {315,100,100,   2000,   1000}, //3
    {225,100,100,   2000,   1000}, //2
    {135,100,100,   2000,   1000}  //1
};
values  array4[1];
values  array5[1];
values  array6[1];
values  array7[1];
values  array8[1];
values  array9[] = {
    {  0,100,100,      0,   1000}, //0
    { 90,100,100,      0,   1000}, //1
    {180,100,100,      0,   1000}, //2
    {270,100,100,      0,   1000}  //3
};
values  array10[] = { //for identify routine, not accessible from modes selector
    {  0,100,100,      0,   300}, //0
    {120,100,100,      0,   300}, //1
    {240,100,100,      0,   300}  //2
};

values *array[MODES] = {array0,array1,array2,array3,array4,array5,array6,array7,array8,array9,array10};
int    arrayn[MODES];

#define PIN_DI 				13
#define PIN_DCKI 			15

bool  on;
float hue,sat,bri;              //homekit values
float huet,satt,brit;           //target values
int   rt,  gt,  bt,  wt;        //target values
int   rn,  gn,  bn,  wn;        //new values
int   ro=0,go=0,bo=0,wo=4095;   //old values
int   mode=0,arrayindex;
bool  changed;
int   transtime=-1,staytime=-1;

// add this section to make your device OTA capable
// create the extra characteristic &ota_trigger, at the end of the primary service (before the NULL)
// it can be used in Eve, which will show it, where Home does not
// and apply the four other parameters in the accessories_information section

#include "ota-api.h"
homekit_characteristic_t ota_trigger  = API_OTA_TRIGGER;
homekit_characteristic_t manufacturer = HOMEKIT_CHARACTERISTIC_(MANUFACTURER,  "X");
homekit_characteristic_t serial       = HOMEKIT_CHARACTERISTIC_(SERIAL_NUMBER, "1");
homekit_characteristic_t model        = HOMEKIT_CHARACTERISTIC_(MODEL,         "Z");
homekit_characteristic_t revision     = HOMEKIT_CHARACTERISTIC_(FIRMWARE_REVISION,  "0.0.0");

// next use these two lines before calling homekit_server_init(&config);
//    int c_hash=ota_read_sysparam(&manufacturer.value.string_value,&serial.value.string_value,
//                                      &model.value.string_value,&revision.value.string_value);
//    config.accessories[0]->config_number=c_hash;
// end of OTA add-in instructions


homekit_value_t mode_get() {
    return HOMEKIT_INT(mode);
}
void mode_set(homekit_value_t value) {
    if (value.format != homekit_format_int) {
        printf("Invalid mode-value format: %d\n", value.format);
        return;
    }
    mode = value.int_value;
    changed=1;
    printf("ModeSet: %d\n", mode);
    if (mode) { //set on=1 and publish
        on=true;
        //homekit_characteristic_notify(&on, HOMEKIT_INT(mode)); //publish mode (not yet supported in accessory defintion)
    }
}

#define HOMEKIT_CHARACTERISTIC_CUSTOM_MODE_SELECT HOMEKIT_CUSTOM_UUID("F0000002")
#define HOMEKIT_DECLARE_CHARACTERISTIC_CUSTOM_MODE_SELECT(_value, ...) \
    .type = HOMEKIT_CHARACTERISTIC_CUSTOM_MODE_SELECT, \
    .description = "ModeSelect", \
    .format = homekit_format_int, \
    .permissions = homekit_permissions_paired_read \
                 | homekit_permissions_paired_write \
                 | homekit_permissions_notify, \
    .unit = homekit_unit_none, \
    .min_value = (float[]) {0}, \
    .max_value = (float[]) {9}, \
    .min_step  = (float[]) {1}, \
    .value = HOMEKIT_INT_(_value), \
    ##__VA_ARGS__

homekit_characteristic_t mode_select = HOMEKIT_CHARACTERISTIC_(CUSTOM_MODE_SELECT, 0, .setter=mode_set, .getter=mode_get);



homekit_value_t fader_get() {
    return HOMEKIT_INT(array[0][0].transtime/1000);
}
void fader_set(homekit_value_t value) {
    if (value.format != homekit_format_int) {
        printf("Invalid fader-value format: %d\n", value.format);
        return;
    }
    array[0][0].transtime = value.int_value*1000;
    changed=1;
    printf("FaderSpeed: %d\n", array[0][0].transtime);
}

#define HOMEKIT_CHARACTERISTIC_CUSTOM_FADER_SPEED HOMEKIT_CUSTOM_UUID("F0000003")
#define HOMEKIT_DECLARE_CHARACTERISTIC_CUSTOM_FADER_SPEED(_value, ...) \
    .type = HOMEKIT_CHARACTERISTIC_CUSTOM_FADER_SPEED, \
    .description = "Speed of Fading", \
    .format = homekit_format_int, \
    .permissions = homekit_permissions_paired_read \
                 | homekit_permissions_paired_write \
                 | homekit_permissions_notify, \
    .unit = homekit_unit_seconds, \
    .min_value = (float[]) { 0}, \
    .max_value = (float[]) {10}, \
    .min_step  = (float[]) { 1}, \
    .value = HOMEKIT_INT_(_value), \
    ##__VA_ARGS__

homekit_characteristic_t fader_speed = HOMEKIT_CHARACTERISTIC_(CUSTOM_FADER_SPEED, 2, .setter=fader_set, .getter=fader_get);




//http://blog.saikoled.com/post/44677718712/how-to-convert-from-hsi-to-rgb-white
void hsi2rgbw(float h, float s, float i, int* rgbw) {
    int r, g, b, w;
    float cos_h, cos_1047_h;
    //h = fmod(h,360); // cycle h around to 0-360 degrees
    h = 3.14159*h/(float)180; // Convert to radians.
    s /=(float)100; i/=(float)100; //from percentage to ratio
    s = s>0?(s<1?s:1):0; // clamp s and i to interval [0,1]
    i = i>0?(i<1?i:1):0;
    i = i*sqrt(i); //shape intensity to have finer granularity near 0

    if(h < 2.09439) {
        cos_h = cos(h);
        cos_1047_h = cos(1.047196667-h);
        r = s*4095*i/3*(1+cos_h/cos_1047_h);
        g = s*4095*i/3*(1+(1-cos_h/cos_1047_h));
        b = 0;
        w = 4095*(1-s)*i;
    } else if(h < 4.188787) {
        h = h - 2.09439;
        cos_h = cos(h);
        cos_1047_h = cos(1.047196667-h);
        g = s*4095*i/3*(1+cos_h/cos_1047_h);
        b = s*4095*i/3*(1+(1-cos_h/cos_1047_h));
        r = 0;
        w = 4095*(1-s)*i;
    } else {
        h = h - 4.188787;
        cos_h = cos(h);
        cos_1047_h = cos(1.047196667-h);
        b = s*4095*i/3*(1+cos_h/cos_1047_h);
        r = s*4095*i/3*(1+(1-cos_h/cos_1047_h));
        g = 0;
        w = 4095*(1-s)*i;
    }

    rgbw[0]=r;
    rgbw[1]=g;
    rgbw[2]=b;
    rgbw[3]=w;
}

void light_loop_task(void *_args) {
    int rgbw[4],oldmode=0;
    while (1) {
        if (changed) {
            vTaskDelay(200/portTICK_PERIOD_MS); //to remove excessive updates from EVE
            changed=0;
            if (mode!=oldmode) {arrayindex=0; oldmode=mode;}
            array[0][0].hue=hue;array[0][0].sat=sat; //bri=100 at init to prevent double scaling
            transtime=-1; staytime=-1; //forces re-reading array
            if (!on) { //this is the escape from any other modes
                mode=0;
                homekit_characteristic_notify(&mode_select, HOMEKIT_INT(mode)); //publish mode
            }
        }
        if (transtime<0 && staytime<=0) { //time to read a new array entry
            if (arrayindex>=arrayn[mode]) arrayindex=0;
            huet=     array[mode][arrayindex].hue;      //read targetvalues from array
            satt=     array[mode][arrayindex].sat;
            brit=     array[mode][arrayindex].bri*bri/100;  //and scale by bri
            transtime=array[mode][arrayindex].transtime;
            staytime= array[mode][arrayindex].staytime;
            arrayindex++;
            //printf("hue=%3d,sat=%3d,bri=%3d => ",(int)huet,(int)satt,(int)brit);
            hsi2rgbw(huet,satt,brit,rgbw);
            rt=rgbw[0];gt=rgbw[1];bt=rgbw[2];wt=rgbw[3];
            //printf("r=%4d,g=%4d,b=%4d,w=%4d\n",rt,gt,bt,wt);
        }
        //printf("--- tt=%d,st=%d,index=%d\n",transtime,staytime,arrayindex);
        if (transtime>=0) {
            //calc new lightvalue using oldvalue,targetvalue and tt
            if (transtime<=BEATTIME){
                rn=rt; gn=gt; bn=bt; wn=wt;
            } else {
                rn=ro+(rt-ro)*BEATTIME/transtime;
                gn=go+(gt-go)*BEATTIME/transtime;
                bn=bo+(bt-bo)*BEATTIME/transtime;
                wn=wo+(wt-wo)*BEATTIME/transtime;
            }
            ro=rn;go=gn;bo=bn;wo=wn;  //oldvalue=newvalue
            transtime-=BEATTIME;
            //if (on) printf("                       rn=%4d,gn=%4d,bn=%4d,wn=%4d\n",rn,gn,bn,wn);
            if (on) mjpwm_send_duty(rn,gn,bn,wn);
            else    mjpwm_send_duty( 0, 0, 0, 0);
        } 
        if (transtime<0) staytime-=BEATTIME;
        vTaskDelay(BEATTIME/portTICK_PERIOD_MS);
    }
}

void light_init() {
    arrayn[0]= sizeof(array0)/sizeof(array[0][0]); //how to make this generic?
    arrayn[1]= sizeof(array1)/sizeof(array[0][0]);
    arrayn[2]= sizeof(array2)/sizeof(array[0][0]);
    arrayn[3]= sizeof(array3)/sizeof(array[0][0]);
    arrayn[4]= sizeof(array4)/sizeof(array[0][0]);
    arrayn[5]= sizeof(array5)/sizeof(array[0][0]);
    arrayn[6]= sizeof(array6)/sizeof(array[0][0]);
    arrayn[7]= sizeof(array7)/sizeof(array[0][0]);
    arrayn[8]= sizeof(array8)/sizeof(array[0][0]);
    arrayn[9]= sizeof(array9)/sizeof(array[0][0]);
    arrayn[10]= sizeof(array10)/sizeof(array[0][0]); //for identify task
    array[0][0].hue=  0;
    array[0][0].sat=  0;
    array[0][0].bri=100; //static bri=100 to prevent double scaling
    array[0][0].transtime  =2000; //later make it an input
    array[0][0].staytime =100000; //just a big value
    
    mjpwm_cmd_t init_cmd = {
        .scatter = MJPWM_CMD_SCATTER_APDM,
        .frequency = MJPWM_CMD_FREQUENCY_DIVIDE_1,
        .bit_width = MJPWM_CMD_BIT_WIDTH_12,
        .reaction = MJPWM_CMD_REACTION_FAST,
        .one_shot = MJPWM_CMD_ONE_SHOT_DISABLE,
        .resv = 0,
    };
    mjpwm_init(PIN_DI, PIN_DCKI, 1, init_cmd);
    mjpwm_send_duty( 0, 0, 0, 4095);
    on=true; hue=0; sat=0; bri=100; //matches rgbw-old values
    changed=1;
    xTaskCreate(light_loop_task, "Light loop", 512, NULL, 1, NULL);
}

homekit_value_t light_on_get() {
    return HOMEKIT_BOOL(on);
}
void light_on_set(homekit_value_t value) {
    if (value.format != homekit_format_bool) {
        printf("Invalid on-value format: %d\n", value.format);
        return;
    }
    //printf("O:%3d @ %d\n",value.bool_value,sdk_system_get_time());
    printf("O:%3d\n",value.bool_value);
    on = value.bool_value;
    changed=1;
}

homekit_value_t light_bri_get() {
    return HOMEKIT_INT(bri);
}
void light_bri_set(homekit_value_t value) {
    if (value.format != homekit_format_int) {
        printf("Invalid bri-value format: %d\n", value.format);
        return;
    }
    //printf("B:%3d @ %d\n",value.int_value,sdk_system_get_time());
    printf("B:%3d\n",value.int_value);
    bri = value.int_value;
    changed=1;
}

homekit_value_t light_hue_get() {
    return HOMEKIT_FLOAT(hue);
}
void light_hue_set(homekit_value_t value) {
    if (value.format != homekit_format_float) {
        printf("Invalid hue-value format: %d\n", value.format);
        return;
    }
    //printf("H:%3.0f @ %d\n",value.float_value,sdk_system_get_time());
    printf("H:%3.0f\n",value.float_value);
    hue = value.float_value;
    changed=1;
}

homekit_value_t light_sat_get() {
    return HOMEKIT_FLOAT(sat);
}
void light_sat_set(homekit_value_t value) {
    if (value.format != homekit_format_float) {
        printf("Invalid sat-value format: %d\n", value.format);
        return;
    }
    //printf("S:%3.0f @ %d\n",value.float_value,sdk_system_get_time());
    printf("S:%3.0f\n",value.float_value);
    sat = value.float_value;
    changed=1;
}

void light_identify_task(void *_args) {
    int oldmode;
    oldmode=mode;
    mode=10; changed=1;
    vTaskDelay(5000 / portTICK_PERIOD_MS); //5 sec
    mode=oldmode; changed=1;
    vTaskDelete(NULL);
}

void identify(homekit_value_t _value) {
    printf("Light Identify\n");
    xTaskCreate(light_identify_task, "Light identify", 256, NULL, 2, NULL);
}


homekit_accessory_t *accessories[] = {
    HOMEKIT_ACCESSORY(
        .id=1,
        .category=homekit_accessory_category_lightbulb,
        .services=(homekit_service_t*[]){
            HOMEKIT_SERVICE(ACCESSORY_INFORMATION,
                .characteristics=(homekit_characteristic_t*[]){
                    HOMEKIT_CHARACTERISTIC(NAME, "ZemiSmartBulb"),
                    &manufacturer,
                    &serial,
                    &model,
                    &revision,
                    HOMEKIT_CHARACTERISTIC(IDENTIFY, identify),
                    NULL
                }),
            HOMEKIT_SERVICE(LIGHTBULB, .primary=true,
                .characteristics=(homekit_characteristic_t*[]){
                    HOMEKIT_CHARACTERISTIC(NAME, "Light"),
                    HOMEKIT_CHARACTERISTIC(
                        ON, true,
                        .getter=light_on_get,
                        .setter=light_on_set
                    ),
                    HOMEKIT_CHARACTERISTIC(
                        BRIGHTNESS, 100,
                        .getter=light_bri_get,
                        .setter=light_bri_set
                    ),
                    HOMEKIT_CHARACTERISTIC(
                        HUE, 0,
                        .getter=light_hue_get,
                        .setter=light_hue_set
                    ),
                    HOMEKIT_CHARACTERISTIC(
                        SATURATION, 0,
                        .getter=light_sat_get,
                        .setter=light_sat_set
                    ),
                    &ota_trigger,
                    &mode_select,
                    &fader_speed,
                    NULL
                }),
            NULL
        }),
    NULL
};

homekit_server_config_t config = {
    .accessories = accessories,
    .password = "111-11-111"
};

void user_init(void) {
    uart_set_baud(0, 115200);
    udplog_init(2);
    UDPLUS("\n\n\nZemiSmart " VERSION "\n");

    light_init();

    int c_hash=ota_read_sysparam(&manufacturer.value.string_value,&serial.value.string_value,
                                      &model.value.string_value,&revision.value.string_value);
    //c_hash=1013; revision.value.string_value="0.1.13"; //cheat line
    config.accessories[0]->config_number=c_hash;
    
    homekit_server_init(&config);
}
