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
#include <Wire.h>                            // I2C通信のためのライブラリ
#include "M5_STHS34PF80.h"                   // TMOS PIR Unitのライブラリ
#include <DFRobot_PAJ7620U2.h>               // ジェスチャーセンサーのライブラリ
// ヘッダファイルのinclude end 
// ================================== End

// ---------------------------------------------
// グローバル変数の定義エリア
// プログラム全体で利用する変数やクラスを決める
int servo_offset_x = 0;  // X軸サーボのオフセット（サーボの初期位置からの+-で設定）
int servo_offset_y = 0;  // Y軸サーボのオフセット（サーボの初期位置からの+-で設定）

using namespace m5avatar;     // (Avatar.h)avatarのnamespaceを使う宣言（こうするとm5avatar::???と書かなくて済む。)
Avatar avatar;                // (Avatar.h)avatarのクラスを定義
ColorPalette *cps[6];        // 5段階の色変化 + 初期色の合計6色のパレットを定義

// TMOS PIR Unitのセンサーを定義
M5_STHS34PF80 TMOS;

// 距離検出用の変数
int16_t presenceVal = 0;     // 存在検出値
int16_t motionVal = 0;       // 動き検出値
int currentColorIndex = 0;   // 現在の色インデックス

// ジェスチャーセンサーを定義
DFRobot_PAJ7620U2 gestSensor;

// ジェスチャー検出用の変数
bool gestureInitialized = false;  // ジェスチャーセンサーの初期化状態
uint32_t lastGestureTime = 0;     // 最後にジェスチャーを検出した時間
const uint32_t gestureInterval = 500; // ジェスチャー検出の間隔（ミリ秒）

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
  
  // カラーパレットの初期化 - 5段階の色変化 + 初期色
  // 初期色（白背景に黒の顔）
  cps[0] = new ColorPalette();
  cps[0]->set(COLOR_PRIMARY, TFT_BLACK);
  cps[0]->set(COLOR_BACKGROUND, TFT_YELLOW);
  
  // 距離が近づくにつれて変化する色（5段階）
  // 1段階目（薄い青）
  cps[1] = new ColorPalette();
  cps[1]->set(COLOR_PRIMARY, TFT_BLACK);
  cps[1]->set(COLOR_BACKGROUND, TFT_RED);
  
  // 2段階目（青）
  cps[2] = new ColorPalette();
  cps[2]->set(COLOR_PRIMARY, TFT_BLACK);
  cps[2]->set(COLOR_BACKGROUND, TFT_BLUE);
  
  // 3段階目（黄色）
  cps[3] = new ColorPalette();
  cps[3]->set(COLOR_PRIMARY, TFT_BLACK);
  cps[3]->set(COLOR_BACKGROUND, TFT_CYAN);
  
  // 4段階目（オレンジ）
  cps[4] = new ColorPalette();
  cps[4]->set(COLOR_PRIMARY, TFT_BLACK);
  cps[4]->set(COLOR_BACKGROUND, TFT_ORANGE);
  
  // 5段階目（赤 - 最も近い）
  cps[5] = new ColorPalette();
  cps[5]->set(COLOR_PRIMARY, TFT_BLACK);
  cps[5]->set(COLOR_BACKGROUND, TFT_YELLOW);
  
  // 初期カラーパレットを設定
  avatar.setColorPalette(*cps[0]);
  avatar.setSpeechFont(&fonts::efontCN_12); // セリフのフォントを設定します。

  last_mouth_millis = millis();    // loop内で使用するのですが、処理を止めずにタイマーを実行するための変数です。一定時間で口を開くのとセリフを切り替えるのに利用します。
  avatar.addTask(moveRandom, "moveRandom"); // ランダムモードのタスクを追加します。
  
  // TMOS PIR Unitの初期化
  M5_LOGI("Initializing TMOS PIR Unit");
  while (TMOS.begin(&Wire, STHS34PF80_I2C_ADDRESS, 32, 33) == false) {
    M5_LOGI("TMOS PIR Unit initialization failed. Retrying...");
    delay(500);
  }
  M5_LOGI("TMOS PIR Unit initialized successfully");
  
  // TMOS PIR Unitの設定
  TMOS.setTmosODR(STHS34PF80_TMOS_ODR_AT_30Hz);  // 30Hzでデータを取得
  TMOS.setPresenceThreshold(0xC8);                // 存在検出の閾値
  TMOS.setMotionThreshold(0xC8);                  // 動き検出の閾値
  TMOS.setPresenceHysteresis(0x32);               // 存在検出のヒステリシス
  TMOS.setMotionHysteresis(0x32);                 // 動き検出のヒステリシス
  TMOS.setGainMode(STHS34PF80_GAIN_DEFAULT_MODE); // デフォルトのゲインモード
  
  // ジェスチャーセンサーの初期化
  M5_LOGI("Initializing Gesture Sensor");
  Wire1.begin(26, 32);  // SDA, SCL ピンを指定（環境に合わせて調整）
  if (gestSensor.begin() == 0) {
    M5_LOGI("Gesture Sensor initialized successfully");
    gestSensor.setGestureHighRate(true);  // 高速モードに設定
    gestureInitialized = true;
  } else {
    M5_LOGI("Gesture Sensor initialization failed");
  }
}


