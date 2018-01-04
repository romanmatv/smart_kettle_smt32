#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <EEPROM.h>
#include <OneButton.h>
#include <OneWireSTM.h>
#include <stdio.h>
#include <DS1302.h>
#include <Time.h>
DS1302 rtc(PB14, PB15, PB13);
#define TEN_H PA8 //Мощный ТЭН
#define TEN_L PA9 //Слабый ТЭН
#define POMP PA10 //Помпа

float current_temp = 0; //Текущая температура
byte need_temp = 90; //Поддерживаемая температура
byte temp_minus = 5; //Нижняя граница поддерживаемой температуры
byte temp_plus = 5; //Верхняя граница поддерживаемой температуры
bool force_enabled = false; //Принудительное поддержание температуры
bool force_disabled = false; //Принудительное отключение поддержания температуры
bool boiling_on_start = false; //Кипичение при включении
bool boiling_on_lowtemp = false; //Кипичение при резком снижении температуры
#define temp_boiling 98 //Тепература кипичения
byte boilint_time = 10; //Кол-во секунд кипичения
//Статусы
#define DISABLED 0 //Отключен
#define POTTING 1 //Поддержка температуры
#define BOILING 2 //Кипичение
byte now_status = DISABLED; //Текущий статус

byte boiling_tm = boilint_time;

bool debug = true; //Вывод дебаг информации в Серийный порт if(debug)

struct shoud_iteam {
  byte week = 200;
  byte h_start;
  byte m_start;
  byte h_stop;
  byte m_stop;
};

shoud_iteam Shudle [100] = {
    {8, 7, 0, 9, 0},
    {8, 20, 0, 23, 59},
    {9, 9, 0, 23, 59}
};

// Setup a new OneButton on pin B11.  
OneButton btn_pomp(PB11, true);
// Setup a new OneButton on pin B10.  
OneButton btn_heating(PB10, true);

double temps[50];
byte countTemps = 50;
byte iTemp = 0;

OneWire ds(PB12); //Датчик температуры на пине B12

String dayAsString(const Time::Day day, bool russian = false) {
  if (russian){
    switch (day) {
      case Time::kSunday: return utf8rus("ВС");
      case Time::kMonday: return utf8rus("ПН");
      case Time::kTuesday: return utf8rus("ВТ");
      case Time::kWednesday: return utf8rus("СР");
      case Time::kThursday: return utf8rus("ЧТ");
      case Time::kFriday: return utf8rus("ПТ");
      case Time::kSaturday: return utf8rus("СБ");
    }
    return utf8rus("(Н/Д)");
  }else{
    switch (day) {
      case Time::kSunday: return "Sunday";
      case Time::kMonday: return "Monday";
      case Time::kTuesday: return "Tuesday";
      case Time::kWednesday: return "Wednesday";
      case Time::kThursday: return "Thursday";
      case Time::kFriday: return "Friday";
      case Time::kSaturday: return "Saturday";
    }
    return "(unknown day)";
  }
}

int dayAsInt(const Time::Day day){
    switch (day) {
      case Time::kSunday: return 7;
      case Time::kMonday: return 1;
      case Time::kTuesday: return 2;
      case Time::kWednesday: return 3;
      case Time::kThursday: return 4;
      case Time::kFriday: return 5;
      case Time::kSaturday: return 6;
    }
}

void printTime() {
  // Get the current time and date from the chip.
  Time t = rtc.time();

  // Name the day of the week.
  const String day = dayAsString(t.day);

  // Format the time and date and insert into the temporary buffer.
  char buf[50];
  snprintf(buf, sizeof(buf), "%s %04d-%02d-%02d %02d:%02d:%02d",
           day.c_str(),
           t.yr, t.mon, t.date,
           t.hr, t.min, t.sec);

  // Print the formatted string to serial so we can see the time.
  Serial.println(buf);
}

String getStrTime(bool russian = false, bool secs = false){
  Time t = rtc.time();

  // Name the day of the week.
  const String day = dayAsString(t.day, russian);

  // Format the time and date and insert into the temporary buffer.
  char buf[50];
  if (secs){
    snprintf(buf, sizeof(buf), "%s %04d-%02d-%02d %02d:%02d:%02d",
           day.c_str(),
           t.yr, t.mon, t.date,
           t.hr, t.min, t.sec);
  }else{
    snprintf(buf, sizeof(buf), "%s %04d-%02d-%02d %02d:%02d",
           day.c_str(),
           t.yr, t.mon, t.date,
           t.hr, t.min);
  }
  String str(buf);
  return str;
}

