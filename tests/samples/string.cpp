// Arduino-style String example - exercises the String runtime lowering.
String greeting = "Hello";
String message;
String combo;

void setup() {
    TRISB = 0x00;

    message = greeting + ", world";   // concat String + literal
    message += "!";                   // append literal

    String copy = message;            // deep copy of a String
    String extra = "Hello";           // local literal init

    combo = greeting;                 // copy
    combo += " ";                     // append literal
    combo += message;                 // append String

    unsigned char ok = 1;
    ok = ok && (message.length() == 13);
    ok = ok && message.startsWith("He");
    ok = ok && message.endsWith("d!");
    ok = ok && (message.indexOf('w') == 7);
    ok = ok && (message.charAt(1) == 'e');
    ok = ok && (message[0] == 'H');
    ok = ok && (message.c_str()[0] == 'H');
    ok = ok && !message.isEmpty();
    ok = ok && (message == copy);
    ok = ok && (message != greeting);
    ok = ok && (greeting < message);
    ok = ok && (extra == "Hello");
    ok = ok && combo.startsWith("Hello Hello");

    if (ok)
        PORTB = 0x01;
    else
        PORTB = 0x00;
}

void loop() {
}
