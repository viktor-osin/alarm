/************************************************************************************************************************************
Программный код для охранной сигнализации (системы оповещения)
Датчик Холла - OH49E

Автор: Виктор Осин
Сайт проекта "Программирование микроконтроллеров" - http://progmk.ru
Сообщество Вконтакте - http://vk.com/progmk
Видео о работе с датчиком Холла: https://www.youtube.com/watch?v=ASj_eXTkPxY
*************************************************************************************************************************************/

#define DELAY_CLOSE 180000    //время на покидание помещения и закрытие двери (3 минуты - 180 сек)
#define DELAY_OPEN 30000      //время на нажатие кнопки после открытия двери (30 сек)

#include <Wire.h> 
#include <LiquidCrystal_I2C.h>  //библиотека для LCD экрана
#include "DHT.h"                //библиотека для работы датчика температуры и влажности
#define DHTPIN 3                //датчик подключен ко входу 3
#define DHTTYPE DHT22  


LiquidCrystal_I2C lcd(0x27,16,2);  // Устанавливаем дисплей

float temph[2];                                 //массив для температуры и влажности
volatile unsigned long int timer = 0;           //переменная для таймера кнопки
volatile unsigned long int clock = 0;           //переменная для сохранения значения таймера кнопки
volatile boolean timerOn = 0;                   //переменная для включения таймера кнопки

volatile unsigned long int alarmTimer = 0;      //переменная для таймера включения/выключения сигнализации
volatile unsigned long int alarmClock = 0;      //переменная для сохранения значения таймера
volatile boolean alarmTimerOn = 0;              //переменная для запуска/остановки таймера

volatile unsigned long int tempTimer = 0;       //переменная для таймера обновления показаний температуры и влажности
volatile unsigned long int tempClock = 0;       //переменная для сохранения значения таймера
volatile boolean tempTimerOn = 0;               //переменная для запуска/остановки таймера

int hollaState;                                //переменная для хранения показания с датчика Холла (0-1024) - аналоговый вход
boolean buttonState;                           //хранение значения кнопки (нажата/отпущена)

DHT dht(DHTPIN, DHTTYPE);                      //настройка датчика температуры и влажности


ISR (TIMER0_COMPA_vect)  	  //функция, вызываемая таймером-счетчиком каждые 0,001 сек
{
  if(timerOn == 1)                //если таймер включен
  {
      timer++;                    //увеличение значения таймера на +1 каждые 0,001 сек
  }
  if(alarmTimerOn == 1)
  {
      alarmTimer++;
  }
  
  if(tempTimerOn == 1)
  {
      tempTimer++;
  }
}

void setup()
{
  Serial.begin(9600);
  lcd.init();                         //инициализация дисплея                     
  lcd.backlight();                    //подсветка дисплея
  lcd.setCursor(0, 0);                //установка курсора на нулевую строку и нулевой столбец  
  lcd.print("Hello user :)    ");     //приветственное сообщение (здесь и далее пробелы нужны для стирания возможных оставшихся символов)
  lcd.setCursor(0, 1);                //установка курсора на первую строку и нулевой столбец 
  lcd.print("Alarm OFF     ");
  
  dht.begin();                        //инициализация датчика температуры
  
  pinMode(8, INPUT_PULLUP);           //пин 8 как вход с подтягивающим резистором (для кнопки)
  pinMode(11, OUTPUT);                //пин 11 как выход для пьезо-пищалки
  digitalWrite(11, LOW);              //устанавливаем нулевой уровень 11 вывода
  pinMode(A3, OUTPUT);                //А3 используется как дополнительный выход +5В - просто мне так было удобнее при пайке проводов :)
  digitalWrite(A3, HIGH);
  pinMode(9, OUTPUT);                 //пин 9 как выход для пьезо-пищалки (можно было оставить один 11й выход, но мне, опять же, так было удобнее при пайке)
  digitalWrite(9, LOW);
  
   //Настройка таймера на срабатывание каждые 0,001 сек
  TCCR0A |= (1 << WGM01);
  OCR0A = 0xF9;                    //начало отсчета до переполнения (255)
  TIMSK0 |= (1 << OCIE0A);         //Set the ISR COMPA vect
  sei();                           //разрешить прерывания
  TCCR0B |= (1 << CS01) | (1 << CS00); //установить делитель частоты на 64
  //теперь каждые 0,001 сек будет вызываться функция ISR (TIMER0_COMPA_vect)
  
  digitalWrite(9, HIGH);      //приветственный "пик"
  digitalWrite(11, HIGH);
  delay(200);
  digitalWrite(9, LOW);
  digitalWrite(11, LOW);
  delay(200);
}