/*
 * Данные экранчика
 */
#define OLED_MOSI  PA3 //D1
#define OLED_CLK   PA4 //D0
#define OLED_DC    PA1 //DC
#define OLED_CS    PA0 //CS
#define OLED_RESET PA2 //RES
Adafruit_SSD1306 display(OLED_MOSI, OLED_CLK, OLED_DC, OLED_RESET, OLED_CS);
#define SSD1306_LCDHEIGHT 64

int timer_secs = 0; //Переменная для тайминга

void setup() {
  Serial.begin(9600);
  pinMode(TEN_H, OUTPUT);
  digitalWrite(TEN_H, HIGH);
  pinMode(TEN_L, OUTPUT); 
  digitalWrite(TEN_L, HIGH);
  pinMode(POMP, OUTPUT); 
  digitalWrite(POMP, HIGH);
  /*
   * Настраиваем экранчик
   */
   display.begin(SSD1306_SWITCHCAPVCC);
   display.display();
   display.clearDisplay();
   display.cp437(true);

  //Читка настроек из EEPROM
  /* Принудительное поддержание/отключение
   *  Индекс: 0
   *  Значения: 2 - отключение, 1 - включение, иное - ни то ни то
   *
  if (EEPROM[0]==1){
    force_enabled = true;
    force_disabled = false;
    now_status = POTTING;
  }else if (EEPROM[0]==2){
    force_enabled = false;
    force_disabled = true;
  }else{
    force_enabled = false;
    force_disabled = false;
  }
  now_status = DISABLED;
  /*
  /* Кипичение при включении
   *  Индекс: 1
   *  Значения: 1 или 0
   *
   if (EEPROM[1]==1){
    boiling_on_start = true;
    now_status = BOILING;
   }
   /* Кипичение при резком снижении температуры
   *  Индекс: 2
   *  Значения: 1 или 0
   *
   if (EEPROM[2]==1){
    boiling_on_lowtemp = true;
   }

   /* Поддерживаемая температура
   *  Индекс: 3
   *  Значения: Byte 0-255
   *
   if (EEPROM[3]>20){
    need_temp = EEPROM[3];
   }
   */
/* Пример задания кнопок
  button1.attachClick(click1); //Клик
  button1.attachDoubleClick(doubleclick1); //Двойной клик
  
  button1.attachLongPressStart(longPressStart1); //Начало долгого зажатия
  button1.attachLongPressStop(longPressStop1); //Окончания долгого зажатия
  button1.attachDuringLongPress(longPress1); //Видимо то, что происходит во время зажатия btn_pomp_onLongStart
  */

//Кнопка помпы
  btn_pomp.attachClick(btn_pomp_onClick);
  btn_pomp.attachLongPressStart(btn_pomp_onLongStart); //Начало нажатия
  btn_pomp.attachLongPressStop(btn_pomp_onLongStop); //Окончание
//Кнопка кипечения
  btn_heating.attachClick(btn_heating_onClick); // вкл/выкл принудительного поддержания (вкл - всегда работает, выкл - подчиняется рассписанию)
  btn_heating.attachDoubleClick(btn_heating_onDoubleClick); // кипичение
  btn_heating.attachLongPressStop(btn_heating_onStopPress); // вкл/выкл принудительного отключения поддержания (вкл - всегда отключено, выкл - подчиняется расписанию)

  //Время
  rtc.writeProtect(false);
  rtc.halt(false);

//Включаем статус кипичения при старте, если оно включено
  if(boiling_on_start){
    now_status = BOILING;
  }
}

void loop() {
  current_temp = getTemperature();
  btn_pomp.tick();
  btn_heating.tick();

  //Чтение настроек
  if (Serial.available() > 0) {
    readCommands();
  }

  //Проверяем статусы
  //Если кипячение, то выполняем кипичение
  //Иначе выявляем поддерживаем ли температуру
  if (now_status == BOILING){
    //Кипячение
    ten_boaling_func();
  }else{
    if (!force_disabled){
      if (force_enabled){
        now_status = POTTING;
      }else{
        if (inShudle()) 
          now_status = POTTING; 
        else 
          now_status = DISABLED;
      }
    }else{
      now_status = DISABLED;
    }

    if (now_status == POTTING){
      //Включаем тэн поддержания
      ten_l_func();
      if (boiling_on_lowtemp) chekTemps(); //Если включено автокипичение при снижении температуры - следим за температурой и кипятим при большом перепаде
    }
    
    if (now_status == DISABLED){
      //Отключаем все тэны
      digitalWrite(TEN_L, HIGH);
      digitalWrite(TEN_H, HIGH);
    }
  }

  printDisplay();
}

