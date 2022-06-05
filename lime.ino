#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <EEPROM.h>

#define RX_BUFFER_SIZE 14
#define TX_BYTE_SIZE 20
#define PI 3.14159
#define METERS_PER_KM 1000
#define INTERVAL_IN_MS 100
#define ONE_SECOND_IN_MS 1000
#define MM_PER_METER 1000
#define SECONDS_IN_ONE_HOUR 3600
#define WHEEL_DIAMETER_IN_MM 254
#define BLANKING_INTERVAL_IN_SEC 10

#define TFT_CS -1
#define TFT_RST        4 // D2 from RES pin on display
#define TFT_DC         5 // D1 from DC pin on display
#define TFT_SCLK 14  // D5 Clock out, from SCL on display
#define TFT_MOSI 13  // D7 Data out, from SDA on display

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

float current_speed_in_meters_per_second = 0;
float current_speed_in_km_per_hour;
float last_speed_in_km_per_hour = 0;
double current_odometer = 0; // in meters
unsigned long eeprom_odometer_in_km = 0;
double last_odometer = 0;
float last_volts;

unsigned long last_odometer_update_in_millis;
unsigned long last_moving_time_in_millis;

uint8_t screen_is_blanked;

void setup_tft() {
  screen_is_blanked = 0;
  last_moving_time_in_millis = millis();
  tft.init(240, 240, SPI_MODE3);
  tft.setSPISpeed(10000000);
  tft.setRotation(2);
  tft.fillScreen(ST77XX_BLACK);
}

void wake_tft() {
  setup_tft();
  draw_kmh_legend();
  draw_battery_legend();
  display_odometer(1);
  set_screen_blank(0);
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(A0, INPUT);
  pinMode(15, OUTPUT);
  Serial.begin(9600);
  EEPROM.begin(4);
  wake_tft();
  last_odometer_update_in_millis = millis();
  read_odometer_from_eeprom();
}

void loop() {
  receive_controller_msg();
  send_controller_settings();
  float current_volts = get_battery_voltage();
  display_battery_voltage(current_volts);
  compute_odometer(current_volts);
  display_odometer(0);
  display_speed();

  toggle_onboard_led();
  if (last_moving_time_in_millis + ONE_SECOND_IN_MS * BLANKING_INTERVAL_IN_SEC  < millis()) {
    set_screen_blank(1);
  } else if (screen_is_blanked) {
    set_screen_blank(0);
  }
  if ( floor(current_odometer / METERS_PER_KM ) > eeprom_odometer_in_km) {
      write_odometer_to_eeprom();
    }
  last_speed_in_km_per_hour = current_speed_in_km_per_hour;
  last_odometer = current_odometer;
  last_volts = current_volts;

  delay(INTERVAL_IN_MS);
}

void write_odometer_to_eeprom() {
  EEPROM.put(0, (unsigned long) ((unsigned long) floor(current_odometer / METERS_PER_KM) ));
  EEPROM.commit();
}

void zero_odometer() {
  unsigned long zero_odometer = 99;
  EEPROM.put(0, zero_odometer);
  EEPROM.commit();
}

void read_odometer_from_eeprom() {
  EEPROM.get(0, eeprom_odometer_in_km);
  current_odometer = eeprom_odometer_in_km * METERS_PER_KM;
}

void set_screen_blank(uint8_t is_blanked) {
  digitalWrite(15, (is_blanked) ? LOW : HIGH );
  screen_is_blanked = is_blanked;
}

float get_speed_in_meters_per_second(byte* frame_buf) {
  byte MSB = frame_buf[8];
  byte LSB = frame_buf[9];
  // I counted 82 revs in 60s while reading 2000ms for rotation_time_in_ms, yielding the 2.7 adjustment.
  float fudge = 2.7;
  unsigned int rotation_time_in_ms = (MSB << 8) + LSB;
  float wheel_circumference_mm = PI * WHEEL_DIAMETER_IN_MM;
  float rotations_per_second = ONE_SECOND_IN_MS / ( (float) rotation_time_in_ms / fudge );
  float speed_in_meters_per_second = rotations_per_second * wheel_circumference_mm  / MM_PER_METER;


  if (MSB == 0x17 && LSB == 0x70)
    speed_in_meters_per_second = 0;

  return (speed_in_meters_per_second);
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

float get_battery_voltage() {
  unsigned int adc_voltage = analogRead(A0);
  float volts = adc_voltage / 1023.0;
  return (volts * 38);
}

unsigned int led = 0;

void toggle_onboard_led() {
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
  if (bytes_tx != TX_BYTE_SIZE) {
    //display_tx_error(bytes_tx);
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
      if (valid_checksum(rx_buffer)) {
        current_speed_in_meters_per_second = get_speed_in_meters_per_second(rx_buffer);
        if (current_speed_in_meters_per_second > 0)
          last_moving_time_in_millis = millis();
      } else {
        drain_serial();
        //        display_checksum_error();
      }
      rx_idx = 0;
    }
  }
}

void compute_odometer(float current_volts) {
  unsigned long now_in_millis = millis();
  unsigned long duration_in_millis = now_in_millis - last_odometer_update_in_millis;
  float duration_in_seconds = (float) duration_in_millis / ONE_SECOND_IN_MS;
  if ( (int) current_volts > 5) // is the scooter on?
    current_odometer = current_odometer + current_speed_in_meters_per_second * duration_in_seconds;
  last_odometer_update_in_millis = now_in_millis;
}

#define SPEED_CHAR1_OFFSET 40
#define SPEED_CHAR2_OFFSET 130
#define SPEED_VERT_OFFSET 50