// メインのloop処理。（必ず定義が必要。）
// 基本的にはずっと繰り返し実行されます。
// stack-chan-testerの場合は、ボタンを押して、各モードの関数が実行されると一時停止します。
void loop() {
  M5.update();  // M5Stackのボタン状態を更新します。
  
  // TMOS PIR Unitからデータを読み取る
  sths34pf80_tmos_drdy_status_t dataReady;
  TMOS.getDataReady(&dataReady);
  
  if (dataReady.drdy == 1) {
    // 存在検出値と動き検出値を取得
    sths34pf80_tmos_func_status_t status;
    TMOS.getPresenceValue(&presenceVal);
    TMOS.getMotionValue(&motionVal);
    TMOS.getStatus(&status);
    
    // 存在検出値に基づいて顔の色を変更
    // presenceValは距離に反比例する（近いほど値が大きい）
    int newColorIndex = 0;
    
    // 存在検出値に基づいて5段階の色変化を実装
    // 3mから50cmまでの距離を5段階に分ける
    if (presenceVal > 0) {
      if (presenceVal < 100) {
        newColorIndex = 1;  // 最も遠い（約3m）
      } else if (presenceVal < 200) {
        newColorIndex = 2;  // 遠い（約2m）
      } else if (presenceVal < 400) {
        newColorIndex = 3;  // 中間（約1.5m）
      } else if (presenceVal < 800) {
        newColorIndex = 4;  // 近い（約1m）
      } else {
        newColorIndex = 5;  // 最も近い（約50cm以下）
      }
    }
    
    // 色が変わった場合のみ更新
    if (newColorIndex != currentColorIndex) {
      currentColorIndex = newColorIndex;
      avatar.setColorPalette(*cps[currentColorIndex]);
      
      // デバッグ情報をシリアルに出力
      M5_LOGI("Distance changed: presenceVal=%d, colorIndex=%d", presenceVal, currentColorIndex);
    }
  }
  
  // ジェスチャーセンサーの処理
  if (gestureInitialized && (millis() - lastGestureTime > gestureInterval)) {
    // ジェスチャーを取得
    DFRobot_PAJ7620U2::eGesture_t gesture = gestSensor.getGesture();
    
    // ジェスチャーに応じてサーボを動かす
    if (gesture != gestSensor.eGestureNone) {
      String description = gestSensor.gestureDescription(gesture);
      M5_LOGI("Detected gesture: %s", description.c_str());
      
      // 現在のX軸の位置を取得
      int centerX = system_config.getServoInfo(AXIS_X)->start_degree;
      int centerY = system_config.getServoInfo(AXIS_Y)->start_degree;
      servo.moveXY(centerX, centerY, 500);

      // ジェスチャーに応じてサーボを動かす
      switch (gesture) {
        case gestSensor.eGestureRight:
          // 右にサーボを動かす
          avatar.setSpeechText("左へ向きます");
          servo.moveX(centerX - 45, 1000);
          break;
          
        case gestSensor.eGestureLeft:
          // 左にサーボを動かす
          avatar.setSpeechText("右へ向きます");
          servo.moveX(centerX + 45, 1000);
          break;
          
        case gestSensor.eGestureUp:
          // 上を向く
          avatar.setSpeechText("下を向きます");
          servo.moveY(centerY + 20, 1000);
          break;
          
        case gestSensor.eGestureDown:
          // 下を向く
          avatar.setSpeechText("上を向きます");
          servo.moveY(centerY - 20, 1000);
          break;
          
        case gestSensor.eGestureWave:
          // 手を振ると左右に首振り
          avatar.setSpeechText("こんにちは！");
          // 左右に首を振る
          servo.moveX(centerX - 40, 300);
          delay(400);
          servo.moveX(centerX + 40, 300);
          delay(400);
          servo.moveX(centerX, 300);
          break;
          
        default:
          // その他のジェスチャー
          avatar.setSpeechText(description.c_str());
          break;
      }
      delay(2000);
      
      // 最後にジェスチャーを検出した時間を更新
      lastGestureTime = millis();
    }
  }
  
  // 口を動かす処理
  if ((millis() - last_mouth_millis) > mouth_wait) {             // 口を開けるタイミングを待ちます。時間を図ってmouth_waitで設定した時間が経つと繰り返します。
    const char* l = lyrics[lyrics_idx++ % lyrics_size];          // セリフを取り出します。
    avatar.setSpeechText(l);                                     // 吹き出しにセリフを表示します。
    avatar.setMouthOpenRatio(0.7);                               // アバターの口を70%開きます。(70%=0.7)
    delay(200);                                                  // 200ミリ秒口を開いたまま
    avatar.setMouthOpenRatio(0.0);                               // 口を閉じます。(0%=0.0)
    last_mouth_millis = millis();                                // 実行時間を更新。（この時点からまたmouth_wait分待ちます。）
  }
}