/*
 * Функция поддержания температуры
 */
void ten_l_func(){
    if (((float)need_temp-temp_minus) >= current_temp){
        digitalWrite(TEN_L, LOW); //Если текущая температура меньше необходимой с мин порогом, то включаем тэн
        if (current_temp<60){ //Если температура менее 60, то включаем еще и сильный тэн
            digitalWrite(TEN_H, LOW);
        }else{
            digitalWrite(TEN_H, HIGH);
        }
    }else{
        if (((float)need_temp+temp_plus) <= current_temp){
            digitalWrite(TEN_L, HIGH); //Если текукщая температура больше необходимой с макс порогом, то выключаем тэн
        }else{
            digitalWrite(TEN_L, LOW); //Иначе включаем
            if (current_temp<60){ //Если температура менее 60, то включаем еще и сильный тэн
                digitalWrite(TEN_H, LOW);
            }else{
                digitalWrite(TEN_H, HIGH);
            }
        }
    }
}

/*
 * Функция кипичения
 */
void ten_boaling_func(){
    digitalWrite(TEN_H, LOW);
    digitalWrite(TEN_L, LOW);
    if (current_temp>temp_boiling){
      //Заменяем бичевский дилэй на работу с реальным временем
      //Каждый раз считываем значение секунды, если отличается снижаем boiling_tm
      //Итого, пока текущая температура больше температуры кипичения - отсчитывается таймер кипичения
      //После чего по условию ниже кипичение отключается
      Time t = rtc.time();
      if (timer_secs != t.sec){
        timer_secs = t.sec;
        boiling_tm--;
      }
    }
    if (boiling_tm<0){
      boiling_tm = boilint_time;
      digitalWrite(TEN_H, HIGH);
      digitalWrite(TEN_L, HIGH);
      now_status = DISABLED;
      if(debug)Serial.println("Кипичение закончено");
    }
}

/*
Функция выявления, находится ли текущее время в расписании
struct shoud_iteam {
  byte week;
  byte h_start;
  byte m_start;
  byte h_stop;
  byte m_stop;
};

shoud_iteam Shudle [100] = {
    {8, 7, 0, 9, 0},
    {8, 20, 0, 23, 59},
    {9, 9, 0, 23, 59}
};
*/
bool inShudle(){
  Time t = rtc.time();
  int week = dayAsInt(t.day);
  for (int i=0; i<101; i++){
      bool chek = false;
      if (Shudle[i].week<10){
          //t.hr, t.min
          if (Shudle[i].week==0){ //Во все дни
              chek = true;
          }else if(Shudle[i].week==week){ //В выбранный день
              chek = true;
          }else if(Shudle[i].week==8){ //В будни
              if (week>0 && week<6) chek = true;
          }else if(Shudle[i].week==9){ //В выходные
              if (week>5 && week<8) chek = true;
          }
      }
      //Проверили день недели, если он совпал, то проверяем вхождение в время
      if (chek){
          if (t.hr>=Shudle[i].h_start &&
              t.min>=Shudle[i].m_start &&
              t.hr<=Shudle[i].h_stop &&
              t.min<=Shudle[i].m_stop
              ) return true; //и провсто возвращаем тру, если входит
      }
  }
  return false; //все циклы пройдены, вхождений не найдено, возвращаем фолз
}

bool addShudle(shoud_iteam iteam){
    for (int i=0; i<101; i++){
        if(Shudle[i].week>10){
            Shudle[i] = iteam;
            return true;
        }
    }
}

bool delShudle(int adress, bool withmoove = false){
    if (withmoove){
        //С передвижением всех последующих
        if (adress<0 || adress>100) return false;
        if (adress<100){
            for (i=adress; i<100; i++){
                Shudle[i] = Shudle[i+1];
            }
            Shudle[100].week = 200;
        }else{
            Shudle[100].week = 200;
        }
    }else{
        //Просто флажим, как не используемую
        Shudle[i].week = 200;
    }
}

void btn_pomp_onLongStart(){
  digitalWrite(POMP, LOW);
}

void btn_pomp_onClick(){
  static byte m = LOW;
  m = !m;
  digitalWrite(POMP, m);
}

