# ZemiSmart RGBW ledlamp enabled for HomeKit

It has a fader and adjustable fading time

It also has modes that can run predefined schedules of fading


### practical stuff
Compiled version 0.3.1 with [esp-open-rtos#a721fb0](https://github.com/SuperHouse/esp-open-rtos/commit/a721fb0bc7867ef421cd81fb89d486ed2a67ee9e)  
Compiled version 0.3.2 with [esp-open-rtos#bc97988](https://github.com/SuperHouse/esp-open-rtos/commit/bc979883c27ea57e948daa813e2bca752ebd39e1)  

```
openssl sha384 -binary -out firmware/main.bin.sig firmware/main.bin
printf "%08x" `cat firmware/main.bin | wc -c`| xxd -r -p >>firmware/main.bin.sig
echo -n 0.3.2 > firmware/latest-pre-release
```