#define ODOMETER_CHAR1_OFFSET 40
#define ODOMETER_VERT_OFFSET 205
#define ODOMETER_CHAR_INC_OFFSET 20

#define VOLTS_CHAR1_OFFSET 52
#define VOLTS_VERT_OFFSET 15

void draw_kmh_legend() {
  tft.setTextSize(4);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(80, 160);
  tft.print( "km/h");
}

void draw_battery_legend() {
  tft.setTextSize(3);
  tft.setTextColor(ST77XX_GREEN);
  tft.setCursor(VOLTS_CHAR1_OFFSET + 53, VOLTS_VERT_OFFSET);
  tft.print( "volts");
}

void display_battery_voltage(float current_volts) {
  if (current_volts >= last_volts + 10 ||
      current_volts <= last_volts - 10  )
    wake_tft(); // turning it on or off fucks up the SPI. Maybe it needs shielding?
  tft.setTextSize(3);
  char buffer[8];
  if ((int) current_volts != (int) last_volts) {
    tft.setTextColor(ST77XX_BLACK);
    tft.setCursor(VOLTS_CHAR1_OFFSET, VOLTS_VERT_OFFSET);
    sprintf(buffer, "%02d", (int) last_volts);
    tft.print(buffer);
  }
  tft.setTextColor(ST77XX_GREEN);
  tft.setCursor(VOLTS_CHAR1_OFFSET, VOLTS_VERT_OFFSET);
  sprintf(buffer, "%02d", (int) current_volts);
  tft.print(buffer);
}
void XXX_init_odometer(void) {
  unsigned int offset = 0;
  tft.setTextSize(3);
  tft.setTextColor(ST77XX_CYAN);
  for (int idx = 7; idx >= 0; idx--) {
    tft.setCursor(ODOMETER_CHAR1_OFFSET + offset, ODOMETER_VERT_OFFSET);
    tft.print(0);
    offset = offset + ODOMETER_CHAR_INC_OFFSET;
  }
}

void display_odometer(uint8_t is_init) {
  if  ( !is_init && (unsigned long) current_odometer == (unsigned long) last_odometer )
    return;

  double current_odometer_copy = current_odometer;
  double last_odometer_copy = last_odometer;

  unsigned int last_odometer_previous_msd = 0;
  unsigned int current_odometer_previous_msd = 0;
  unsigned int offset = 0;
  char buffer[16];

  for (int idx = 7; idx >= 0; idx--) {
    last_odometer_copy = last_odometer_copy - last_odometer_previous_msd;
    unsigned int last_odometer_sd = last_odometer_copy / pow(10, idx);
    last_odometer_previous_msd = last_odometer_sd * pow(10, idx);

    current_odometer_copy = current_odometer_copy - current_odometer_previous_msd;
    unsigned int current_odometer_sd = current_odometer_copy / pow(10, idx);
    current_odometer_previous_msd = current_odometer_sd * pow(10, idx);

    if (is_init || current_odometer_sd != last_odometer_sd ) {
      tft.setTextSize(3);
      // erase old digit
      tft.setTextColor(ST77XX_BLACK);
      tft.setCursor(ODOMETER_CHAR1_OFFSET + offset, ODOMETER_VERT_OFFSET);
      tft.print(last_odometer_sd);
      // write new digit
      tft.setTextColor(ST77XX_CYAN);
      tft.setCursor(ODOMETER_CHAR1_OFFSET + offset, ODOMETER_VERT_OFFSET);
      tft.print(current_odometer_sd);
    }
    offset = offset + ODOMETER_CHAR_INC_OFFSET;
  }
}

void display_speed(void) {
  uint8_t t_speed;
  uint8_t o_speed;
  uint8_t lt_speed;
  uint8_t lo_speed;

  tft.setTextSize(14);

  current_speed_in_km_per_hour = current_speed_in_meters_per_second * 60 * 60 / 1000;

  t_speed = current_speed_in_km_per_hour / 10;
  o_speed = current_speed_in_km_per_hour - (t_speed * 10);
  lt_speed = last_speed_in_km_per_hour / 10;
  lo_speed = last_speed_in_km_per_hour - (lt_speed * 10);

  if (t_speed != lt_speed) {
    tft.setTextColor(ST77XX_BLACK);
    tft.setCursor(SPEED_CHAR1_OFFSET, SPEED_VERT_OFFSET);
    tft.print(lt_speed);
  }
  if (o_speed != lo_speed) {
    tft.setTextColor(ST77XX_BLACK);
    tft.setCursor(SPEED_CHAR2_OFFSET, SPEED_VERT_OFFSET);
    tft.print(lo_speed);
  }
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(SPEED_CHAR1_OFFSET, SPEED_VERT_OFFSET);
  tft.print(t_speed);
  tft.setCursor(SPEED_CHAR2_OFFSET, SPEED_VERT_OFFSET);
  tft.print(o_speed);
}

void display_error(char *msg) {
  tft.fillRect(0, 0,  240, 15, ST77XX_RED);
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(0, 0);
  char buffer[24];
  char small_buffer[24];
  strncpy(small_buffer, msg, 24 - 1);
  sprintf(buffer, "%s", small_buffer);
  tft.print(buffer);
}

void display_tx_error(unsigned int bytes) {
  char str[24];
  sprintf(str, "error: tx %d bytes", bytes);
  display_error(str);
}

void display_checksum_error() {
  char str[24];
  sprintf(str, "error: bad checksum");
  display_error(str);
}
