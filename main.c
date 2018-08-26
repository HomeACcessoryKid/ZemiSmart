/*  (c) 2018 HomeAccessoryKid
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
#include <esp/uart.h>
#include <esp8266.h>
#include <FreeRTOS.h>
#include <task.h>

#include <homekit/homekit.h>
#include <homekit/characteristics.h>
//#include "wifi.h"

#include <math.h>  //requires LIBS ?= hal m to be added to Makefile
#include "mjpwm.h"

#define  BEATTIME   250 //the granularity of calculating light transitions in ms
#define  MODES       10 //0-9

typedef struct _values {
    int hue;
    int sat;
    int bri;
    int transtime;
    int staytime;
} values;

values  array0[1]; //this array is loaded with the homekit values
//   hue sat bri      tt      st
values  array1[] = {
    {  0,100,100,      0,   1000}, //0
    { 90,100,100,      0,   1000}, //1
    {180,100,100,      0,   1000}, //2
    {270,100,100,      0,   1000}  //3
};
values  array2[] = {
    { 45,100,100,   1000,   2000}, //0
    {135,100,100,   1000,   2000}, //1
    {225,100,100,   1000,   3000}, //2
    {315,100,100,   1000,   3000}  //3
};
values  array3[1];
values  array4[1];
values  array5[1];
values  array6[1];
values  array7[1];
values  array8[1];
values  array9[1];

values *array[MODES] = {array0,array1,array2,array3,array4,array5,array6,array7,array8,array9};
int    arrayn[MODES];

#define PIN_DI 				13
#define PIN_DCKI 			15

bool  on;
float hue,sat,bri;              //homekit values
float huet,satt,brit;           //target values
float huen,satn,brin;           //new values
float hueo=0,sato=0,brio=100;   //old values
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
    .min_step = (float[]) {1}, \
    .value = HOMEKIT_INT_(_value), \
    ##__VA_ARGS__

homekit_characteristic_t mode_select = HOMEKIT_CHARACTERISTIC_(CUSTOM_MODE_SELECT, 0, .setter=mode_set, .getter=mode_get);




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

void lightSET(void) {
    int rgbw[4];
    if (on) {
        printf("h=%d,s=%d,b=%d => ",(int)huen,(int)satn,(int)brin);
        
        hsi2rgbw(huen,satn,brin,rgbw);
        printf("r=%d,g=%d,b=%d,w=%d\n",rgbw[0],rgbw[1],rgbw[2],rgbw[3]);
        
        mjpwm_send_duty(rgbw[0],rgbw[1],rgbw[2],rgbw[3]);
    } else {
        printf("off\n");
        mjpwm_send_duty(     0,      0,      0,      0 );
    }
}

void light_loop_task(void *_args) {
    while (1) {
        if (changed) {
            changed=0; arrayindex=0;
            array[0][0].hue=hue;array[0][0].sat=sat; //bri=100 at init to prevent double scaling
            transtime=-1; staytime=-1; //forces re-reading array
            if (!on) { //this is the escape from any other modes
                mode=0;
                homekit_characteristic_notify(&mode_select, HOMEKIT_INT(mode)); //publish mode
            }
        }
        if (transtime<0 && staytime<=0) {
            if (arrayindex>=arrayn[mode]) arrayindex=0;
            huet=     array[mode][arrayindex].hue;      //read targetvalues from array
            satt=     array[mode][arrayindex].sat;
            brit=     array[mode][arrayindex].bri*bri/100;  //and scale by bri
            transtime=array[mode][arrayindex].transtime;
            staytime= array[mode][arrayindex].staytime;
            arrayindex++;
        }
        printf("tt=%d,st=%d,ai=%d\n",transtime,staytime,arrayindex);
        if (transtime>=0) {
            //calc new lightvalue using oldvalue,targetvalue and tt
            huen=huet;satn=satt;brin=brit;
            lightSET();                     //set      newvalue
            hueo=huen;sato=satn;brio=brin;  //oldvalue=newvalue
            transtime-=BEATTIME;
        } 
        if (transtime<0) staytime-=BEATTIME;  //not perfectly accurate yet...(or yes?)
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
    on=true; hue=hueo; sat=sato; bri=brio;
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
    printf("O:");
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
    printf("B:");
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
    printf("H:");
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
    printf("S:");
    sat = value.float_value;
    changed=1;
}

void light_identify_task(void *_args) {
    for (int i=0;i<5;i++) {
        mjpwm_send_duty(4095,    0,    0,    0);
        vTaskDelay(300 / portTICK_PERIOD_MS); //0.3 sec
        mjpwm_send_duty(   0, 4095,    0,    0);
        vTaskDelay(300 / portTICK_PERIOD_MS); //0.3 sec
        mjpwm_send_duty(   0,    0, 4095,    0);
        vTaskDelay(300 / portTICK_PERIOD_MS); //0.3 sec
    }
    lightSET();

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

    light_init();

    int c_hash=ota_read_sysparam(&manufacturer.value.string_value,&serial.value.string_value,
                                      &model.value.string_value,&revision.value.string_value);
    //c_hash=1010; revision.value.string_value="0.1.10"; //cheat line
    config.accessories[0]->config_number=c_hash;
    
    homekit_server_init(&config);
}
