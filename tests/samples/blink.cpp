// Blink example - C++ for PIC18
class Led {
public:
    void init(unsigned char pin) { this->pin = pin; }
    void on() { PORTB |= (1 << pin); }
    void off() { PORTB &= ~(1 << pin); }
private:
    unsigned char pin;
};

void setup() {
    TRISB = 0x00;
    Led led;
    led.init(1);
    while (true) {
        led.on();
        for (volatile int i = 0; i < 100000; i++);
        led.off();
        for (volatile int i = 0; i < 100000; i++);
    }
}