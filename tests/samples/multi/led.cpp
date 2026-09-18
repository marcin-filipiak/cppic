#include "led.h"

void Led::init(unsigned char pin) {
    this->pin = pin;
    TRISB &= ~(1 << this->pin);
}

void Led::on() {
    PORTB |= (1 << this->pin);
}

void Led::off() {
    PORTB &= ~(1 << this->pin);
}