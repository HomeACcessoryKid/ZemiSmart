#include <stdlib.h>  //for printf
#include <stdio.h>
#include <string.h>

#include <rboot-api.h>
#include <sysparam.h>

// the first function is the ONLY thing needed for a repo to support ota after having started with ota-boot
// in ota-boot the user gets to set the wifi and the repository details and it then installs the ota-main binary

void ota_update(void) {
    rboot_set_temp_rom(1); //select the OTA main routine
    sdk_system_restart();  //#include <rboot-api.h>
    // there is a bug in the esp SDK such that if you do not power cycle the chip after serial flashing, restart is unreliable
}

// this function is optional to couple Homekit parameters to the sysparam variables and github parameters
int  ota_read_sysparam(char **manufacturer,char **model,char **revision) {
    sysparam_status_t status;
    char *value;

    status = sysparam_get_string("ota_repo", &value);
    if (status == SYSPARAM_OK) {
        strchr(value,'/')[0]=0;
        *manufacturer=value;
        *model=value+strlen(value)+1;
    } else {
        *manufacturer="manuf_unknown";
        *model="model_unknown";
    }
    status = sysparam_get_string("ota_version", &value);
    if (status == SYSPARAM_OK) {
        *revision=value;
    } else *revision="0.0.1";

    int c_hash=0;
    char version[16];
    char* rev=version;
    char* dot;
    strncpy(rev,*revision,16);
    if ((dot=strchr(rev,'.'))) {dot[0]=0; c_hash=            atoi(rev); rev=dot+1;}
    if ((dot=strchr(rev,'.'))) {dot[0]=0; c_hash=c_hash*1000+atoi(rev); rev=dot+1;}
                                          c_hash=c_hash*1000+atoi(rev);
                                            //c_hash=c_hash*10  +configuration_variant; //possible future extension
    printf("manuf=\'%s\' model=\'%s\' revision=\'%s\' c#=%d\n",*manufacturer,*model,*revision,c_hash);
    return c_hash;
}

