#ifndef LED_H
#define LED_H

class Led {
public:
    void init(unsigned char pin);
    void on();
    void off();
private:
    unsigned char pin;
};

#endif