void btn_pomp_onLongStop(){
  digitalWrite(POMP, HIGH);
}

void btn_heating_onClick(){
    force_enabled = !force_enabled;
    if (force_enabled){
      if(debug)Serial.println("Поддержание температуры включено");
    }else{
      if(debug)Serial.println("Поддержание температуры отключено");
    }
}

void btn_heating_onStopPress(){
    force_disabled = !force_disabled;
    if (force_disabled){
      if(debug)Serial.println("Принудительное отключение вкл");
    }else{
      if(debug)Serial.println("Принудительное отключение выкл");
    }
}

void btn_heating_onDoubleClick(){
    now_status = BOILING;
    if(debug)Serial.println("Включено кипячение");
}

float getTemperature(){
  byte i;
  byte data[12];
  byte addr[8];
  ds.search(addr);
  ds.reset();
  ds.select(addr);
  ds.write(0x44, 1);
  ds.reset();
  ds.select(addr);    
  ds.write(0xBE);
  for ( i = 0; i < 9; i++) {
    data[i] = ds.read();
  }
  int16_t raw = (data[1] << 8) | data[0];
  byte cfg = (data[4] & 0x60);
  if (cfg == 0x00) raw = raw & ~7;
  else if (cfg == 0x20) raw = raw & ~3;
  else if (cfg == 0x40) raw = raw & ~1;
  return (float)raw / 16.0;
}

void clearComBuffer(){
  while (Serial.available()){
    Serial.read();
  }
}

void readCommands(){
    String cmd = "";
    char val = Serial.read();
    while (val != ';'){
        cmd += val;
        val = Serial.read();
    }

    if (cmd = "settime"){
        setTimeCom(); //Устанавливаем время
    }else if (cmd = "gettime"){
        Serial.println(getStrTime(false, true)); //Получаем время
    }else if (cmd = "getstatus"){
        getStatusCom(); //Получаем статус
    }else if (cmd = "setforce"){
        setForceCom(); //Устанавливаем Форсированные вкл/выкл
    }else if (cmd = "getforce"){
        getForceCom(); //Получаем Форсированные вкл/выкл
    }else if (cmd = "addshudle"){
        addShudleCom(); //Добавляем раписание
    }else if (cmd = "updshudle"){
        updShudleCom(); //Обновляем расписание по индексу
    }else if (cmd = "getshudle"){
        getShudleCom(); //Получаем все расписание
    }else if (cmd = "getsettings"){
        getSettingsCom(); //Получаем все настройки
    }else if (cmd = "setsettings"){
        setSettingsCom(); //Устанавливаем настройки
    }
    clearComBuffer(); //Чистим оставшийся мусор
}

void setTimeCom(){
  //char val = Serial.read();
  char val_ar[2];
  char val_ar4[4];
  // Установка времени
  // Подается строка ГОДмесяцДЕНЬчасМИНУТЫсекундыДЕНЬНЕДЕЛИ
  // Например 201801031309003
  // Тоесть 2018-01-03 13:09:00 Среда
  //
  // Args:
  //   yr: year. Range: {2000, ..., 2099}.
  //   mon: month. Range: {1, ..., 12}.
  //   date: date (of the month). Range: {1, ..., 31}.
  //   hr: hour. Range: {0, ..., 23}.
  //   min: minutes. Range: {0, ..., 59}.
  //   sec: seconds. Range: {0, ..., 59}.
  //   day: day of the week. Sunday is 1. Range: {1, ..., 7}.
  if(debug)Serial.println("Установка часов");
  //Год
  val_ar4[0] = Serial.read();
  val_ar4[1] = Serial.read();
  val_ar4[2] = Serial.read();
  val_ar4[3] = Serial.read();
  String yearr = val_ar4;
  //Месяц
  val_ar[0] = Serial.read();
  val_ar[1] = Serial.read();
  String monthh = val_ar;
  //День
  val_ar[0] = Serial.read();
  val_ar[1] = Serial.read();
  String dayim = val_ar;
  //Час
  val_ar[0] = Serial.read();
  val_ar[1] = Serial.read();
  String hourim = val_ar;
  //Минуты
  val_ar[0] = Serial.read();
  val_ar[1] = Serial.read();
  String mins = val_ar;
  //Секунды
  val_ar[0] = Serial.read();
  val_ar[1] = Serial.read();
  String secs = val_ar;
  //День недели, от 1 до 7 по Русскому стандарту
  val_ar[0] = '0';
  val_ar[1] = Serial.read();
  String daywek = val_ar;
    
  Time::Day dayw;
  switch (daywek.toInt()) {
    case 7:  dayw = Time::kSunday; break;
    case 1:  dayw = Time::kMonday; break;
    case 2:  dayw = Time::kTuesday; break;
    case 3:  dayw = Time::kWednesday; break;
    case 4:  dayw = Time::kThursday; break;
    case 5:  dayw = Time::kFriday; break;
    case 6:  dayw = Time::kSaturday; break;
    default: dayw = Time::kMonday; break;
  }
  
  if(debug)Serial.print("Год: ");
  if(debug)Serial.println(yearr);
  if(debug)Serial.print("Месяц: ");
  if(debug)Serial.println(monthh);
  if(debug)Serial.print("День: ");
  if(debug)Serial.println(dayim);
  if(debug)Serial.print("Часов: ");
  if(debug)Serial.println(hourim);
  if(debug)Serial.print("Минут: ");
  if(debug)Serial.println(mins);
  if(debug)Serial.print("Секунд: ");
  if(debug)Serial.println(secs);
  if(debug)Serial.print("День недели: ");
  if(debug)Serial.print(daywek.toInt());
  if(debug)Serial.print("/");
  if(debug)Serial.println(dayw);
  Time t(yearr.toInt(), monthh.toInt(), dayim.toInt(), hourim.toInt(), mins.toInt(), secs.toInt(), dayw);
  rtc.time(t);
  if(debug)Serial.print("Время установлено на ");
  printTime();
}

