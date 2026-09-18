#include "led.h"

Led led;

void setup() {
    led.init(1);
}

void loop() {
    led.off();
    led.on();
}