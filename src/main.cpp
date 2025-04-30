/*
 * Stack-chan-tester
 * 
 * @author TakaoAkaki
 * Copyright (c) 2021-2024 Takao Akaki. All right reserved
 */

// ------------------------
// ヘッダファイルのinclude
// 
#include <Arduino.h>                         // Arduinoフレームワークを使用する場合は必ず必要
#include <SD.h>                              // SDカードを使うためのライブラリです。
#include <Update.h>                          // 定義しないとエラーが出るため追加。
#include <Ticker.h>                          // 定義しないとエラーが出るため追加。
#include <M5Unified.h>                       // M5Unifiedライブラリ
#include <Stackchan_system_config.h>         // stack-chanの初期設定ファイルを扱うライブラリ
#include <Stackchan_servo.h>                 // stack-chanのサーボを動かすためのライブラリ
#include <Avatar.h>                          // 顔を表示するためのライブラリ https://github.com/meganetaaan/m5stack-avatar
#include "formatString.hpp"                  // 文字列に変数の値を組み込むために使うライブラリ https://gist.github.com/GOB52/e158b689273569357b04736b78f050d6
// ヘッダファイルのinclude end 
// ================================== End

// ---------------------------------------------
// グローバル変数の定義エリア
// プログラム全体で利用する変数やクラスを決める
int servo_offset_x = 0;  // X軸サーボのオフセット（サーボの初期位置からの+-で設定）
int servo_offset_y = 0;  // Y軸サーボのオフセット（サーボの初期位置からの+-で設定）

using namespace m5avatar;     // (Avatar.h)avatarのnamespaceを使う宣言（こうするとm5avatar::???と書かなくて済む。)
Avatar avatar;                // (Avatar.h)avatarのクラスを定義
ColorPalette *cps[2];

StackchanSERVO servo;                         // (Stackchan_servo.h) サーボを扱うためのクラス
StackchanSystemConfig system_config;          // (Stackchan_system_config.h) プログラム内で使用するパラメータをYAMLから読み込むクラスを定義

uint32_t mouth_wait = 2000;       // 通常時のセリフ入れ替え時間（msec）
uint32_t last_mouth_millis = 0;   // セリフを入れ替えた時間
bool core_port_a = false;         // Core1のPortAを使っているかどうか

const char* lyrics[] = { "手を振ってみて ", "左から右へ手をかざしてみて", "右から左へ手をかざしてみて"};  // 通常モード時に表示するセリフ
const int lyrics_size = sizeof(lyrics) / sizeof(char*);  // セリフの数
int lyrics_idx = 0;                                      // 表示するセリフ数用の変数
// ================================== End


// ランダムモード(loop()でBtnCが押されると実行されます。)
void moveRandom(void *args) {
  for (;;) { // 無限ループ（BtnCが押されるまでランダムモードを繰り返します。
    // ランダムモード
    int x = random(system_config.getServoInfo(AXIS_X)->lower_limit + 90, system_config.getServoInfo(AXIS_X)->upper_limit - 90);  // 可動範囲の下限+45〜上限-45 でランダム
    int y = random(system_config.getServoInfo(AXIS_Y)->lower_limit, system_config.getServoInfo(AXIS_Y)->upper_limit);            // 可動範囲の下限〜上限 でランダム
#ifdef ARDUINO_M5STACK_CORES3
    unifiedButton.update(); // M5.update() よりも前に呼ぶ事
#endif
    M5.update();
    if (M5.BtnC.wasPressed()) {
      // ランダムモードを抜ける処理。(loop()に戻ります。)
      break;
    }
    uint16_t base_delay_time = 0;
    if (system_config.getServoType() == ServoType::SCS || system_config.getServoType() == ServoType::DYN_XL330) {
      base_delay_time = 500;
    }
    int delay_time = random(10);
    servo.moveXY(x, y, 1000 + (100 + base_delay_time) * delay_time);
    delay(2000 + 500 * delay_time);
    //avatar.setSpeechText("Stop BtnC");
    avatar.setSpeechText("");
  }
}
// ============================================== End