void loop()      //главная циклическая функция
{
   readState();                //считывем значение с датчиков (кнопка, температура, влажность, датчик Холла)
   if(!buttonState)            //если кнопка нажата
   {
     timerOnNull();            //обнуляем и запускаем таймер кнопки
     while(!buttonState)       //пока конпка нажата
     {
       readState();            //считывем значение с датчиков (кнопка, температура, влажность, датчик Холла)
       cli();                  //останавливаем прерывания
       clock = timer;          //сохраняем значения с таймера кнопки
       sei();                  //возобновляем прерывания
       if(clock >= 3000)       //если кнопку удерживают больше 3-х секунд
       {
         cli();                //останавливаем и обнуляем таймер
         timerOn = 0;
         timer = 0;
         clock = 0;
         sei();
         startAlarm();        //переходим в функцию запуска сигнализации
       }
       if((buttonState) && (clock >= 1000))  //если кнопку отпустили раньше времени
       {
         cli();            //обнуляем и останавливаем таймер, ждем повторного нажатия и удержания кнопки
         timerOn = 0;
         timer = 0;
         clock = 0;
         sei();                      
       }
     }
   }
}

void readState()       //функция считывания датчиков
{
  temp();              //считывание показаний с датчика DHT
  buttonState = digitalRead(8);  //считывение кнопки
  int val = analogRead(0);       //считывание показаний датчика Холла
  if((val >= 490) && (val <= 900))  //если показание находится в пределах от 490 до 900 (магнит поднесен)
  {
    hollaState = 1;              //фиксируем что дверь закрыта 
  }
  else
  {
    hollaState = 0;              //иначе дверь открыта (нет магнитного поля, обрыв витой пары, КЗ на землю)
  }
}

void startAlarm()                //функция включения и опрашивания датчиков
{
  digitalWrite(11, HIGH);        //звуковое уведомление - один "пик"
  digitalWrite(9, HIGH);
  delay(100);
  digitalWrite(11, LOW);
  digitalWrite(9, LOW);
  lcd.setCursor(0, 1);
  lcd.print("    Alarm wait..");
  alarmtimerOnNull();               //обнуление и старт таймера для задержки перед включением опрашивания датчика Холла
  if (delayBeforeInclusion() == 1)  //если задержка не была выдержана и кнопка была нажата, 
  {
    return;                         //то выключаем сигнализацию и переходим в начало алгоритма
  }
  readState();                      //после выдержки времени считываем показания с датчиков
  if(alarmON() == 1)                //переходим в функцию постоянного считывания кнопки и датчика Холла
  {
    return;                        //если будет нажата кнопка, то выключаем сигнализацию и переходим в начало алгоритма
  }
  //если будет открыта дверь, либо западет кнопка при изначальном нажатии включения, то переходим к двум условиям ниже
  if(!buttonState)                //если запала кнопка, говорим что бы ее отпустили
  {
      lcd.setCursor(0, 1);
      lcd.print("Release button!     ");
      for(int i=0; i<3; i++)
      {
      digitalWrite(11, HIGH);
      digitalWrite(9, HIGH);
      delay(500);
      digitalWrite(11, LOW);
      digitalWrite(9, LOW);
      delay(100);
      }
  }
  if(!hollaState)                //если сработал датчик - дверь открыта
  {
      lcd.setCursor(0, 1);
      lcd.print("Press unblock!     ");    //то пишем "Нажмите разблокировать"
      if(delayBeforeSignal() == 1)    //ожидаем разблокировки в течение 30 секунд
      {
        return;                       //если кнопка была нажата, выключаем сигнализацию и возвращаемся в начало алгоритма
      }
      readState();                    //если кнопку не нажали и 30 секунд прошло, вновь считываем показания датчика
      if(!hollaState)                 //если дверь все еще открыта 
      {
          if(alarm() == 1)            //переходим в функцию звукового оповещения и находимся в ней до тех пор, пока не будет нажата кнопка
          {
            return;
          }         
      }
      else                           //если дверь закрыта, делаем вывод о ложном срабатывании и возвращаемся к запуску сигнализации
      {
          startAlarm();
      }
  }  
}

int alarmON()                    //функция считывания кнопки и датчика Холла
{
  while(buttonState && hollaState) //пока кнопка не нажата и дверь закрыта, считываем показания датчиков
  {
         lcd.setCursor(0, 1);
         lcd.print("    Alarm ON!   "); 
         readState(); 
         if(!buttonState)    //если кнопка нажата
         {
           lcd.setCursor(0, 1);
           lcd.print("Hold the button..  ");
           timerOnNull();
           while(!buttonState)
           {
                 readState();
                 cli();
                 timerOn = 1;
                 clock = timer;
                 sei();
                 if(clock >= 2000)
                 {
                   cli();
                   timerOn = 0;
                   timer = 0;
                   clock = 0;
                   sei();
                   stopAlarm();      //функция остановки работы сигнализции
                   return 1;
                 }
                 if((buttonState) && (clock >= 100))
                 {
                   timerOnNull();
                 }
               }
       }
  }
}


