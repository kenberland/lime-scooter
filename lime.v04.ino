#include <LiquidCrystal_I2C.h>
#define RX_BUFFER_SIZE 14
#define TX_BYTE_SIZE 20

LiquidCrystal_I2C lcd(0x27, 20, 4);

void setup_lcd() {
  // The begin call takes the width and height. This
  // Should match the number provided to the constructor.
  lcd.begin(20, 4);
  lcd.init();
  lcd.backlight();
  display_error(char "Init");
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(A0, INPUT);
  setup_lcd();
  Serial.begin(9600);
}

#define PI 3.14159
float get_speed(byte* frame_buf) {
  unsigned char *p;
  byte MSB = frame_buf[8];
  byte LSB = frame_buf[9];
  unsigned int wheel_diameter_mm = 241;
  unsigned int rotation_time_in_ms = (MSB << 8) + LSB;
  float wheel_circumference_mm = PI * wheel_diameter_mm;
  float rotations_per_second = 1000 / (float) rotation_time_in_ms;
  float speed_in_km_per_hour = rotations_per_second * wheel_circumference_mm * 60 * 60 / 1000 / 1000;
  return (speed_in_km_per_hour);
}

bool valid_checksum(byte* frame_buf) {
  byte xor_checksum = 0;
  byte *p;
  byte tmp;
  for (p = frame_buf; p < frame_buf + (RX_BUFFER_SIZE - 1); p++) {
    tmp = *p;
    xor_checksum = xor_checksum ^ tmp;
  }
  return (xor_checksum == frame_buf[RX_BUFFER_SIZE - 1]);
}

#define ERROR_LCD_ROW 0
#define SPEED_LCD_ROW 1
#define ODO_LCD_ROW 2
#define BAT_LCD_ROW 3


void display_message(char *msg, int row) {
  lcd.setCursor(0, row);
  lcd.print(msg);
}

void display_error(char *msg) {
  display_message(msg, ERROR_LCD_ROW);
}

void display_tx_error(unsigned int bytes) {
  char str[21];
  sprintf(str, "error: tx %d bytes", bytes);
  display_error(str);
}

void display_checksum_error() {
  char str[21];
  sprintf(str, "bad checksum");
  display_error(str);
}

void display_battery_message(float volts) {
  char str[21];
  sprintf(str, "bat: %2.2f volts", volts);
  display_message(str, BAT_LCD_ROW);
}

void display_speed(float speed) {
  char str[21];
  sprintf(str, "speed %f km/h", speed);
  display_message(str, SPEED_LCD_ROW);
}

void show_battery_voltage() {
  unsigned int adc_voltage = analogRead(A0);
  float volts = adc_voltage / 1023.0;
  volts = volts * 40;
  display_battery_message(volts);
}

unsigned int led = 0;

void blink_onboard_led() {
  if (led == 0) {
    digitalWrite(LED_BUILTIN, HIGH);
    led = 1;
  } else {
    digitalWrite(LED_BUILTIN, LOW);
    led = 0;
  }

}

byte rx_buffer[RX_BUFFER_SIZE];
unsigned int rx_idx = 0;

void send_controller_settings() {
  // byte tx_buf[] = { 0x01, 0x14, 0x01, 0x01, 0x05, 0x80, 0x50, 0x00, 0x5f, 0x01, 0x05, 0x00, 0x64, 0x16, 0x01, 0x22, 0x00, 0x00, 0x16, 0xdc }; // speed 1
  byte tx_buf[] = { 0x01, 0x14, 0x01, 0x01, 0x0f, 0x80, 0x50, 0x00, 0x5f, 0x01, 0x05, 0x00, 0x64, 0x16, 0x01, 0x22, 0x00, 0x00, 0x16, 0xd6 }; // speed 3
  unsigned int bytes_tx;
  bytes_tx = Serial.write(tx_buf, TX_BYTE_SIZE);
  if (bytes_tx == TX_BYTE_SIZE) {
    blink_onboard_led();
  } else {
    display_tx_error(bytes_tx);
  }
}

void drain_serial() {
  while (Serial.available() > 0) {
    Serial.readBytes(rx_buffer, RX_BUFFER_SIZE);
  }
}

void receive_controller_msg() {
  while (Serial.available() > 0) {
    int rlen = Serial.readBytes(rx_buffer + rx_idx, 1);
    rx_idx = rx_idx + rlen;
    if (rx_idx == RX_BUFFER_SIZE) {
      blink_onboard_led();
      if (valid_checksum(rx_buffer)) {
        display_speed(get_speed(rx_buffer));
      } else {
        drain_serial();
        display_checksum_error();
      }
      rx_idx = 0;
    }
  }
}

void loop() {
  receive_controller_msg();
  send_controller_settings();
  show_battery_voltage();
  delay(100);
}