// ------------------------------------------------------------------
// Arduinoフレームワークで一番最初に実行される関数の定義
// void setup()とvoid loop()は必ず必要です。
// void setup()は、最初に1回だけ実行します。
void setup() {
  auto cfg = M5.config();       // 設定用の情報を抽出
  cfg.serial_baudrate = 115200;
  M5.begin(cfg);                // M5Stackをcfgの設定で初期化
#ifdef ARDUINO_M5STACK_CORES3 // CORES3のときに有効になります。
  // 画面上のタッチパネルを3分割してBtnA, B, Cとして使う設定。
  unifiedButton.begin(&M5.Display, goblib::UnifiedButton::appearance_t::transparent_all);
#endif
  M5.Log.setLogLevel(m5::log_target_display, ESP_LOG_NONE);    // M5Unifiedのログ初期化（画面には表示しない。)
  M5.Log.setLogLevel(m5::log_target_serial, ESP_LOG_INFO);     // M5Unifiedのログ初期化（シリアルモニターにESP_LOG_INFOのレベルのみ表示する)
  M5.Log.setEnableColor(m5::log_target_serial, false);         // M5Unifiedのログ初期化（ログをカラー化しない。）
  M5_LOGI("Hello World");                                      // logにHello Worldと表示
  SD.begin(GPIO_NUM_4, SPI, 25000000);                         // SDカードの初期化
  delay(2000);                                                 // SDカードの初期化を少し待ちます。
 
  system_config.loadConfig(SD, "");                            // SDカードから初期設定ファイルを読み込む
  // servoの初期化
  M5_LOGI("attach servo"); // ログへ出力
  // サーボの初期化を行います。（このとき、初期位置（正面）を向きます。）
  servo.begin(system_config.getServoInfo(AXIS_X)->pin, system_config.getServoInfo(AXIS_X)->start_degree,
              system_config.getServoInfo(AXIS_X)->offset,
              system_config.getServoInfo(AXIS_Y)->pin, system_config.getServoInfo(AXIS_Y)->start_degree,
              system_config.getServoInfo(AXIS_Y)->offset,
              (ServoType)system_config.getServoType());


  M5_LOGI("ServoType: %d\n", system_config.getServoType());      // サーボのタイプをログに出力
  //USBSerial.println("HelloWorldUSBSerial");
  avatar.init();                   // avatarを初期化して実行開始します。(このときに顔が表示されます。)
  cps[0] = new ColorPalette();
  cps[0]->set(COLOR_PRIMARY, TFT_BLACK);
  cps[0]->set(COLOR_BACKGROUND, TFT_WHITE);
  avatar.setColorPalette(*cps[0]);
  avatar.setSpeechFont(&fonts::efontCN_12); // セリフのフォントを設定します。

  last_mouth_millis = millis();    // loop内で使用するのですが、処理を止めずにタイマーを実行するための変数です。一定時間で口を開くのとセリフを切り替えるのに利用します。
  avatar.addTask(moveRandom, "moveRandom"); // ランダムモードのタスクを追加します。
}


// メインのloop処理。（必ず定義が必要。）
// 基本的にはずっと繰り返し実行されます。
// stack-chan-testerの場合は、ボタンを押して、各モードの関数が実行されると一時停止します。
void loop() {
  M5.update();  // M5Stackのボタン状態を更新します。

  if ((millis() - last_mouth_millis) > mouth_wait) {             // 口を開けるタイミングを待ちます。時間を図ってmouth_waitで設定した時間が経つと繰り返します。
    const char* l = lyrics[lyrics_idx++ % lyrics_size];          // セリフを取り出します。
    avatar.setSpeechText(l);                                     // 吹き出しにセリフを表示します。
    avatar.setMouthOpenRatio(0.7);                               // アバターの口を70%開きます。(70%=0.7)
    delay(200);                                                  // 200ミリ秒口を開いたまま
    avatar.setMouthOpenRatio(0.0);                               // 口を閉じます。(0%=0.0)
    last_mouth_millis = millis();                                // 実行時間を更新。（この時点からまたmouth_wait分待ちます。）
  }

}