int delayBeforeSignal()      //задержка после открытия двери при работающей сигнализации
{
  alarmtimerOnNull();
  while(alarmClock <= DELAY_OPEN)    //время на нажатие кнопки после открытия двери (30 сек)
  {
    //Serial.println(alarmClock);
    cli();
    alarmTimerOn = 1;
    alarmClock = alarmTimer;
    sei();
    readState(); 
   if(!buttonState)    //если кнопка нажата
   {
     timerOnNull();
     while(!buttonState)
     {
           readState();
           cli();
           timerOn = 1;
           clock = timer;
           sei();
           if(clock >= 2000)
           {
             cli();
             timerOn = 0;
             timer = 0;
             clock = 0;
             sei();
             stopAlarm();
             return 1;
           }
           if((buttonState) && (clock >= 100))
           {
             timerOnNull();
           }
         }
        
      }
  }
  cli();
  alarmTimerOn = 0;
  alarmTimer = 0;
  alarmClock = 0;
  sei();
}

void stopAlarm()        //функция уведомления об остановке
{
  lcd.setCursor(0, 1);
  lcd.print("Alarm OFF!      ");
  
  for(int i=0; i<3; i++)
  {
    digitalWrite(11, HIGH);
    digitalWrite(9, HIGH);
    delay(100);
    digitalWrite(11, LOW);
    digitalWrite(9, LOW);
    delay(100);
  }
  return;
}

int alarm()        //функция оповещения об открытии двери (при выдержанной задержке на включение)
{
  lcd.setCursor(0, 1);
  lcd.print("Door is open!    ");
  while(buttonState)
  {
         digitalWrite(11, HIGH);
         digitalWrite(9, HIGH);
         delay(40);
         digitalWrite(11, LOW);
         digitalWrite(9, LOW);
         delay(40);
         readState(); 
         if(!buttonState)    //если кнопка нажата
         {
           timerOnNull();
           while(!buttonState)
           {
                 readState();
                 cli();
                 timerOn = 1;
                 clock = timer;
                 sei();
                 if(clock >= 2000)
                 {
                   cli();
                   timerOn = 0;
                   timer = 0;
                   clock = 0;
                   sei();
                   stopAlarm();
                   return 1;
                 }
                 if((buttonState) && (clock >= 100))
                 {
                   timerOnNull();
                 }
           }
       }
  }
}

void temp()        //функция считывания показаний датчика DHT22
{
    cli();
    tempTimerOn = 1;
    tempClock = tempTimer;
    sei();
    
    if(tempClock >= 1000)
    {
        cli();
        tempTimerOn = 1;
        tempTimer = 0;
        tempClock = 0;
        sei();    
        float t = dht.readTemperature();
        float h = dht.readHumidity(); 
        temph[0] = float(t);
        temph[1] = float(h);
        lcd.setCursor(0, 0);
        lcd.print(temph[0]);
        lcd.setCursor(5, 0);
        lcd.print("C ");
        lcd.setCursor(7, 0);
        lcd.print("|| ");
        lcd.setCursor(10, 0);
        lcd.print(temph[1]);
        lcd.setCursor(15, 0);
        lcd.print("H");
    }
    
    if(temph[0] >= 60)
    {
      lcd.setCursor(0, 1);
      lcd.print("Attantion, fire!      ");
      digitalWrite(11, LOW);
      digitalWrite(9, LOW);
      delay(40);
      digitalWrite(11, HIGH);
      digitalWrite(9, HIGH);
    }
}

int delayBeforeInclusion()          //функция здержки перед включением считывания двтчика Холла
{
  while(alarmClock <= DELAY_CLOSE)    //время на покидание помещения и закрытие двери (3 минуты - 180 сек)
  {
     //Serial.println(alarmClock);
     cli();
     alarmClock = alarmTimer;
     sei();
     readState(); 
     if(!buttonState)    //если кнопка нажата
     {
       timerOnNull();
       while(!buttonState)
       {
             readState();
             cli();
             timerOn = 1;
             clock = timer;
             sei();
             if(clock >= 2000)
             {
               cli();
               timerOn = 0;
               timer = 0;
               clock = 0;
               sei();
               stopAlarm();
               return 1;
             }
             if((buttonState) && (clock >= 100))
             {
               timerOnNull();
             }
         }
      }
  }
  cli();
  alarmTimerOn = 0;
  alarmTimer = 0;
  alarmClock = 0;
  sei();
}

void timerOnNull()    //функуия обнуления и запуска таймера кнопки
{
   cli();
   timerOn = 1;
   timer = 0;
   clock = 0;
   sei();
}

void alarmtimerOnNull()    //функция обнуления и запуска таймера задержек на включение/выключение
{
  cli();
  alarmTimerOn = 1;
  alarmTimer = 0;
  alarmClock = 0;
  sei();
}