/*
 * Текущий статус, 1 байт
 * Текущая температура
 */
void getStatusCom(){
    Serial.print(now_status);
    Serial.print(';');
    Serial.println(current_temp);
}

/*
 * Установка принудительного включения отключения: первое значение, от 0 до 2, 0 - по расписанию, 1 - прин. вкл, 2 - прин. выкл.
 * Установка кипичения при снижении температуры: второе значение, 0 - отключено, 1 - включено
 */
void setForceCom(){
    if(debug)Serial.println("Установка принудиловки");
    char force = Serial.read();

    switch (force) {
    case '0':
        force_disabled = false;
        force_enabled = false;
        break;
    case '1':
        force_disabled = false;
        force_enabled = true;
        break;
    case '2':
        force_disabled = true;
        force_enabled = false;
        break;
    default:
        force_disabled = false;
        force_enabled = false;
        break;
    }
    Serial.println(force);
}

void getForceCom(){
    char force;
    if (!force_disabled && !force_enabled){
        force = '0';
    }else{
        if (force_enabled){
            force = '1';
        }else{
            force = '2';
        }
    }
    Serial.println(force);
}

void addShudleCom(){

}

void updShudleCom(){

}

void getShudleCom(){

}

/*
 * Получаем настройки
 * Значения перечисляются по порядку
 *
 */
void getSettingsCom(){

}

void setSettingsCom(){

}

void printDisplay(){
  String stat = "";
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0,0);
  display.println(getStrTime(true));
  display.print(utf8rus("Темп: "));
  display.print(current_temp);
  display.print(utf8rus(" Нужн: "));
  display.println(need_temp);
  display.print(utf8rus("Статус: "));
  if (now_status == BOILING){
    stat = "Кипячение";
  }else{
    if (force_disabled){
      stat = "Принуд. Откл.";
    }else{
      if (force_enabled){
        stat = "Принуд. Вкл.";
      }else{
        stat = "Распис.";
        if (now_status==0){
          stat += ", ОТКЛ";
        }else{
          stat += ", ВКЛ";
        }
      }
    }
  }
  display.println(utf8rus(stat));
  display.print(utf8rus("Авто кипячение: "));
  if (boiling_on_lowtemp){
      display.print(utf8rus("ВЛК"));
  }else{
      display.print(utf8rus("ВЫКЛ"));
  }
  display.display();
  delay(100);
}

/*
double temps[50];
byte countTemps = 50;
byte iTemp = 0;
 */
void chekTemps(){
  temps[iTemp] = current_temp;
  iTemp++;
  if (iTemp>=countTemps) iTemp = 0;
  //if (iTemp>10){
    double ab;
    for (byte i=0; i<countTemps;i++){
      if(i%2){
        ab += temps[iTemp];
      }else{
        ab -= temps[iTemp];
      }
    }
    ab = abs(ab);
    if (ab>40) now_status = BOILING;
  //}
}

